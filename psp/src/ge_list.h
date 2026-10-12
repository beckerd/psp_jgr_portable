#ifndef JGR_GE_LIST_H
#define JGR_GE_LIST_H
/* Installed PSPSDK guInternal.h list prefix, verified against Start,
   CheckList, BoneMatrix and SetMatrix disassembly. */
typedef struct {unsigned *start,*current;int parent_context;} JgrGuList;
extern JgrGuList *gu_list;
#endif
