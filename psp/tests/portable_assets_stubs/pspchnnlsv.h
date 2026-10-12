#ifndef JGR_TEST_PSPCHNNLSV_H
#define JGR_TEST_PSPCHNNLSV_H
#include <stdint.h>
typedef struct {
    int mode;
    unsigned char data[16],key[16];
    uint32_t size;
} SceSdContext1;
int sceSdSetIndex(SceSdContext1 *,int);
int sceSdRemoveValue(SceSdContext1 *,unsigned char *,int);
int sceSdGetLastIndex(SceSdContext1 *,unsigned char *,unsigned char *);
#endif
