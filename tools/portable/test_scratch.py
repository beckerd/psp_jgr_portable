"""Exercise setup scratch streams in RAM, after spilling, and under file limits."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
DRIVER = r'''
#include "portable_scratch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>
static FILE *files[32];static unsigned opened,maximum,limit;
static unsigned reads,seeks,fail_seek,metadata_checks,removes,fail_close,fail_remove;static size_t read_bytes;
static unsigned fail_write;
int lstat(const char *p,struct stat *s){int (*fn)(const char*,struct stat*)=dlsym(RTLD_NEXT,"lstat");metadata_checks++;return fn(p,s);}
int remove(const char *p){int (*fn)(const char*)=dlsym(RTLD_NEXT,"remove");removes++;if(fail_remove){fail_remove=0;errno=EACCES;return -1;}return fn(p);}

static int backing(FILE *f){for(unsigned i=0;i<32;i++)if(files[i]==f)return 1;return 0;}
size_t fwrite(const void *data,size_t size,size_t count,FILE *f){
 size_t (*system_fwrite)(const void *,size_t,size_t,FILE *)=dlsym(RTLD_NEXT,"fwrite");
 if(backing(f)&&fail_write){fail_write=0;size_t n=system_fwrite(data,size,count/2,f);errno=EIO;return n;}
 return system_fwrite(data,size,count,f);
}
size_t fread(void *data,size_t size,size_t count,FILE *f){
 size_t (*system_fread)(void *,size_t,size_t,FILE *)=dlsym(RTLD_NEXT,"fread");
 if(backing(f)){reads++;read_bytes+=size*count;}return system_fread(data,size,count,f);
}
int fseek(FILE *f,long at,int whence){
 int (*system_fseek)(FILE *,long,int)=dlsym(RTLD_NEXT,"fseek");
 if(backing(f)){seeks++;if(fail_seek){fail_seek=0;errno=EIO;return -1;}}
 return system_fseek(f,at,whence);
}
FILE *fopen(const char *name,const char *mode){
 if(limit && opened>=limit){errno=EMFILE;return NULL;}
 FILE *(*system_fopen)(const char *,const char *)=dlsym(RTLD_NEXT,"fopen");
 FILE *f=system_fopen(name,mode);if(f){for(unsigned i=0;i<32;i++)if(!files[i]){files[i]=f;break;}opened++;if(opened>maximum)maximum=opened;}return f;
}
int fclose(FILE *f){
 int (*system_fclose)(FILE *)=dlsym(RTLD_NEXT,"fclose");
 int disk=backing(f);
 for(unsigned i=0;i<32;i++)if(files[i]==f){files[i]=NULL;opened--;break;}
 int result=system_fclose(f);if(disk && fail_close){fail_close=0;errno=EIO;return -1;}return result;
}
static FILE *stream(JgrScratchPool *p,const char *folder,unsigned i){
 char path[1024];snprintf(path,sizeof path,"%s/scratch-%02u.tmp",folder,i);return jgr_scratch_open(p,path);
}
static int cache_test(const char *folder){
 JgrScratchPool p;jgr_scratch_init(&p,0);FILE *f[6];unsigned char data[16384],out[16384];limit=4;
 for(unsigned i=0;i<6;i++){
  f[i]=stream(&p,folder,i);if(!f[i])return 20;
  for(unsigned k=0;k<sizeof data;k++)data[k]=(unsigned char)(i*17+k*3);
  if(fwrite(data,1,sizeof data,f[i])!=sizeof data||fflush(f[i]))return 21;
 }
 reads=seeks=0;read_bytes=0;
 for(unsigned round=0;round<100;round++)for(unsigned i=0;i<6;i++)for(unsigned page=0;page<2;page++){
  unsigned at=page*4096+((round*17)%3000);
  if(fseek(f[i],at,SEEK_SET)||fread(out,1,24,f[i])!=24)return 22;
  for(unsigned k=0;k<24;k++)if(out[k]!=(unsigned char)(i*17+(at+k)*3))return 23;
 }
 unsigned cached_reads=reads;size_t cached_bytes=read_bytes;
 if(cached_reads>16||cached_bytes>65536||!p.read_cache)return 24;
 /* Twenty-four distinct pages force eviction from the sixteen-page cache. */
 for(unsigned round=0;round<3;round++)for(unsigned page=0;page<4;page++)for(unsigned i=0;i<6;i++){
  unsigned at=page*4096+3100;
  if(fseek(f[i],at,SEEK_SET)||fread(out,1,24,f[i])!=24)return 35;
  for(unsigned k=0;k<24;k++)if(out[k]!=(unsigned char)(i*17+(at+k)*3))return 36;
 }
 /* Rewrites overlapping two cached pages must invalidate both. */
 if(fseek(f[0],4000,SEEK_SET)||fread(out,1,200,f[0])!=200||
    fseek(f[0],4090,SEEK_SET)||fwrite("abcdefghijklmnopqrst",1,20,f[0])!=20||fflush(f[0])||
    fseek(f[0],4000,SEEK_SET)||fread(out,1,200,f[0])!=200||memcmp(out+90,"abcdefghijklmnopqrst",20))return 25;
 /* A backing seek failure cannot be masked by an old cache page. */
 for(unsigned k=0;k<24;k++)data[k]=(unsigned char)((13000+k)*3);
 if(fseek(f[0],13000,SEEK_SET)||fwrite(data,1,24,f[0])!=24||fflush(f[0]))return 37;
 fail_seek=1;if(fseek(f[0],13000,SEEK_SET)||fread(out,1,24,f[0])!=0||!ferror(f[0])||errno!=EIO)return 26;
 clearerr(f[0]);if(fseek(f[0],13000,SEEK_SET)||fread(out,1,24,f[0])!=24)return 27;
 for(unsigned k=0;k<24;k++)if(out[k]!=(unsigned char)((13000+k)*3))return 28;
 if(jgr_scratch_reset(&p,f[0])||ftell(f[0])!=0||fread(out,1,1,f[0])!=0||!feof(f[0]))return 29;
 if(fseek(f[0],9000,SEEK_SET)||fwrite("reset",1,5,f[0])!=5||fflush(f[0])||fseek(f[0],0,SEEK_SET)||fread(out,1,9005,f[0])!=9005)return 30;
 for(unsigned k=0;k<9000;k++)if(out[k])return 31;
 if(memcmp(out+9000,"reset",5))return 32;
 for(unsigned i=0;i<6;i++)if(fclose(f[i]))return 33;
 if(p.read_cache||p.first_stream||p.streams||p.allocated||opened)return 34;
 printf("{\"reads\":%u,\"read_bytes\":%zu,\"open_max\":%u}\n",cached_reads,cached_bytes,maximum);return 0;
}
static int views_test(const char *folder){
 JgrScratchPool p;jgr_scratch_init(&p,32768);unsigned char data[24576];memset(data,53,sizeof data);size_t bytes=0;
 FILE *a=stream(&p,folder,0),*b=stream(&p,folder,1),*c=stream(&p,folder,2);if(!a||!b||!c)return 40;
 if(fwrite(data,1,sizeof data,a)!=sizeof data)return 41;
 const unsigned char *view=jgr_scratch_view(&p,a,&bytes);if(!view||bytes!=sizeof data)return 42;
 if(jgr_scratch_reset(&p,a)==0||errno!=EBUSY)return 43;
 if(jgr_scratch_discard(&p,a)==0||errno!=EBUSY)return 43;
 if(fwrite(data,1,8192,b)!=8192||fflush(b)||jgr_scratch_view(&p,b,&bytes))return 44;
 if(memcmp(view,data,sizeof data))return 45;
 jgr_scratch_unview(&p,a);
 /* Once unpinned, the large bank spills before this small indexed table. */
 if(fwrite(data,1,4096,c)!=4096||fflush(c)||p.allocated>4096)return 46;
 view=jgr_scratch_view(&p,c,&bytes);if(!view||bytes!=4096||memcmp(view,data,4096))return 47;
 jgr_scratch_unview(&p,c);
 if(jgr_scratch_view(&p,a,&bytes)||jgr_scratch_reset(&p,a)||jgr_scratch_reset(&p,c)||p.allocated)return 48;
 if(fclose(a)||fclose(b)||fclose(c)||p.read_cache||p.first_stream||p.streams||opened)return 49;
 printf("{\"spills\":%u,\"peak\":%zu}\n",p.spills,p.peak);return 0;
}
static int phase_test(const char *folder){
 if(jgr_scratch_setup_phase(0))return 50;
 JgrScratchPool *p=jgr_scratch_begin();if(!p||p->budget!=18u*1024u*1024u||p->headroom!=6u*1024u*1024u)return 51;
 if(jgr_scratch_setup_phase(3)==0||errno!=EBUSY||jgr_scratch_begin()!=p||jgr_scratch_end())return 52;
 FILE *f=stream(p,folder,0);if(!f||jgr_scratch_end()==0||errno!=EBUSY)return 53;
 if(jgr_scratch_setup_phase(3)==0||errno!=EBUSY||jgr_scratch_begin())return 54;
 if(fclose(f)||jgr_scratch_setup_phase(3))return 55;
 p=jgr_scratch_begin();if(!p||p->budget!=JGR_SCRATCH_DEFAULT_BUDGET||p->headroom||jgr_scratch_end())return 56;
 if(jgr_scratch_setup_phase(5)==0||errno!=EINVAL||jgr_scratch_setup_phase(4))return 57;
 p=jgr_scratch_begin();if(!p||p->budget!=18u*1024u*1024u||jgr_scratch_end())return 58;
 printf("{\"normal_budget\":%zu}\n",p->budget);return 0;
}
static int lazy_test(const char *folder){
 char target[1024],link[1024],directory[1024],owned[1024],fifo[1024];
 snprintf(target,sizeof target,"%s/victim",folder);snprintf(link,sizeof link,"%s/link",folder);
 snprintf(directory,sizeof directory,"%s/directory",folder);snprintf(owned,sizeof owned,"%s/owned",folder);snprintf(fifo,sizeof fifo,"%s/fifo",folder);
 FILE *f=fopen(target,"wb");if(!f||fwrite("keep",1,4,f)!=4||fclose(f)||symlink(target,link)||mkdir(directory,0700)||mkfifo(fifo,0600))return 60;
 unsigned old_checks=metadata_checks,old_removes=removes;JgrScratchPool p;jgr_scratch_init(&p,16384);
 f=jgr_scratch_open(&p,link);if(!f||fwrite("memory",1,6,f)!=6||jgr_scratch_discard(&p,f))return 61;
 if(metadata_checks!=old_checks||removes!=old_removes||p.spills||opened)return 62;
 /* Only the first real backing operation inspects the path. Symlinks,
    directories and FIFOs are rejected without opening or deleting them. */
 const char *bad[]={link,directory,fifo};
 for(unsigned i=0;i<3;i++){
  jgr_scratch_init(&p,0);f=jgr_scratch_open(&p,bad[i]);if(!f)return 63;
  if(fwrite("bad",1,3,f)!=3||fflush(f)==0||errno!=EINVAL)return 64;
  clearerr(f);if(jgr_scratch_discard(&p,f)==0||errno!=EINVAL||p.opens||opened||removes!=old_removes)return 65;
 }
 f=fopen(target,"rb");char value[4];if(!f||fread(value,1,4,f)!=4||memcmp(value,"keep",4)||fclose(f))return 66;
 /* Existing regular private spill files may be reused, and reset retains
    ownership even if the replacement stays completely in RAM. */
 f=fopen(owned,"wb");if(!f||fwrite("old",1,3,f)!=3||fclose(f))return 67;
 jgr_scratch_init(&p,0);f=jgr_scratch_open(&p,owned);
 if(!f||fwrite("new",1,3,f)!=3||fflush(f)||p.opens!=1||jgr_scratch_reset(&p,f))return 68;
 p.budget=8192;if(fwrite("ram",1,3,f)!=3||jgr_scratch_discard(&p,f)||access(owned,F_OK)==0)return 69;
 /* fclose's flush can perform the first spill; cleanup still owns it. */
 jgr_scratch_init(&p,0);f=jgr_scratch_open(&p,owned);if(!f||fwrite("buffered",1,8,f)!=8||p.spills)return 70;
 if(jgr_scratch_discard(&p,f)||access(owned,F_OK)==0||p.opens!=1)return 71;
 /* Preserve the first close error when deletion also fails. */
 f=jgr_scratch_open(&p,owned);if(!f||fwrite("failure",1,7,f)!=7||fflush(f))return 72;
 fail_close=fail_remove=1;if(jgr_scratch_discard(&p,f)==0||errno!=EIO||opened||p.streams)return 73;
 if(remove(owned))return 74;
 f=jgr_scratch_open(&p,owned);if(!f||fwrite("remove",1,6,f)!=6||fflush(f))return 75;
 fail_remove=1;if(jgr_scratch_discard(&p,f)==0||errno!=EACCES||opened||p.streams)return 76;
 if(remove(owned))return 77;
 printf("{\"ram_path_operations\":0,\"rejected_types\":3,\"close_and_remove_errors_preserved\":true}\n");return 0;
}
typedef struct {JgrScratchPool *pool;unsigned calls;size_t restore;} ReclaimCheck;
static void reclaim_check(void *context){
 ReclaimCheck *check=context;check->calls++;check->pool->budget+=check->restore;
 JgrScratchReclaim recursive={0};assert(jgr_scratch_reclaim_begin(check->pool,&recursive,reclaim_check,context));
 errno=EDOM;
}
static int reclaim_test(const char *folder){
 JgrScratchPool pool;jgr_scratch_init(&pool,0);JgrScratchReclaim outer={0},inner={0};
 ReclaimCheck a={&pool,0,0},b={&pool,0,16384};
 assert(jgr_scratch_reclaim_begin(NULL,&outer,reclaim_check,&a));
 assert(!jgr_scratch_reclaim_begin(&pool,&outer,reclaim_check,&a));
 assert(jgr_scratch_reclaim_begin(&pool,&outer,reclaim_check,&a));
 assert(!jgr_scratch_reclaim_begin(&pool,&inner,reclaim_check,&b));
 assert(jgr_scratch_reclaim_end(&outer)&&errno==EBUSY);
 FILE *f=stream(&pool,folder,0);assert(f);assert(!setvbuf(f,NULL,_IONBF,0));
 unsigned char data[12000],out[12000];memset(data,93,sizeof data);errno=ERANGE;
 assert(fwrite(data,1,sizeof data,f)==sizeof data&&!fflush(f));assert(errno==ERANGE);
 assert(!inner.pool&&pool.reclaim==&outer&&b.calls==1&&!a.calls&&!pool.spills&&!pool.opens);
 rewind(f);assert(fread(out,1,sizeof out,f)==sizeof out&&!memcmp(data,out,sizeof out));
 assert(!jgr_scratch_reclaim_end(&outer)&&!pool.reclaim);
 assert(!jgr_scratch_discard(&pool,f));
 /* A consumed no-op callback runs once, then the ordinary spill remains. */
 jgr_scratch_init(&pool,0);a.calls=0;
 assert(!jgr_scratch_reclaim_begin(&pool,&outer,reclaim_check,&a));
 f=stream(&pool,folder,0);assert(f);
 assert(fwrite(data,1,sizeof data,f)==sizeof data&&!fflush(f));
 assert(a.calls==1&&!outer.pool&&!pool.reclaim&&pool.spills==1);
 assert(!jgr_scratch_discard(&pool,f));
 printf("{\"callback_before_spill\":true,\"nested_scope_restored\":true}\n");return 0;
}
static int partial_write_test(const char *folder){
 JgrScratchPool pool;unsigned char data[8192],out[4096];memset(data,71,sizeof data);
 jgr_scratch_init(&pool,8192);FILE *a=stream(&pool,folder,0),*b=stream(&pool,folder,1);assert(a&&b);
 assert(!setvbuf(a,NULL,_IONBF,0)&&!setvbuf(b,NULL,_IONBF,0));
 assert(fwrite(data,1,4096,a)==4096);fail_write=1;
 assert(fwrite(data,1,sizeof data,b)!=sizeof data&&ferror(b)&&errno==EIO);
 /* A partial spill must not release or replace the only complete RAM copy. */
 size_t bytes=0;const void *view=jgr_scratch_view(&pool,a,&bytes);
 assert(view&&bytes==4096&&!memcmp(view,data,4096)&&pool.allocated==4096&&!pool.spills);
 jgr_scratch_unview(&pool,a);clearerr(b);
 assert(!fseek(a,0,SEEK_SET)&&fread(out,1,sizeof out,a)==sizeof out&&!memcmp(out,data,sizeof out));
 assert(!jgr_scratch_discard(&pool,b)&&!jgr_scratch_discard(&pool,a));
 char path[1024];snprintf(path,sizeof path,"%s/scratch-00.tmp",folder);assert(access(path,F_OK)&&errno==ENOENT);
 assert(!pool.streams&&!pool.allocated&&!pool.first_stream&&!pool.read_cache&&!opened);
 /* An existing disk stream may contain a partial append after failure; its
  * unpublished backing file must still be discarded completely. */
 jgr_scratch_init(&pool,0);a=stream(&pool,folder,0);assert(a&&!setvbuf(a,NULL,_IONBF,0));
 assert(fwrite(data,1,4096,a)==4096);fail_write=1;
 assert(fwrite(data,1,1024,a)!=1024&&ferror(a)&&errno==EIO);clearerr(a);
 assert(!jgr_scratch_discard(&pool,a)&&access(path,F_OK)&&errno==ENOENT);
 assert(!pool.streams&&!pool.allocated&&!pool.first_stream&&!pool.read_cache&&!opened);
 printf("{\"failed_spill_keeps_ram\":true,\"partial_backing_removed\":true}\n");return 0;
}
int main(int argc,char **argv){
 if(argc==3){if(!strcmp(argv[1],"partial_write"))return partial_write_test(argv[2]);if(!strcmp(argv[1],"reclaim"))return reclaim_test(argv[2]);if(!strcmp(argv[1],"lazy"))return lazy_test(argv[2]);if(!strcmp(argv[1],"cache"))return cache_test(argv[2]);if(!strcmp(argv[1],"views"))return views_test(argv[2]);if(!strcmp(argv[1],"phase"))return phase_test(argv[2]);return 1;}
 if(argc!=4)return 2;unsigned budget=strtoul(argv[1],NULL,0);limit=strtoul(argv[2],NULL,0);
 JgrScratchPool pool;jgr_scratch_init(&pool,budget);FILE *streams[13]={0};char path[1024];unsigned char data[700],expected[45117],readback[45117];
 for(unsigned i=0;i<13;i++){snprintf(path,sizeof path,"%s/scratch-%02u.tmp",argv[3],i);streams[i]=jgr_scratch_open(&pool,path);if(!streams[i])return 3;}
 for(unsigned row=0;row<64;row++)for(unsigned i=0;i<13;i++){
  for(unsigned k=0;k<700;k++)data[k]=(unsigned char)(i*19+row*23+k*17);
  if(fwrite(data,1,700,streams[i])!=700)return 4;
 }
 for(unsigned i=0;i<13;i++){
  FILE *f=streams[i];if(fflush(f)||ftell(f)!=44800||fseek(f,1000,SEEK_SET)||fwrite("replacement",1,11,f)!=11||fseek(f,45100,SEEK_SET)||fwrite("ending after hole",1,17,f)!=17||fflush(f)||fseek(f,0,SEEK_END)||ftell(f)!=45117||fseek(f,-17,SEEK_END)||fread(data,1,17,f)!=17||memcmp(data,"ending after hole",17)||fseek(f,0,SEEK_SET)||fread(readback,1,sizeof readback,f)!=sizeof readback)return 5;
  for(unsigned row=0;row<64;row++)for(unsigned k=0;k<700;k++)expected[row*700+k]=(unsigned char)(i*19+row*23+k*17);
  memcpy(expected+1000,"replacement",11);memset(expected+44800,0,300);memcpy(expected+45100,"ending after hole",17);
  if(memcmp(readback,expected,sizeof expected)||fread(data,1,1,f)!=0||!feof(f))return 6;
  if(fseek(f,-1,SEEK_SET)==0)return 7;
  clearerr(f);
 }
 for(unsigned i=0;i<13;i++)if(fclose(streams[i]))return 8;
 if(pool.allocated||opened||pool.peak>budget||pool.read_cache||pool.first_stream||pool.streams)return 9;
 for(unsigned i=0;i<JGR_SCRATCH_DISK_FILES;i++)if(pool.file[i]||pool.owner[i])return 10;
 printf("{\"budget\":%u,\"peak\":%zu,\"spills\":%u,\"open_max\":%u,\"open_calls\":%u}\n",budget,pool.peak,pool.spills,maximum,pool.opens);return 0;
}
'''


class Scratch(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='jgr-scratch-test-')
        cls.root = Path(cls.temp.name)
        (cls.root/'driver.c').write_text(DRIVER)
        cls.driver = cls.root/'driver'
        subprocess.run(['cc', '-std=gnu99', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        '-I', str(ROOT/'psp/src'), str(cls.root/'driver.c'),
                        str(ROOT/'psp/src/portable_scratch.c'), '-o', str(cls.driver)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def exercise(self, budget, limit=4):
        folder = self.root/f"{budget}-{limit}"
        folder.mkdir()
        run = subprocess.run([self.driver, str(budget), str(limit), folder], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        report = json.loads(run.stdout)
        self.assertLessEqual(report['open_max'], min(limit,16))
        self.assertLessEqual(report['peak'], budget)
        return report

    def test_optional_residency_reclaimed_before_spill_with_nested_lifetime(self):
        report = self.mode('reclaim')
        self.assertTrue(report['callback_before_spill'])
        self.assertTrue(report['nested_scope_restored'])

    def test_partial_backing_writes_keep_source_and_cleanup_unpublished_files(self):
        report = self.mode('partial_write')
        self.assertTrue(report['failed_spill_keeps_ram'])
        self.assertTrue(report['partial_backing_removed'])

    def test_memory_only_streams_need_no_disk_handles(self):
        result = self.exercise(1024*1024)
        self.assertEqual((result['spills'], result['open_max']), (0, 0))

    def test_mixed_streams_stay_within_shared_memory_budget(self):
        result = self.exercise(32768)
        self.assertGreater(result['spills'], 0)

    def test_all_spilled_streams_reopen_under_four_file_limit(self):
        result = self.exercise(0)
        self.assertEqual((result['spills'], result['open_max']), (13, 4))

    def test_all_thirteen_spills_keep_handles_when_available(self):
        result=self.exercise(0,16)
        self.assertEqual((result['spills'],result['open_max'],result['open_calls']),(13,13,13))

    def test_lazy_validation_only_owned_cleanup_and_failure_lifetime(self):
        report=self.mode('lazy')
        self.assertEqual(report['ram_path_operations'],0)
        self.assertEqual(report['rejected_types'],3)
        self.assertTrue(report['close_and_remove_errors_preserved'])

    def mode(self, name):
        folder = self.root/name
        folder.mkdir()
        run = subprocess.run([self.driver, name, folder], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, f'{name}: exit{run.returncode} {run.stderr}')
        return json.loads(run.stdout)

    def test_cached_indexed_reads_rewrites_reopens_errors_and_reset(self):
        result = self.mode('cache')
        self.assertLessEqual(result['reads'], 16)
        self.assertLessEqual(result['read_bytes'], 65536)
        self.assertLessEqual(result['open_max'], 4)

    def test_borrowed_view_pins_bank_and_largest_spill_protects_small_tables(self):
        result = self.mode('views')
        self.assertEqual(result['spills'], 2)
        self.assertLessEqual(result['peak'], 32768)

    def test_phase_policy_rejects_live_scopes_and_returns_small_replay_budget(self):
        self.assertEqual(self.mode('phase')['normal_budget'], 18*1024*1024)


if __name__ == '__main__':
    unittest.main()
