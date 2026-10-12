/* Authored protocol/corruption tests; no original data or observations. */
#include "../../../psp/src/portable_replay_bootstrap.c"
#include <assert.h>
static unsigned checks;
static int sample_ok(void*u,const unsigned char*a,const unsigned char*c,const unsigned char*i,const unsigned char*p,unsigned part,unsigned cut,unsigned variant)
{(void)u;(void)a;(void)c;(void)i;(void)p;(void)part;(void)cut;(void)variant;return 1;}
static FILE* file_bytes(const unsigned char*p,size_t n)
{FILE*f=tmpfile();assert(f);assert(fwrite(p,1,n,f)==n);rewind(f);return f;}
static int decode(const unsigned char*p,size_t n)
{
 struct StaffReuse*s=calloc(1,sizeof *s);assert(s);s->stream=file_bytes(p,n);s->crc=UINT32_MAX;
 unsigned kind=0;int result=0;
 for(unsigned k=0;k<32;k++){
  if(!staff_event_read(s,&kind))break;
  if(!kind){result=1;break;}
  if(kind==6)s->parts++;
  if(kind==5)s->samples++;
 }
 fclose(s->stream);free(s);return result;
}
int main(void)
{
 Devices*d=calloc(1,sizeof *d);assert(d);FILE*f=tmpfile();assert(f);
 assert(!portable_replay_bootstrap_staff_record(d,f));checks++;
 d->active=1;d->selection.kind=PORTABLE_REPLAY_STAFF;
 assert(portable_replay_bootstrap_staff_record(d,f));
 assert(!portable_replay_bootstrap_staff_record(d,f));checks++;
 unsigned char state[0xc60]={0};
 for(unsigned k=0;k<9;k++){
  staff_put32(state+0x108,k*17+3);
  assert(staff_event_write(d->staff_reuse,6,state,0x900));d->staff_reuse->parts++;
 }
 for(unsigned k=0;k<2;k++){
  staff_put32(state+0x908,k+7);
  assert(staff_event_write(d->staff_reuse,5,state,sizeof state));d->staff_reuse->samples++;
 }
 assert(!portable_replay_bootstrap_staff_record_finish(d));checks++;
 d->complete=1;assert(portable_replay_bootstrap_staff_record_finish(d));assert(!d->staff_reuse);checks++;
 long bytes=ftell(f);assert(bytes>40 && bytes<4096);unsigned char raw[4096];rewind(f);
 assert(fread(raw,1,(size_t)bytes,f)==(size_t)bytes);fclose(f);
 assert(decode(raw+16,(size_t)bytes-16));checks++;
 for(long n=0;n<bytes-16;n++)assert(!decode(raw+16,(size_t)n));checks++;
 unsigned char altered[4096];memcpy(altered,raw,(size_t)bytes);altered[22]^=1;
 assert(!decode(altered+16,(size_t)bytes-16));checks++;
 memcpy(altered,raw,(size_t)bytes);altered[bytes-1]^=1;
 assert(!decode(altered+16,(size_t)bytes-16));checks++;
 memcpy(altered,raw,(size_t)bytes);altered[bytes]=0;
 assert(!decode(altered+16,(size_t)bytes-15));checks++;
 unsigned char malformed[32]={7,0,0,0};assert(!decode(malformed,4));checks++;
 malformed[0]=1;staff_put16(malformed+2,793);assert(!decode(malformed,4));checks++;
 staff_put16(malformed+2,1);staff_put16(malformed+4,576);assert(!decode(malformed,10));checks++;
 staff_put16(malformed+2,2);staff_put16(malformed+4,3);staff_put16(malformed+10,3);
 assert(!decode(malformed,16));checks++;
 staff_put16(malformed+4,4);staff_put16(malformed+10,3);assert(!decode(malformed,16));checks++;
 for(unsigned field=0;field<4;field++){
  memcpy(altered,raw,16);altered[field*4]^=0x40;f=file_bytes(altered,16);
  assert(!portable_replay_bootstrap_staff_poses(d,f,0,sample_ok,NULL));fclose(f);
 }checks++;
 /* An exhausted event bound rejects even an otherwise well-formed next item. */
 struct StaffReuse*s=calloc(1,sizeof *s);assert(s);s->events=STAFF_INPUT_EVENTS;s->stream=file_bytes(raw+16,(size_t)bytes-16);
 unsigned kind;assert(!staff_event_read(s,&kind));fclose(s->stream);free(s);checks++;
 free(d);printf("%u authored STAFF reuse protocol checks passed\n",checks);return 0;
}
