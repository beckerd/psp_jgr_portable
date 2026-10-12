#ifndef JGR_TEST_PSPKERNEL_H
#define JGR_TEST_PSPKERNEL_H
#include <fcntl.h>
#include <unistd.h>
typedef int SceUID;
typedef off_t SceOff;
#define PSP_O_RDONLY O_RDONLY
#define PSP_SEEK_END SEEK_END
#define PSP_SEEK_SET SEEK_SET
#define sceIoOpen open
#define sceIoLseek lseek
#define sceIoRead read
#define sceIoClose close

static inline unsigned sceKernelGetSystemTimeLow(void){return 0;}
#endif
