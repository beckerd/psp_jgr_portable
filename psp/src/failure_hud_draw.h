#ifndef JGR_FAILURE_HUD_DRAW_H
#define JGR_FAILURE_HUD_DRAW_H
#include "charpose.h"
#include "scene.h"
#include "failure_hud.h"
#include "recruit_result.h"
#define FAILURE_MODELS 16
typedef struct {Chars models;unsigned char *image;Scene source;unsigned motion[FAILURE_MODELS][2];} FailureModels;
int failure_models_load(FailureModels *,const char *,const char *);
void failure_models_free(FailureModels *);
int failure_models_pose(const FailureModels *,int,int,float,CharPose[64]);
void failure_hud_draw(FailureModels *,const FailureHud *,int kind,int mode,const float[16],const float[16]);
void result_hud_draw(FailureModels *,const RecruitResult *,int kind,int rank,int souls,const float[16],const float[16]);
#endif
