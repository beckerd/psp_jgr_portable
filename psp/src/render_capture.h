#ifndef JGR_RENDER_CAPTURE_H
#define JGR_RENDER_CAPTURE_H
#include <string.h>

/* Memory-stick drivers receive bounded ordinary-RAM buffers, never EDRAM.
   The caller owns fixed scratch storage; no heap or frame-sized stack copy. */
#define JGR_CAPTURE_CHUNK_BYTES 4096u
typedef int (*JgrCaptureWriter)(int,const void *,unsigned);
static int jgr_capture_write(int fd,const void *source,unsigned bytes,
                            void *scratch,unsigned capacity,JgrCaptureWriter writer)
{
    if(!source || !scratch || !capacity || !writer || bytes>0x7fffffffu)return -1;
    if(capacity>JGR_CAPTURE_CHUNK_BYTES)capacity=JGR_CAPTURE_CHUNK_BYTES;
    const unsigned char *pixels=source;
    unsigned done=0;
    while(done<bytes) {
        unsigned count=bytes-done;if(count>capacity)count=capacity;
        memcpy(scratch,pixels+done,count);
        unsigned sent=0;
        while(sent<count) {
            int written=writer(fd,(const unsigned char *)scratch+sent,count-sent);
            if(written<0)return written;
            if(!written)return -2;
            if((unsigned)written>count-sent)return -3;
            sent+=(unsigned)written;
        }
        done+=count;
    }
    return (int)done;
}
#endif
