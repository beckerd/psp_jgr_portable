#ifndef JGR_TEST_MODULEMGR_H
#define JGR_TEST_MODULEMGR_H
#include <stddef.h>
#define PSP_MEMORY_PARTITION_KERNEL 1
typedef struct {
    size_t size;
    int mpidtext,mpiddata;
    unsigned flags;
    char position,access,creserved[2];
} SceKernelLMOption;
int sceKernelLoadModule(const char *,int,SceKernelLMOption *);
int sceKernelStartModule(int,unsigned,void *,int *,void *);
#endif
