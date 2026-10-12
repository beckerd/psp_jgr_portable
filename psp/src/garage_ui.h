#ifndef JGR_GARAGE_UI_H
#define JGR_GARAGE_UI_H
#include "tex2d.h"
#include "grind_map.h"
/* These small layered models sit near z=-500. The world 1..32768 range
   quantizes their separate coloured faces to the same PSP depth value. */
#define GARAGE_MARKER_NEAR 100.f
#define GARAGE_MARKER_FAR 1000.f
int garage_ui_neighbor(const unsigned char *save,int selection,int direction);
const float *garage_ui_stats(int resident);
const char *garage_ui_mission(int entry);
const char *garage_ui_chapter(int chapter);
const char *garage_ui_menu_prompt(int menu);
int garage_ui_marker(int entry,int selected,float root[16]); /* source placement/scale */
void garage_ui_marker_at(float x,float y,int selected,float root[16]);
void garage_ui_selection(const Tex2dSet *,const unsigned char *save,int selection,int special,int stats);
void garage_ui_street(const Tex2dSet *,int entry);
void garage_ui_grind(const Tex2dSet *,const GrindMap *,int entry);
void garage_ui_menu(const Tex2dSet *,int menu);
#endif
