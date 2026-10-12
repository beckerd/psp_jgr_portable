#include "portable_assets.h"
#include <string.h>
#include <errno.h>
/* These tests exercise story save I/O, not mission/scene selection. Supply
   empty external storage rather than embedding the original game tables. */
int portable_test_story_z[17] __asm__("jgr_data_mixed_story_c__z_0_z");
int portable_test_story_tbl[12] __asm__("jgr_data_mixed_story_c__tbl_0_tbl");
signed char portable_test_story_scenes[15] __asm__("jgr_data_mixed_story_c__scenes_0_scenes");
#ifdef __PSP__
#include <pspiofilemgr.h>
int portable_test_sce_read(const char *name)
{
    int fd=sceIoOpen(name,PSP_O_RDONLY,0777);if(fd<0)return -1;
    unsigned char value=0;int n=sceIoRead(fd,&value,1);sceIoClose(fd);
    return n==1 ? value:-1;
}
int portable_test_sce_write_open(const char *name,int flags)
{int fd=sceIoOpen(name,flags,0777);if(fd>=0)sceIoClose(fd);return fd;}
int portable_test_sce_stat(const char *name)
{SceIoStat value;return sceIoGetstat(name,&value);}
int portable_test_sce_remove(const char *name){return sceIoRemove(name);}
int portable_test_sce_rename(const char *a,const char *b){return sceIoRename(a,b);}
#endif

int portable_test_read(const char *name)
{
    FILE *f=fopen(name,"rb");if(!f)return -1;
    int value=fgetc(f);fclose(f);return value;
}
int portable_test_write(const char *name,const char *text)
{
    FILE *f=fopen(name,"wb");if(!f)return -1;
    int failed=fwrite(text,1,strlen(text),f)!=strlen(text);
    if(fclose(f))failed=1;
    return failed ? -1:0;
}
int portable_test_exists(const char *name)
{struct stat value;return stat(name,&value);}
int portable_test_remove(const char *name){return remove(name);}
int portable_test_rename(const char *from,const char *to){return rename(from,to);}
