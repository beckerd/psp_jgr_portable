"""Replay publication: exact chunk boundaries, optional memory, and I/O failures."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
DRIVER = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static unsigned allocation_fail,buffer_fail,read_fail,write_fail,read_error;
static unsigned allocations,releases,reads,writes,buffer_calls,max_read;
static void *transfer_malloc(size_t n)
{assert(n==128u*1024u);allocations++;return allocation_fail?NULL:malloc(n);}
static void transfer_free(void *p){if(p)releases++;free(p);}
static int transfer_setvbuf(FILE *f,char *p,int mode,size_t n)
{buffer_calls++;assert(!p&&mode==_IONBF&&!n);return buffer_fail?-1:setvbuf(f,p,mode,n);}
static size_t transfer_read(void *p,size_t a,size_t n,FILE *f)
{assert(a==1);reads++;if(n>max_read)max_read=(unsigned)n;
 if(read_fail&&reads==read_fail){read_error=1;return 0;}return fread(p,a,n,f);}
static size_t transfer_write(const void *p,size_t a,size_t n,FILE *f)
{assert(a==1);writes++;if(write_fail&&writes==write_fail)return n-1;return fwrite(p,a,n,f);}
static int transfer_error(FILE *f){return read_error||ferror(f);}
#define malloc transfer_malloc
#define free transfer_free
#define setvbuf transfer_setvbuf
#define fread transfer_read
#define fwrite transfer_write
#define ferror transfer_error
REPLAY_COPY_FUNCTION
#undef malloc
#undef free
#undef setvbuf
#undef fread
#undef fwrite
#undef ferror
static void check(unsigned size,unsigned mode,unsigned fail_read,unsigned fail_write)
{
 allocation_fail=mode==1;buffer_fail=mode==2;read_fail=fail_read;write_fail=fail_write;
 read_error=allocations=releases=reads=writes=buffer_calls=max_read=0;
 FILE *from=tmpfile(),*to=tmpfile();assert(from&&to);
 for(unsigned i=0;i<size;i++)assert(fputc((i*37u+(i>>9))&255,from)!=EOF);
 assert(!fflush(from));
 /* Reopen before changing buffering, as the production reader does. */
  int descriptor=dup(fileno(from));assert(descriptor>=0);assert(!fclose(from));
 assert(lseek(descriptor,0,SEEK_SET)==0);from=fdopen(descriptor,"rb");assert(from);
 int result=copy_output(from,to);assert(result==((fail_read||fail_write)?-1:0));
 assert(allocations==1&&releases==(mode!=1)&&buffer_calls==(mode!=1));
 assert(max_read==(mode?4096u:128u*1024u));
 if(!fail_read&&!fail_write){
  assert(!fflush(to)&&ftell(to)==(long)size);rewind(to);
  for(unsigned i=0;i<size;i++)assert(fgetc(to)==(int)((i*37u+(i>>9))&255));
  assert(fgetc(to)==EOF&&!ferror(to));
  unsigned chunks=(size+(mode?4095u:131071u))/(mode?4096u:131072u);
  assert(writes==chunks&&reads==chunks+1);
 }
 assert(!fclose(from)&&!fclose(to));
}
int main(int argc,char **argv)
{
 assert(argc==2);unsigned failure=argv[1][0]=='f';
 const unsigned sizes[]={0,1,4095,4096,4097,131071,131072,131073,3u*131072u+123u};
 if(!failure)for(unsigned m=0;m<3;m++)for(unsigned i=0;i<sizeof sizes/sizeof *sizes;i++)check(sizes[i],m,0,0);
 else for(unsigned m=0;m<3;m++){
  check(3u*131072u+123u,m,1,0);check(3u*131072u+123u,m,2,0);
  check(3u*131072u+123u,m,0,1);check(3u*131072u+123u,m,0,2);
 }
 return 0;
}
'''


class ReplayCopy(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='jgr-replay-copy-')
        root = Path(cls.temp.name)
        source = (ROOT/'psp/src/portable_replay_session.c').read_text()
        function = source[source.index('static int copy_output('):source.index('static int generate_output(')]
        (root/'probe.c').write_text(DRIVER.replace('REPLAY_COPY_FUNCTION', function))
        cls.exe = root/'probe'
        subprocess.run(['cc', '-std=gnu99', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', str(root/'probe.c'), '-o', str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_exact_boundaries_and_allocation_or_buffer_failure_fallback(self):
        subprocess.run([str(self.exe), 'exact'], check=True)

    def test_read_and_short_write_errors_release_optional_memory(self):
        subprocess.run([str(self.exe), 'failure'], check=True)


if __name__ == '__main__':
    unittest.main()
