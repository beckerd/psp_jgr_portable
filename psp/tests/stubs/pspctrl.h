#ifndef TEST_PSPCTRL_H
#define TEST_PSPCTRL_H
/* SDK numeric constants; host shim only, not a movement implementation. */
enum { PSP_CTRL_SELECT=0x1, PSP_CTRL_UP=0x10, PSP_CTRL_RIGHT=0x20, PSP_CTRL_DOWN=0x40,
       PSP_CTRL_LEFT=0x80, PSP_CTRL_LTRIGGER=0x100, PSP_CTRL_RTRIGGER=0x200,
       PSP_CTRL_CROSS=0x4000 };
#ifndef PSP_CTRL_TRIANGLE
#define PSP_CTRL_TRIANGLE 0x1000
#endif
#ifndef PSP_CTRL_CIRCLE
#define PSP_CTRL_CIRCLE 0x2000
#endif
#ifndef PSP_CTRL_SQUARE
#define PSP_CTRL_SQUARE 0x8000
#endif
#ifndef PSP_CTRL_START
#define PSP_CTRL_START 0x8
#endif
typedef struct { unsigned TimeStamp, Buttons; unsigned char Lx,Ly,Rsrv[6]; } SceCtrlData;
#endif
