#include <stdio.h>
#include <pspkernel.h>
#include <pspgu.h>
#include <pspgum.h>
#include <malloc.h>
#include <string.h>
#include <math.h>
#include "graffiti.h"

static int finite_words(const void *raw,unsigned count)
{const unsigned char *p=raw;for(unsigned i=0;i<count;i++){float f;memcpy(&f,p+i*4,4);if(!isfinite(f))return 0;}return 1;}
/* 0c4ab4 transforms the static-city GF through an identity parent. Values
   are unchanged except signed zeros: FTRV's zero terms produce +0. Keep
   the raw descriptor TRS intact; it builds the separate GF+c0 basis. */
static void identity_points(void *raw,unsigned count)
{unsigned char *p=raw;for(unsigned i=0;i<count;i++){float f;memcpy(&f,p+i*4,4);if(f==0.f){f=0.f;memcpy(p+i*4,&f,4);}}}
static void free_spots(Graffiti *g)
{if(g->spots){for(int i=0;i<g->n;i++)free(g->spots[i].source);free(g->spots);}g->spots=NULL;g->n=0;}

int graffiti_load(Graffiti *g, const char *spots_path, const char *tags_path)
{
    memset(g, 0, sizeof *g); g->near = -1;
    SceUID fd = sceIoOpen(spots_path, PSP_O_RDONLY, 0777); if (fd < 0) return -1;
    char hdr[8]; int n;
    if (sceIoRead(fd,hdr,8)!=8) { sceIoClose(fd); return -2; }
    memcpy(&n,hdr+4,4);
    if (memcmp(hdr, "SPT6", 4) || n <= 0 || n > 4096) { sceIoClose(fd); return -2; }
    g->spots = memalign(16, n * sizeof(Spot)); g->n = 0;
    if (!g->spots) { sceIoClose(fd); return -3; }
    for (int i = 0; i < n; i++) {
        Spot *s = &g->spots[g->n]; memset(s, 0, sizeof *s);
        s->active = 1; s->area = 0; s->idx = i;
        { unsigned ai[2]; float v[40];
            if (sceIoRead(fd,ai,8)!=8 || sceIoRead(fd,v,sizeof v)!=sizeof v ||
                ai[0]>=6 || ai[1]>4095 || !(v[0]>=1 && v[0]<=3) || !finite_words(v,40)) goto corrupt;
            s->area = ai[0]; s->idx = ai[1]; s->size = (int)v[0]; s->x = v[1]; s->y = v[2]; s->z = v[3]; memcpy(s->pts, v + 4, 72); memcpy(s->nrm, v + 22, 72); s->hasvol = 1;
            {
                unsigned extra[2]; if(sceIoRead(fd,extra,8)!=8 || extra[1]>7) goto corrupt;
                s->source_data=1;s->style_mask=extra[0];s->nstrokes=extra[1];
                unsigned bytes=s->nstrokes*sizeof(SprayAnchor);
                if (bytes && sceIoRead(fd,s->anchors,bytes)!=bytes) goto corrupt;
                for(unsigned j=0;j<s->nstrokes;j++)if(!finite_words(s->anchors[j].position,3))goto corrupt;
            }
            {
                SpotSource source;
                if(sceIoRead(fd,&source,sizeof source)!=sizeof source || source.nvolume>5 ||
                   !finite_words(source.origin,3) || !finite_words(source.scale,15))goto corrupt;
                unsigned bytes=source.nvolume*176;
                SpotSource *data=malloc(sizeof source+bytes);if(!data)goto corrupt;
                memcpy(data,&source,sizeof source);
                if(bytes && sceIoRead(fd,data->volumes,bytes)!=bytes){free(data);goto corrupt;}
                int valid=1;
                for(unsigned j=0;j<data->nvolume;j++) {
                    const unsigned char *v=data->volumes+j*176;int32_t fov;float seconds;
                    memcpy(&fov,v+168,4);memcpy(&seconds,v+172,4);
                    if(!finite_words(v,42) || !isfinite(seconds) || seconds<=0 || fov<=0 || fov>=32768)valid=0;
                }
                if(!valid){free(data);goto corrupt;}
                identity_points(data->center,6);identity_points(s->pts,18);identity_points(s->nrm,18);
                for(unsigned j=0;j<s->nstrokes;j++)identity_points(s->anchors[j].position,3);
                for(unsigned j=0;j<data->nvolume;j++)identity_points(data->volumes+j*176,39);
                s->source=data;memcpy(s->facing,data->facing,12);
                /* A target with NO subvolumes still has the original
                   style-camera branch and must run the spray actor. */
                s->camera_valid=1;
            }
        }
        s->tex = g->n % 8; g->n++;
    }
    sceIoClose(fd);
    (void)tags_path; /* CityTags owns the original selected art and wall meshes. */
    return 0;
corrupt:
    sceIoClose(fd);free_spots(g);return -2;
}

void graffiti_free(Graffiti *g) { graffiti_can_drops_free(g); free_spots(g); if (g->cans) free(g->cans); if (g->have_tags) stage_free(&g->tags); memset(g, 0, sizeof *g); }
