#ifndef PORTABLE_TEST_IO_H
#define PORTABLE_TEST_IO_H
typedef int SceUID;
typedef int SceMode;
typedef struct SceIoStat { unsigned size; } SceIoStat;
enum { PSP_O_RDONLY=1,PSP_O_WRONLY=2,PSP_O_RDWR=3,
       PSP_O_APPEND=0x100,PSP_O_CREAT=0x200,PSP_O_TRUNC=0x400 };
SceUID sceIoOpen(const char *,int,SceMode);
int sceIoGetstat(const char *,SceIoStat *);
int sceIoRemove(const char *);
int sceIoRename(const char *,const char *);
int sceIoRead(int,void *,unsigned);
int sceIoClose(int);
#endif
