/* PLAYDEMO attract/title demos ('PDM1' from tools/jgs_playdemo.py): per skater an input stream + a camera timeline */
#ifndef JGR_DEMO_H
#define JGR_DEMO_H
#include "ctrl.h"
typedef struct { unsigned tick, kind, code; float pos[3], dir[3]; int roll, ang; float a, b; } DemoEv;   /* code 0x20 fixed world camera, 0x2a offset around the skater */
typedef struct { CtrlStream s; int nev; const DemoEv *ev; } DemoSkater;
typedef struct { unsigned char *data; int n; DemoSkater sk[4]; } Demo;
int  demo_load(Demo *d, const char *path);
void demo_free(Demo *d);
#endif
