#ifndef PORTABLE_TEST_DEBUG_H
#define PORTABLE_TEST_DEBUG_H
void pspDebugScreenInit(void);
void pspDebugScreenEnableBackColor(int);
void pspDebugScreenSetTextColor(unsigned);
void pspDebugScreenSetXY(int,int);
void pspDebugScreenClear(void);
int pspDebugScreenPrintf(const char *,...);
#endif
