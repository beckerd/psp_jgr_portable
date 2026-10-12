/* Native setup-time model parsing and deterministic mesh assembly. */
#include "portable_models.h"
#include "portable_crc32.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#ifdef __PSP__
#include "portable_scratch.h"
#endif

#define MAX_VERTICES 65536u
#define MAX_REFERENCES 1048576u
#define MAX_GROUPS 4096u
#define HASH_SIZE 131072u
#ifndef JGR_MODEL_SNAPSHOT_LIMIT
#define JGR_MODEL_SNAPSHOT_LIMIT (12u*1024u*1024u)
#endif
#define PI 3.14159265358979323846

typedef struct {
    double p[3],n[3],wp[3],wn[3];
    uint32_t color,generation;
    int owner;
    unsigned flags;
} Vertex;
struct JgrModelCache {Vertex *vertices;unsigned capacity;uint32_t generation;};
typedef struct {
    JgrDisc *disc;const JgrDiscFile *file;uint32_t base,window_start;
    unsigned window_size;unsigned char window[4096];
} Reader;
typedef struct {unsigned texture,flags,corners;} Group;
typedef struct {unsigned index;int u,v;unsigned divisor;} Corner;
typedef struct {
    double u,v,p[3],n[3];uint32_t color,diffuse;
    int qu,qv,owner;unsigned original;
} MeshVertex;
typedef struct {
    JgrModelCache *cache;const JgrModelOptions *options;
    Group *groups;unsigned group_count,selected;
    MeshVertex *vertices;unsigned nv,vcap;
    uint32_t *indices;unsigned ni,icap;
    uint32_t *hash;unsigned hash_mask,hash_capacity;int collect,position_identity;
} Assembly;

/* Lookup storage is reused, but the active table follows each material's
 * size. A large earlier material must not force large clears on later ones. */
static int assembly_hash_reserve(Assembly *a,unsigned slots)
{
    if(slots<128 || slots>HASH_SIZE || (slots&(slots-1)))return -1;
    if(slots<=a->hash_capacity)return 0;
    /* Existing keys are no longer needed: midpoint growth reconstructs them
     * from its compact edge records. Free first to bound peak PSP memory. */
    free(a->hash);a->hash=NULL;a->hash_capacity=0;
    a->hash=malloc(slots*sizeof *a->hash);if(!a->hash)return -1;
    a->hash_capacity=slots;return 0;
}

static uint32_t u32(const unsigned char *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static unsigned u16(const unsigned char *p){return p[0]|(unsigned)p[1]<<8;}
static int s16(const unsigned char *p){unsigned v=u16(p);return v>=32768?(int)v-65536:(int)v;}
static double number(const unsigned char *p){uint32_t v=u32(p);float f;memcpy(&f,&v,4);return f;}
static void w16(unsigned char *p,unsigned v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);}
static void w32(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static void wf(unsigned char *p,double v){float f=(float)v;uint32_t bits;memcpy(&bits,&f,4);w32(p,bits);}
static int read_at(Reader *r,uint32_t at,void *out,unsigned n)
{
    if(at>r->file->size || n>r->file->size-at)return -1;
    if(n>sizeof r->window)return jgr_disc_read(r->disc,r->file,at,out,n);
    if(at<r->window_start || at-r->window_start>r->window_size || n>r->window_size-(at-r->window_start)){
        r->window_start=at;r->window_size=r->file->size-at;
        if(r->window_size>sizeof r->window)r->window_size=sizeof r->window;
        if(jgr_disc_read(r->disc,r->file,at,r->window,r->window_size))return -1;
    }
    memcpy(out,r->window+(at-r->window_start),n);return 0;
}
static int pointer(Reader *r,uint32_t p,unsigned bytes,uint32_t *offset)
{if(p<r->base || p-r->base>r->file->size || bytes>r->file->size-(p-r->base))return -1;*offset=p-r->base;return 0;}
static void transform(double out[3],const double m[16],const double p[3],int normal)
{
    double q[3];for(unsigned i=0;i<3;i++)q[i]=m[i*4]*p[0]+m[i*4+1]*p[1]+m[i*4+2]*p[2]+(normal?0:m[i*4+3]);
    if(normal){double d=sqrt(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]);if(!d)d=1;for(unsigned i=0;i<3;i++)q[i]/=d;}
    memcpy(out,q,sizeof q);
}
/* Only an exact positive-zero identity can bypass the arithmetic: negative
 * zero coefficients and nonfinite inputs can change zero/NaN propagation. */
static int position_identity(const double m[16])
{
    for(unsigned i=0;i<12;i++){
        double expected=(i%5)==0?1.0:0.0;
        if(m[i]!=expected || (expected==0 && signbit(m[i])))return 0;
    }return 1;
}
static void transform_position(double out[3],const double m[16],const double p[3],int identity)
{
    if(identity && isfinite(p[0]) && isfinite(p[1]) && isfinite(p[2])){
        /* The ordinary identity expression ends with +0, canonicalizing any
         * signed zero. Retain that behavior rather than copying source bits. */
        for(unsigned i=0;i<3;i++)out[i]=p[i]+0.0;
    }else transform(out,m,p,0);
}
void jgr_model_identity(double m[16]){for(unsigned i=0;i<16;i++)m[i]=(i%5)==0;}
void jgr_model_multiply(double out[16],const double a[16],const double b[16])
{double v[16];for(unsigned i=0;i<4;i++)for(unsigned j=0;j<4;j++){double s=0;for(unsigned k=0;k<4;k++)s+=a[i*4+k]*b[k*4+j];v[i*4+j]=s;}memcpy(out,v,sizeof v);}
int jgr_model_inverse(double out[16],const double m[16])
{
    double a[4][8];for(unsigned i=0;i<4;i++)for(unsigned j=0;j<8;j++)a[i][j]=j<4?m[i*4+j]:(j-4==i);
    for(unsigned c=0;c<4;c++){
        unsigned pivot=c;for(unsigned row=c+1;row<4;row++)if(fabs(a[row][c])>fabs(a[pivot][c]))pivot=row;
        for(unsigned j=0;j<8;j++){double t=a[c][j];a[c][j]=a[pivot][j];a[pivot][j]=t;}
        double divisor=a[c][c];if(!divisor)divisor=1e-12;
        for(unsigned j=0;j<8;j++)a[c][j]/=divisor;
        for(unsigned row=0;row<4;row++)if(row!=c && a[row][c]){double f=a[row][c];for(unsigned j=0;j<8;j++)a[row][j]-=f*a[c][j];}
    }
    for(unsigned i=0;i<4;i++)for(unsigned j=0;j<4;j++){out[i*4+j]=a[i][j+4];if(!isfinite(out[i*4+j]))return -1;}
    return 0;
}
int jgr_model_node(JgrDisc *disc,const JgrDiscFile *file,uint32_t base,uint32_t address,JgrModelNode *n)
{
    unsigned char b[52];if(!n || address<base || address-base>file->size || sizeof b>file->size-(address-base) || jgr_disc_read(disc,file,address-base,b,sizeof b))return -1;
    n->address=address;n->eval=u32(b);n->model=u32(b+4);n->child=u32(b+44);n->sibling=u32(b+48);
    for(unsigned i=0;i<3;i++){n->position[i]=number(b+8+i*4);n->angle[i]=(int32_t)u32(b+20+i*4);n->scale[i]=number(b+32+i*4);if(!isfinite(n->position[i]) || !isfinite(n->scale[i]))return -1;}
    return 0;
}
static void rotation(double m[16],unsigned axis,double angle)
{double c=cos(angle),s=sin(angle);jgr_model_identity(m);unsigned a=(axis+1)%3,b=(axis+2)%3;m[a*4+a]=m[b*4+b]=c;m[a*4+b]=-s;m[b*4+a]=s;}
void jgr_model_node_matrix(double out[16],const JgrModelNode *n,int legacy)
{
    double temp[16],rot[3][16],mix[16];jgr_model_identity(out);
    if(!(n->eval&1)){out[3]=n->position[0];out[7]=n->position[1];out[11]=n->position[2];}
    if(!(n->eval&2)){
        for(unsigned i=0;i<3;i++)rotation(rot[i],i,n->angle[i]*(2*PI/65536.0));
        if(legacy){jgr_model_multiply(temp,rot[1],rot[2]);jgr_model_multiply(mix,rot[0],temp);}
        else if(n->eval&0x20){jgr_model_multiply(temp,rot[0],rot[2]);jgr_model_multiply(mix,rot[1],temp);}
        else{jgr_model_multiply(temp,rot[1],rot[0]);jgr_model_multiply(mix,rot[2],temp);}
        jgr_model_multiply(out,out,mix);
    }
    if(!(n->eval&4)){jgr_model_identity(temp);temp[0]=n->scale[0];temp[5]=n->scale[1];temp[10]=n->scale[2];jgr_model_multiply(out,out,temp);}
}
static int collision_node(JgrDisc *disc,const JgrDiscFile *file,uint32_t base,uint32_t address,JgrModelNode *n)
{
    unsigned char b[68];if(address<base || address-base>file->size || sizeof b>file->size-(address-base) || jgr_disc_read(disc,file,address-base,b,sizeof b))return -1;
    unsigned kind=u16(b),shape=u32(b+4);if((kind!=0 && kind!=2) || u32(b+8) || u32(b+48) || u32(b+52) || u32(b+56))return -1;
    if(shape && (u16(b+2)!=26 || shape<base || shape-base>file->size || 16>file->size-(shape-base)))return -1;
    memset(n,0,sizeof *n);n->address=address;n->child=u32(b+60);n->sibling=u32(b+64);
    for(unsigned i=0;i<3;i++){n->position[i]=number(b+12+i*4);n->angle[i]=(int32_t)u32(b+24+i*4);n->scale[i]=number(b+36+i*4);if(!isfinite(n->position[i]) || !isfinite(n->scale[i]))return -1;}
    return 0;
}
static int walk_nodes(JgrDisc *d,const JgrDiscFile *f,uint32_t base,uint32_t root,const double parent[16],int legacy,int root_sibling,JgrModelVisit visit,void *user,int collision)
{
    typedef struct{uint32_t address;int parent,sibling;double matrix[16];} Pending;
    Pending *stack=malloc(128*sizeof *stack);uint32_t *seen=malloc(4096*sizeof *seen);unsigned sp=0,n=0;int result=-1;
    if(!stack || !seen || !visit)goto done;
    stack[sp].address=root;stack[sp].parent=-1;stack[sp].sibling=root_sibling;memcpy(stack[sp++].matrix,parent,128);
    while(sp){Pending p=stack[--sp];JgrModelNode node;double local[16],world[16];if(!p.address)continue;
        if(n>=4096 || (collision?collision_node(d,f,base,p.address,&node):jgr_model_node(d,f,base,p.address,&node)))goto done;
        for(unsigned i=0;i<n;i++)if(seen[i]==p.address)goto done;
        seen[n]=p.address;jgr_model_node_matrix(local,&node,legacy);jgr_model_multiply(world,p.matrix,local);
        if(visit(&node,world,n,p.parent,user))goto done;
        if(node.sibling && p.sibling){if(sp>=128)goto done;stack[sp].address=node.sibling;stack[sp].parent=p.parent;stack[sp].sibling=1;memcpy(stack[sp++].matrix,p.matrix,128);}
        if(node.child){if(sp>=128)goto done;stack[sp].address=node.child;stack[sp].parent=(int)n;stack[sp].sibling=1;memcpy(stack[sp++].matrix,world,128);}n++;
    }result=0;
done:free(stack);free(seen);return result;
}
int jgr_model_walk(JgrDisc *d,const JgrDiscFile *f,uint32_t base,uint32_t root,const double parent[16],int legacy,int root_sibling,JgrModelVisit visit,void *user)
{return walk_nodes(d,f,base,root,parent,legacy,root_sibling,visit,user,0);}
JgrModelCache *jgr_model_cache_create(void){return calloc(1,sizeof(JgrModelCache));}
void jgr_model_cache_clear(JgrModelCache *c){if(c){if(c->capacity)memset(c->vertices,0,c->capacity*sizeof *c->vertices);c->generation=0;}}
void jgr_model_cache_free(JgrModelCache *c){if(c){free(c->vertices);free(c);}}
void jgr_model_options_default(JgrModelOptions *o)
{memset(o,0,sizeof *o);jgr_model_identity(o->matrix);jgr_model_identity(o->bind);jgr_model_identity(o->inverse_bind);o->owner=-1;o->light=1;}
static int cache_size(JgrModelCache *c,unsigned n)
{
    if(n>MAX_VERTICES)return -1;
    if(n<=c->capacity)return 0;
    unsigned cap=c->capacity?c->capacity:64;while(cap<n)cap*=2;
    Vertex *v=realloc(c->vertices,cap*sizeof *v);if(!v)return -1;
    memset(v+c->capacity,0,(cap-c->capacity)*sizeof *v);c->vertices=v;c->capacity=cap;return 0;
}
static int parse_vertices(Reader *r,uint32_t at,JgrModelCache *c,const JgrModelOptions *o)
{
    static const unsigned char strides[]={16,32,12,16,16,16,16,16,16,24,28,28,28,28,28,28,16,16,16};
    unsigned count=0;
    while(count++<MAX_VERTICES){unsigned char head[8];if(read_at(r,at,head,4))return -1;unsigned type=head[0];if(type==255)return 0;
        if(!type){at+=4;continue;}if(type<32 || type>50 || read_at(r,at,head,8))return -1;
        unsigned size=u16(head+2),first=u16(head+4),n=u16(head+6),stride=strides[type-32];
        if(size<1 || n>(size*4-4)/stride || at>r->file->size || 4+size*4>r->file->size-at || cache_size(c,first+n))return -1;
        for(unsigned i=0;i<n;i++){unsigned char data[32];if(read_at(r,at+8+i*stride,data,stride))return -1;Vertex *v=c->vertices+first+i;memset(v,0,sizeof *v);
            v->generation=c->generation;v->flags=1;v->owner=o->owner;for(unsigned k=0;k<3;k++)v->p[k]=number(data+k*4);
            if(type==33 || (type>=41 && type<=47)){unsigned offset=type==33?16:12;v->flags|=2;for(unsigned k=0;k<3;k++)v->n[k]=number(data+offset+k*4);}
            if(type==35 || type==42){v->flags|=4;v->color=u32(data+(type==35?12:24));}
            /* A few original EVENT meshes retain NaN normals even on drawn
             * corners. The source-lighting path keeps those original normal
             * bytes without using them for this setup-time color bake. */
            for(unsigned k=0;k<3;k++)if(!isfinite(v->p[k]))return -1;
            /* Owner-local banks retain the authored position directly. World
             * positions serve only legacy cross-bone vertices; world normals
             * additionally serve the baked-lighting path below. */
            if(o->cache_vertices){
                if(o->owner<0)transform(v->wp,o->bind,v->p,0);
                if((v->flags&2) && (o->owner<0 || o->baked_light))transform(v->wn,o->bind,v->n,1);
            }
        }at+=4+size*4;
    }return -1;
}
static int group_compare(const void *a,const void *b)
{const Group *x=a,*y=b;return x->texture<y->texture?-1:x->texture>y->texture?1:x->flags<y->flags?-1:x->flags>y->flags;}
static int uv_key(int numerator,unsigned divisor)
{
    /* Source UVs are signed 16-bit integers with divisors 1, 255 or 1023.
     * Round the exact rational to the same ties-to-even key without software
     * double division/floor for every triangle corner on the PSP. */
    unsigned scaled=(unsigned)(numerator<0?-numerator:numerator)*10000u;
    unsigned whole=scaled/divisor,remainder=scaled%divisor;
    if(remainder>divisor-remainder || (remainder==divisor-remainder && (whole&1)))whole++;
    return numerator<0?-(int)whole:(int)whole;
}
static uint32_t shade(const Vertex *v,const double *normal,uint32_t diffuse,const JgrModelOptions *o,unsigned flags)
{
    uint32_t base=(v->flags&4)?v->color:diffuse;unsigned a=base>>24,r=(base>>16)&255,g=(base>>8)&255,b=base&255;
    if(o->light && !o->source_lighting && (v->flags&2) && !(v->flags&4) && !(o->baked_light && (flags&1))){
        if(o->baked_light){
            const JgrModelLight *p=o->baked_light;double rgb[3],intensity;
            if(o->toon){double dot=0;for(unsigned i=0;i<3;i++)dot+=p->cel_direction[i]*normal[i];unsigned color=(-dot)>0;for(unsigned i=0;i<3;i++)rgb[i]=p->cel_colors[color][i]/255.0;intensity=1;}
            else{double length=sqrt(p->direction[0]*p->direction[0]+p->direction[1]*p->direction[1]+p->direction[2]*p->direction[2]);if(!length)length=1;double dot=0;for(unsigned i=0;i<3;i++)dot+=p->direction[i]*normal[i]/length;
                intensity=p->ambient-p->diffuse*dot;if(intensity<p->ambient)intensity=p->ambient;memcpy(rgb,p->rgb,sizeof rgb);}
            unsigned channels[3]={r,g,b};for(unsigned i=0;i<3;i++){double value=channels[i]*rgb[i]*intensity;channels[i]=value<0?0:value>255?255:(unsigned)value;}r=channels[0];g=channels[1];b=channels[2];
        }else{
            double length=sqrt(0.4*0.4+0.8*0.8+0.45*0.45),dot=normal[0]*(0.4/length)+normal[1]*(0.8/length)+normal[2]*(0.45/length);
            double k=o->toon?(dot>0.15?1:0.55):0.55+0.45*(dot>0?dot:0);r=(unsigned)(r*k);g=(unsigned)(g*k);b=(unsigned)(b*k);
        }
    }return a<<24|b<<16|g<<8|r;
}
static int index_append(Assembly *a,uint32_t value)
{
    if(a->ni>=MAX_REFERENCES)return -1;
    if(a->ni==a->icap){unsigned n=a->icap?a->icap*2:256;uint32_t *p=realloc(a->indices,n*sizeof *p);if(!p)return -1;a->indices=p;a->icap=n;}a->indices[a->ni++]=value;return 0;
}
static int triangle(Assembly *a,unsigned texture,unsigned flags,uint32_t diffuse,const Corner corners[3])
{
    const JgrModelOptions *o=a->options;unsigned target=65535;
    if(texture!=65535){if(o->texture_map){if(!o->texture_slots)return -1;target=o->texture_base+o->texture_map[texture<o->texture_slots?texture:0];}
        else {if(!o->texture_count)return -1;target=o->texture_base+(texture<o->texture_count?texture:o->texture_count-1);}if(target>=65535)return -1;}
    unsigned bflags=((flags&8)?1:0)|(((flags&16)||o->two_sided)?2:0)|(texture==65535?4:0)|((flags&1)?8:0)|(flags>>8);
    if(o->source_lighting)bflags|=o->source_lighting<<12;
    if(a->collect){for(unsigned i=0;i<a->group_count;i++)if(a->groups[i].texture==target && a->groups[i].flags==bflags){if(a->groups[i].corners<MAX_VERTICES)a->groups[i].corners+=3;return 0;}
        if(a->group_count>=MAX_GROUPS)return -1;
        Group *group=a->groups+a->group_count++;group->texture=target;group->flags=bflags;group->corners=3;return 0;}
    Group group=a->groups[a->selected];if(group.texture!=target || group.flags!=bflags)return 0;
    for(unsigned i=0;i<3;i++){
        const Corner *k=corners+i;if(k->index>=a->cache->capacity)continue;const Vertex *v=a->cache->vertices+k->index;
        if(!(v->flags&1) || (!o->cache_vertices && v->generation!=a->cache->generation))continue;
        int owner=o->owner>=0?v->owner:-1,qu=uv_key(k->u,k->divisor),qv=uv_key(k->v,k->divisor);
        uint32_t hash=(k->index*2654435761u)^(uint32_t)qu*2246822519u^(uint32_t)qv*3266489917u^diffuse^(unsigned)owner*668265263u;
        unsigned slot=hash&a->hash_mask,found;
        while((found=a->hash[slot])){MeshVertex *mv=a->vertices+found-1;if(mv->original==k->index && mv->qu==qu && mv->qv==qv && mv->diffuse==diffuse && mv->owner==owner)break;slot=(slot+1)&a->hash_mask;}
        if(!found){if(a->nv>=MAX_VERTICES)return -1;if(a->nv==a->vcap){unsigned cap=a->vcap?a->vcap*2:64;MeshVertex *p=realloc(a->vertices,cap*sizeof *p);if(!p)return -1;a->vertices=p;a->vcap=cap;}
            MeshVertex *mv=a->vertices+a->nv;mv->u=(double)k->u/k->divisor;mv->v=(double)k->v/k->divisor;mv->original=k->index;mv->qu=qu;mv->qv=qv;mv->diffuse=diffuse;mv->owner=owner;
            double position[3],normal[3];memcpy(position,v->p,sizeof position);memcpy(normal,v->n,sizeof normal);
            if(o->cache_vertices && o->owner<0 && v->generation!=a->cache->generation){transform(position,o->inverse_bind,v->wp,0);if(v->flags&2)transform(normal,o->inverse_bind,v->wn,1);}
            transform_position(mv->p,o->matrix,position,a->position_identity);memcpy(mv->n,normal,sizeof normal);
            /* Source lighting publishes the authored normal in mv->n and
             * shades at runtime. Other unlit/vertex-colored paths likewise
             * never consume the transformed normal. Palette mixing still
             * needs its legacy lighting normal, even on a baked alpha face. */
            int uses_normal=o->light && !o->source_lighting && !(v->flags&4) &&
                (!o->baked_light || !(flags&1) || o->palette_mix);
            if((v->flags&2) && uses_normal)transform(normal,o->light_matrix?o->light_matrix:o->matrix,normal,1);
            double legacy_normal[3];memcpy(legacy_normal,normal,sizeof normal);
            if(o->baked_light && o->owner>=0 && o->cache_vertices && (v->flags&2))memcpy(normal,v->wn,sizeof normal);
            if(o->palette_mix){
                JgrModelOptions legacy=*o,baked=*o;legacy.baked_light=NULL;baked.toon=1;
                uint32_t old=shade(v,legacy_normal,diffuse,&legacy,flags),color=old;
                if(!(flags&1)){
                    uint32_t corrected=shade(v,normal,diffuse,&baked,flags);legacy.light=0;
                    uint32_t unlit=shade(v,legacy_normal,diffuse,&legacy,flags);color=old&0xff000000u;
                    for(unsigned shift=0;shift<24;shift+=8){
                        double a=(old>>shift)&255,b=(corrected>>shift)&255;
                        unsigned mixed=(unsigned)(a+(b-a)*0.5+0.5);
                        mixed=(unsigned)(mixed+(((unlit>>shift)&255)-(double)mixed)*o->shadow_lift+0.5);
                        color|=mixed<<shift;
                    }
                }mv->color=color;
            }else mv->color=shade(v,normal,diffuse,o,flags);a->hash[slot]=++a->nv;found=a->nv;
        }if(index_append(a,found-1))return -1;
    }return 0;
}
static int parse_strips(Reader *r,uint32_t at,Assembly *a)
{
    static const unsigned char extra[]={0,2,2,3,5,5,2,4,4,1,3,3};
    unsigned texture=0,blend=0x25,guard=0;uint32_t diffuse=0xffffffffu;
    while(guard++<MAX_REFERENCES){unsigned char head[8];if(read_at(r,at,head,2))return -1;unsigned type=head[0],flags=head[1];if(type==255)return 0;
        if(!type || (type>=1 && type<=5)){if(type==1)blend=flags;at+=2;continue;}
        if(type==8 || type==9){if(read_at(r,at,head,4))return -1;texture=u16(head+2)&8191;at+=4;continue;}
        if((type>=16 && type<=31)||(type>=56 && type<=58)){
            if(read_at(r,at,head,4))return -1;
            unsigned bytes=4+2*u16(head+2);if(bytes>r->file->size-at)return -1;
            if(type<=31){blend=flags;if(type&1){if(bytes<8 || read_at(r,at+4,head,4))return -1;diffuse=u32(head);}}at+=bytes;continue;
        }
        if(type<64 || type>75 || read_at(r,at,head,6))return -1;
        unsigned bytes=4+2*u16(head+2),count=u16(head+4)&16383,user_flags=u16(head+4)>>14;
        if(bytes<6 || bytes>r->file->size-at)return -1;
        uint32_t end=at+bytes,p=at+6;
        unsigned divisor=(type==65||type==68||type==71||type==73)?255:(type==66||type==69||type==72||type==74)?1023:1;
        int has_uv=type==65||type==66||type==68||type==69||type==71||type==72;
        int environment=a->options->source_environment && type==64 && (flags&64);
        unsigned draw_flags=flags;if(a->options->source_material)draw_flags|=(16|((blend>>3&7)<<5)|((blend&7)<<8))<<8;if(environment)draw_flags|=2048<<8;
        for(unsigned strip=0;strip<count;strip++){
            if(p>end || end-p<2 || read_at(r,p,head,2))return -1;
            int signed_n=s16(head);unsigned n=signed_n<0?(unsigned)-signed_n:(unsigned)signed_n;p+=2;
            unsigned stride=2+2*extra[type-64];if(n>(end-p)/stride)return -1;Corner previous[2];memset(previous,0,sizeof previous);
            for(unsigned k=0;k<n;k++){
                Corner current;if(read_at(r,p,head,has_uv?6:2))return -1;p+=stride;current.index=u16(head);current.u=has_uv?s16(head+2):0;current.v=has_uv?s16(head+4):0;current.divisor=divisor;
                if(k>=2){Corner tri[3]={previous[0],previous[1],current};if(((k-2)&1)^(signed_n<0)){Corner swap=tri[0];tri[0]=tri[1];tri[1]=swap;}if(triangle(a,(has_uv||environment)?texture:65535,draw_flags,diffuse,tri))return -1;}
                previous[0]=previous[1];previous[1]=current;
            }
            unsigned skip=(n>2?n-2:0)*user_flags*2;if(skip>end-p)return -1;p+=skip;
        }at=end;
    }return -1;
}
typedef struct {unsigned left,right,vertex;} Midpoint;
typedef struct {Midpoint *data;unsigned count,capacity,hash_size;} Midpoints;
static unsigned midpoint_hash(unsigned left,unsigned right,unsigned mask)
{return (left*2654435761u^right*2246822519u)&mask;}
static int midpoint_grow(Assembly *a,Midpoints *m)
{
    if(!m->hash_size || m->hash_size>HASH_SIZE/2 || m->count>m->hash_size/2)return -1;
    unsigned size=m->hash_size*2;
    if(assembly_hash_reserve(a,size))return -1;
    memset(a->hash,0,size*sizeof *a->hash);
    for(unsigned i=0;i<m->count;i++){
        const Midpoint *p=m->data+i;unsigned slot=midpoint_hash(p->left,p->right,size-1);
        while(a->hash[slot])slot=(slot+1)&(size-1);
        a->hash[slot]=i+1;
    }
    m->hash_size=size;return 0;
}
static double edge_length(const MeshVertex *a,const MeshVertex *b)
{
    /* Rank edges at the precision written to the renderer. Host and PSP
     * libm can differ by one binary64 ulp; using those invisible differences
     * to break equal-length edges otherwise changes the subdivision mesh. */
    double x=(double)(float)a->p[0]-(double)(float)b->p[0];
    double y=(double)(float)a->p[1]-(double)(float)b->p[1];
    double z=(double)(float)a->p[2]-(double)(float)b->p[2];
    return x*x+y*y+z*z;
}
static int split_group(Assembly *a)
{
    Midpoints mids={0};Assembly out;memset(&out,0,sizeof out);unsigned guard=0;int result=-1;
    if(a->options->owner>=0 || !isfinite(a->options->max_edge))return -1;
    double limit=a->options->max_edge*a->options->max_edge;
    mids.hash_size=128;
    if(assembly_hash_reserve(a,mids.hash_size))return -1;
    memset(a->hash,0,mids.hash_size*sizeof *a->hash);
    /* The reference ignores incomplete final triangles. */
    a->ni-=a->ni%3;
    while(a->ni){
        if(++guard>400000)goto done;
        unsigned triangle[3];for(unsigned i=0;i<3;i++)triangle[i]=a->indices[a->ni-3+i];a->ni-=3;
        double lengths[3]={edge_length(a->vertices+triangle[0],a->vertices+triangle[1]),edge_length(a->vertices+triangle[1],a->vertices+triangle[2]),edge_length(a->vertices+triangle[2],a->vertices+triangle[0])};
        unsigned which=0;for(unsigned i=1;i<3;i++)if(lengths[i]>=lengths[which])which=i;
        if(lengths[which]<=limit){for(unsigned i=0;i<3;i++)if(index_append(&out,triangle[i]))goto done;continue;}
        unsigned left=triangle[which],right=triangle[(which+1)%3];if(left>right){unsigned t=left;left=right;right=t;}
        unsigned slot=midpoint_hash(left,right,mids.hash_size-1),mid;
        while(a->hash[slot] && (mids.data[a->hash[slot]-1].left!=left || mids.data[a->hash[slot]-1].right!=right))slot=(slot+1)&(mids.hash_size-1);
        if(a->hash[slot])mid=mids.data[a->hash[slot]-1].vertex;
        else{
            if(mids.count>=65536 || a->nv>=131072)goto done;
            if(mids.count>=mids.hash_size/2){
                if(midpoint_grow(a,&mids))goto done;
                slot=midpoint_hash(left,right,mids.hash_size-1);
                while(a->hash[slot])slot=(slot+1)&(mids.hash_size-1);
            }
            if(mids.count==mids.capacity){unsigned cap=mids.capacity?mids.capacity*2:256;Midpoint *data=realloc(mids.data,cap*sizeof *data);if(!data)goto done;mids.data=data;mids.capacity=cap;}
            if(a->nv==a->vcap){unsigned cap=a->vcap?a->vcap*2:64;MeshVertex *data=realloc(a->vertices,cap*sizeof *data);if(!data)goto done;a->vertices=data;a->vcap=cap;}
            mid=a->nv++;MeshVertex *m=a->vertices+mid,*l=a->vertices+left,*r=a->vertices+right;memset(m,0,sizeof *m);m->owner=-1;
            m->u=(l->u+r->u)*0.5;m->v=(l->v+r->v)*0.5;for(unsigned k=0;k<3;k++)m->p[k]=(l->p[k]+r->p[k])*0.5;
            for(unsigned shift=0;shift<32;shift+=8)m->color|=(((l->color>>shift&255)+(r->color>>shift&255))/2)<<shift;
            mids.data[mids.count].left=left;mids.data[mids.count].right=right;mids.data[mids.count].vertex=mid;a->hash[slot]=++mids.count;
        }
        unsigned next[6];if(which==0){unsigned t[]={triangle[0],mid,triangle[2],mid,triangle[1],triangle[2]};memcpy(next,t,sizeof next);}
        else if(which==1){unsigned t[]={triangle[0],triangle[1],mid,triangle[0],mid,triangle[2]};memcpy(next,t,sizeof next);}
        else{unsigned t[]={triangle[0],triangle[1],mid,mid,triangle[1],triangle[2]};memcpy(next,t,sizeof next);}
        for(unsigned i=0;i<6;i++)if(index_append(a,next[i]))goto done;
    }
    free(a->indices);a->indices=out.indices;a->ni=out.ni;a->icap=out.icap;out.indices=NULL;result=0;
done:free(mids.data);free(out.indices);return result;
}
static int emit_group(Assembly *a,JgrModelEmit emit,void *user)
{
    if(a->ni<3)return 0;
    unsigned stride=a->options->owner<0?24:a->options->source_environment?40:28;
    unsigned capacity=a->ni<60000?a->ni:60000;
    unsigned char *vb=malloc(capacity*stride),*ib=malloc(capacity*2);uint32_t *remap=malloc(a->nv*sizeof *remap);double *positions=malloc(capacity*3*sizeof *positions);unsigned char *lighting=a->options->retain_lighting?malloc(capacity*20):NULL;int result=-1;
    if(!vb || !ib || !remap || !positions || (a->options->retain_lighting && (!lighting || stride<28)))goto done;
    for(unsigned start=0;start<a->ni;start+=60000){unsigned count=a->ni-start;if(count>60000)count=60000;unsigned nv=0;for(unsigned i=0;i<a->nv;i++)remap[i]=UINT32_MAX;
        for(unsigned i=0;i<count;i++){unsigned index=a->indices[start+i];if(remap[index]==UINT32_MAX){MeshVertex *v=a->vertices+index;unsigned char *dest=vb+nv*stride;const Vertex *original=stride>=28?a->cache->vertices+v->original:NULL;
                double *point=positions+nv*3;if(a->options->post_matrix)transform(point,a->options->post_matrix,v->p,0);else memcpy(point,v->p,3*sizeof *point);
                wf(dest,v->u);wf(dest+4,v->v);w32(dest+8,v->color);for(unsigned k=0;k<3;k++)wf(dest+12+4*k,point[k]);
                if(stride>=28){if(v->owner<0 || v->owner>65535)goto done;w16(dest+24,(unsigned)v->owner);w16(dest+26,a->options->source_lighting && (original->flags&2) && !(original->flags&4));}
                if(stride==40){if((a->groups[a->selected].flags&2048) && !(original->flags&2))goto done;for(unsigned k=0;k<3;k++)wf(dest+28+k*4,v->n[k]);}
                if(lighting){
                    unsigned char *record=lighting+nv*20;for(unsigned k=0;k<3;k++)wf(record+k*4,v->n[k]);
                    JgrModelOptions unlit=*a->options;unlit.light=0;
                    w32(record+12,shade(original,v->n,v->diffuse,&unlit,0));
                    w32(record+16,(a->options->retain_lighting>>8)|((original->flags&2)&&!(original->flags&4)?256:0));
                }
                remap[index]=nv++;
            }w16(ib+2*i,remap[index]);}
        JgrModelBatch b={a->groups[a->selected].texture,a->groups[a->selected].flags,nv,count,stride,vb,ib,positions,lighting};if(emit(&b,user))goto done;
    }result=0;
done:free(vb);free(ib);free(remap);free(positions);free(lighting);return result;
}
int jgr_model_mesh(JgrModelCache *cache,JgrDisc *disc,const JgrDiscFile *file,uint32_t base,uint32_t address,const JgrModelOptions *options,JgrModelEmit emit,void *user)
{
    Reader reader;Assembly a;unsigned char header[8];uint32_t model,vl,pl;int result=-1;
    if(!cache || !disc || !file || !options || !emit || options->source_lighting>2 || (options->source_lighting && (!options->source_environment || options->owner<0)))return -1;
    memset(&reader,0,sizeof reader);reader.disc=disc;reader.file=file;reader.base=base;
    memset(&a,0,sizeof a);a.cache=cache;a.options=options;a.collect=1;a.position_identity=position_identity(options->matrix);
    if(pointer(&reader,address,24,&model) || read_at(&reader,model,header,8))goto done;
    if(!u32(header) || !u32(header+4)){result=0;goto done;}
    if(pointer(&reader,u32(header),4,&vl) || pointer(&reader,u32(header+4),2,&pl))goto done;
    if(++cache->generation==0){jgr_model_cache_clear(cache);cache->generation=1;}
    if(parse_vertices(&reader,vl,cache,options))goto done;
    a.groups=malloc(MAX_GROUPS*sizeof *a.groups);if(!a.groups || parse_strips(&reader,pl,&a))goto done;
    qsort(a.groups,a.group_count,sizeof *a.groups,group_compare);a.collect=0;
    /* Both corner lookup and subdivision use bounded, half-full tables.
     * Hash collisions change lookup cost only, never emitted vertex order. */
    for(a.selected=0;a.selected<a.group_count;a.selected++){
        unsigned hash_size=128,vertices=a.groups[a.selected].corners;
        if(vertices>MAX_VERTICES)vertices=MAX_VERTICES;
        while(hash_size<vertices*2)hash_size*=2;
        if(assembly_hash_reserve(&a,hash_size))goto done;
        a.hash_mask=hash_size-1;
        a.nv=a.ni=0;memset(a.hash,0,hash_size*sizeof *a.hash);if(parse_strips(&reader,pl,&a))goto done;
        if(a.ni<3 || a.nv>65535)continue;
        if((options->max_edge>0 && split_group(&a)) || emit_group(&a,emit,user))goto done;
    }result=0;
done:free(a.groups);free(a.vertices);free(a.indices);free(a.hash);return result;
}

/* The compaction step is independent of geometry conversion and never changes
 * mesh, key or texture bytes. It reads a private generated bank, not a bundled
 * game payload. Seekable temporary files keep the large key/pixel regions off
 * the PSP heap. */
static int file_read(FILE *f,uint32_t at,void *data,unsigned size)
{return fseek(f,(long)at,SEEK_SET) || fread(data,1,size,f)!=size?-1:0;}
static int file_copy(FILE *src,FILE *out,uint32_t at,uint32_t size)
{
    unsigned char b[4096];if(fseek(src,(long)at,SEEK_SET))return -1;
    while(size){unsigned n=size<sizeof b?size:sizeof b;if(fread(b,1,n,src)!=n || fwrite(b,1,n,out)!=n)return -1;size-=n;}return 0;
}
static int file_pad(FILE *f)
{long at=ftell(f);unsigned char zero[16]={0};if(at<0)return -1;unsigned n=(unsigned)(-at)&15;return fwrite(zero,1,n,f)==n?0:-1;}
static int file_equal(FILE *f,uint32_t a,uint32_t b,unsigned size)
{
    unsigned char first[1024],second[1024];while(size){unsigned n=size<sizeof first?size:sizeof first;if(file_read(f,a,first,n) || file_read(f,b,second,n))return -1;if(memcmp(first,second,n))return 0;size-=n;a+=n;b+=n;}return 1;
}
static int span(uint32_t at,uint32_t count,uint32_t stride,uint32_t size)
{return at<=size && count<=(size-at)/stride;}
typedef struct {FILE *file;const unsigned char *data;uint32_t size;} CompactSource;
static int compact_read(CompactSource *s,uint32_t at,void *data,unsigned size)
{if(s->data){if(at>s->size || size>s->size-at)return -1;memcpy(data,s->data+at,size);return 0;}return file_read(s->file,at,data,size);}
static int compact_copy(CompactSource *s,FILE *out,uint32_t at,uint32_t size)
{if(s->data){if(at>s->size || size>s->size-at)return -1;return fwrite(s->data+at,1,size,out)==size?0:-1;}return file_copy(s->file,out,at,size);}
static int compact_equal(CompactSource *s,uint32_t a,uint32_t b,unsigned size)
{if(s->data){if(a>s->size || size>s->size-a || b>s->size || size>s->size-b)return -1;return !memcmp(s->data+a,s->data+b,size);}return file_equal(s->file,a,b,size);}
int jgr_model_compact(FILE *source,FILE *output,int mesh_only)
{return jgr_model_compact_view(source,output,mesh_only,NULL,0);}
int jgr_model_compact_view(FILE *source,FILE *output,int mesh_only,const void *input_data,size_t input_bytes)
{
    typedef struct {uint32_t source,output,bytes;uint64_t hash;} Table;
    unsigned char header[80],record[48],*snapshot=NULL,*channel_data=NULL;CompactSource bank_source={source,NULL,0};unsigned maximum_bones=0;uint32_t h[16],size,*owners=NULL,*buckets=NULL;Table *tables=NULL;unsigned table_count=0;int result=-1;
    if(!source || !output || source==output || fseek(source,0,SEEK_END))return -1;
    long length=ftell(source);if(length<80 || (unsigned long)length>UINT32_MAX || compact_read(&bank_source,0,header,80))return -1;size=(uint32_t)length;bank_source.size=size;
    if(input_data){if(input_bytes!=size)return -1;bank_source.data=input_data;}
    unsigned stride=!memcmp(header,"JGC1",4)?24:!memcmp(header,"JGC2",4)?28:!memcmp(header,"JGC3",4)?40:0;if(!stride)return -1;
    for(unsigned i=0;i<16;i++)h[i]=u32(header+4+4*i);
    unsigned nc=h[0],nb=h[1],nt=h[5],nm=h[6];uint32_t oc=h[7],ot=h[12],om=h[13],ok=h[14],op=h[15];
    if(nc>4096 || nb>16384 || nt>8192 || nm>8192 || h[7]<68 || h[15]>size)return -1;
    for(unsigned i=7;i<15;i++)if(h[i]>h[i+1])return -1;
    unsigned strides[]={48,48,16,stride,2,16,8};for(unsigned i=0;i<7;i++)if(h[i]>(h[8+i]-h[7+i])/strides[i])return -1;
    owners=malloc((nm?nm:1)*sizeof *owners);tables=malloc((nm?nm:1)*sizeof *tables);buckets=calloc(16384,sizeof *buckets);if(!owners || !tables || !buckets)goto done;
    for(unsigned i=0;i<nm;i++)owners[i]=UINT32_MAX;
    for(unsigned i=0;i<nc;i++){
        if(compact_read(&bank_source,oc+48*i,record,16))goto done;
        uint32_t bone=u32(record),bones=u32(record+4),motion=u32(record+8),count=u32(record+12);
        if(bone>nb || bones>nb-bone || motion>nm || count>nm-motion || bones>4096)goto done;
        if(bones>maximum_bones)maximum_bones=bones;
        for(unsigned j=motion;j<motion+count;j++){if(owners[j]!=UINT32_MAX && (memcmp(header+68,"JCL1",4)||owners[j]!=bones))goto done;owners[j]=bones;}
    }
    channel_data=malloc(maximum_bones?maximum_bones*24:1);if(!channel_data)goto done;
    if(!input_data && size<=JGR_MODEL_SNAPSHOT_LIMIT){
        /* An indirect volatile call keeps the compiler from deleting this
     * allocation/free pair: its occupied heap is the purpose of the guard. */
    int saved=errno;void *(*volatile reserve)(size_t)=malloc;
    void (*volatile release)(void *)=free;
    void *headroom=reserve(6u*1024u*1024u);
        if(headroom){snapshot=malloc(size);release(headroom);}
        if(snapshot){if(file_read(source,0,snapshot,size))goto done;bank_source.data=snapshot;}
        else errno=saved;
    }
    if(fseek(output,0,SEEK_SET) || compact_copy(&bank_source,output,0,om))goto done;
    if(!mesh_only){unsigned char zero[8]={0};for(unsigned i=0;i<nm;i++)if(fwrite(zero,1,8,output)!=8)goto done;}
    for(unsigned i=0;i<nm;i++){
        unsigned bones=owners[i];if(bones==UINT32_MAX || compact_read(&bank_source,om+8*i,record,8))goto done;uint32_t frames=u32(record),channel=u32(record+4),bytes=bones*24;
        if(channel<om+nm*8 || channel>ok || bytes>ok-channel || compact_read(&bank_source,channel,channel_data,bytes))goto done;
        for(unsigned bone=0;bone<bones;bone++){
            const unsigned char *row=channel_data+bone*24;
            for(unsigned k=0;k<3;k++){unsigned count=u32(row+k*8),offset=u32(row+k*8+4),keystride=k==1?6:12;if(count && !span(offset,count,keystride,op-ok))goto done;}
        }
        if(mesh_only)continue;
        /* Hashes only select candidates. Full bytes still decide equality. */
        uint32_t hash=jgr_crc32_add(UINT32_MAX,channel_data,bytes);
        unsigned slot=(unsigned)hash&16383,match=0;
        while(buckets[slot]){Table *t=tables+buckets[slot]-1;if(t->hash==hash && t->bytes==bytes){int equal=compact_equal(&bank_source,channel,t->source,bytes);if(equal<0)goto done;if(equal){match=buckets[slot];break;}}slot=(slot+1)&16383;}
        uint32_t target;
        if(match)target=tables[match-1].output;
        else{
            if(file_pad(output))goto done;
            long pos=ftell(output);if(pos<0 || (unsigned long)pos>UINT32_MAX)goto done;target=(uint32_t)pos;
            if(fwrite(channel_data,1,bytes,output)!=bytes)goto done;
            tables[table_count].source=channel;tables[table_count].output=target;tables[table_count].bytes=bytes;tables[table_count].hash=hash;buckets[slot]=++table_count;
        }
        long end=ftell(output);if(end<0 || fseek(output,(long)(om+i*8),SEEK_SET))goto done;w32(record,frames);w32(record+4,target);if(fwrite(record,1,8,output)!=8 || fseek(output,end,SEEK_SET))goto done;
    }
    uint32_t newpix;
    if(mesh_only){h[6]=0;h[13]=h[14]=h[15]=newpix=om;}
    else{if(file_pad(output))goto done;
            long pos=ftell(output);if(pos<0 || (unsigned long)pos>UINT32_MAX)goto done;h[14]=(uint32_t)pos;
        if(compact_copy(&bank_source,output,ok,op-ok) || file_pad(output))goto done;
        pos=ftell(output);if(pos<0 || (unsigned long)pos>UINT32_MAX)goto done;h[15]=newpix=(uint32_t)pos;}
    if(compact_copy(&bank_source,output,op,size-op))goto done;
    for(unsigned i=0;i<nt;i++){
        if(compact_read(&bank_source,ot+16*i,record,16))goto done;
        unsigned width=u16(record),height=u16(record+2);uint32_t clut=u32(record+8),pixels=u32(record+12);
        if(!width || !height || !span(clut,1024,1,size) || !span(pixels,width,height,size) || clut<op || pixels<op || clut>=size || pixels>=size || clut-op>UINT32_MAX-newpix || pixels-op>UINT32_MAX-newpix)goto done;
        w32(record+8,clut-op+newpix);w32(record+12,pixels-op+newpix);if(fseek(output,(long)(ot+16*i),SEEK_SET) || fwrite(record,1,16,output)!=16)goto done;
    }
    if(mesh_only)for(unsigned i=0;i<nc;i++){unsigned char zero[8]={0};if(fseek(output,(long)(oc+48*i+8),SEEK_SET) || fwrite(zero,1,8,output)!=8)goto done;}
    for(unsigned i=0;i<16;i++)w32(header+4+4*i,h[i]);
    if(fseek(output,0,SEEK_SET) || fwrite(header,1,68,output)!=68 || fseek(output,0,SEEK_END) || fflush(output))goto done;
    result=0;
done:free(snapshot);free(channel_data);free(owners);free(tables);free(buckets);return result;
}

#define JGC_MAX_TEXTURES 8192u
#define JGC_MAX_KEYS 32768u
#define JGC_KEY_HASH 65536u
#define JGC_SOURCE_CACHE 4096u
typedef struct {uint64_t hash;uint32_t offset,size,alignment;} KeyRecord;
typedef struct {uint64_t source;uint32_t size,alignment,output;} SourceKey;
typedef struct ReuseRow ReuseRow;
struct JgrJgcBuild {
    JgrDisc *disc;FILE *section[10];JgrModelCache *cache;
    const JgrJgcCharacter *current;
    unsigned legacy,failed,stride,nchars,nbones,nbatches,nvertices,nindices,nmotions;
    unsigned char used[JGC_MAX_TEXTURES];unsigned bone_start;
    KeyRecord *keys;unsigned key_count,key_capacity;uint32_t *key_hash;
    SourceKey *source_keys;
    ReuseRow *rows;size_t row_bytes;unsigned row_count,row_hits,row_vertices;
};
static int append_bytes(FILE *f,const void *p,unsigned n){return fwrite(p,1,n,f)==n?0:-1;}
static int position(FILE *f,uint32_t *out)
{long n=ftell(f);if(n<0 || (unsigned long)n>UINT32_MAX)return -1;*out=(uint32_t)n;return 0;}
static int align_file(FILE *f,unsigned alignment)
{uint32_t n;unsigned char zero[16]={0};if(position(f,&n))return -1;return append_bytes(f,zero,(-n)&(alignment-1));}
/* Optional per-bank reuse of already validated zero-motion source-lit rows.
 * Source object pointers may differ, so equality checks the complete ordered
 * hierarchy and raw chunk payloads, never just a hash or model address. */
#ifndef JGR_MODEL_ROW_REUSE
#define JGR_MODEL_ROW_REUSE 1
#endif
#define ROW_REUSE_BYTES (128u*1024u)
#define ROW_REUSE_ENTRIES 64u
#define ROW_REUSE_NODES 1024u
typedef struct {
    uint32_t address,model,eval;int parent;
    float position[3],scale[3];int32_t angle[3];
} ReuseNode;
struct ReuseRow {
    struct ReuseRow *next;
    JgrModelOptions options;JgrDiscFile source;
    uint32_t base,effect_blend;unsigned force_alpha,force_two_sided;
    unsigned nodes,maps,bone,batches,vertices,indices,nbatches,nvertices,nindices,stride;
    size_t bytes;
    /* Followed by nodes ReuseNode records and maps uint16_t entries. */
};
static ReuseNode *reuse_nodes(ReuseRow *r){return (ReuseNode *)(r+1);}
static uint16_t *reuse_maps(ReuseRow *r){return (uint16_t *)(reuse_nodes(r)+r->nodes);}
static int reuse_eligible(const JgrJgcBuild *b,const JgrJgcCharacter *c)
{
    const JgrModelOptions *o=&c->model;
    return JGR_MODEL_ROW_REUSE && !b->legacy && !c->motion_count && !c->collision_tree &&
        !c->lighting && o->source_environment && o->source_lighting &&
        !o->baked_light && !o->light_matrix && !o->post_matrix && !o->palette_mix &&
        !o->retain_lighting && !o->max_edge && o->texture_slots<=512 &&
        (!o->texture_slots || o->texture_map) && (!o->texture_map || o->texture_slots);
}
static void reuse_free(JgrJgcBuild *b)
{
    while(b->rows){ReuseRow *r=b->rows;b->rows=r->next;free(r);}
    b->row_bytes=0;b->row_count=0;
}
static int reuse_pose_equal(const ReuseNode *a,const ReuseNode *z)
{
    return !!a->model==!!z->model && a->eval==z->eval && a->parent==z->parent &&
        !memcmp(a->position,z->position,sizeof a->position) &&
        !memcmp(a->scale,z->scale,sizeof a->scale) && !memcmp(a->angle,z->angle,sizeof a->angle);
}
static ReuseRow *reuse_collect(JgrDisc *disc,const JgrJgcCharacter *c)
{
    typedef struct {uint32_t address;int parent;} Pending;
    Pending stack[128];unsigned sp=1,count=0,capacity=0;ReuseNode *nodes=NULL;ReuseRow *result=NULL;
    stack[0]=(Pending){c->root,-1};
    while(sp){
        Pending p=stack[--sp];JgrModelNode n;if(!p.address)continue;
        if(count>=ROW_REUSE_NODES || jgr_model_node(disc,&c->source,c->base,p.address,&n))goto done;
        for(unsigned i=0;i<count;i++)if(nodes[i].address==p.address)goto done;
        if(count==capacity){unsigned cap=capacity?capacity*2:16;ReuseNode *v=realloc(nodes,cap*sizeof *v);if(!v)goto done;nodes=v;capacity=cap;}
        ReuseNode *v=nodes+count;memset(v,0,sizeof *v);v->address=n.address;v->model=n.model;v->eval=n.eval;v->parent=p.parent;
        for(unsigned k=0;k<3;k++){v->position[k]=(float)n.position[k];v->scale[k]=(float)n.scale[k];v->angle[k]=n.angle[k];}
        if(n.sibling){if(sp>=128)goto done;stack[sp++]=(Pending){n.sibling,p.parent};}
        if(n.child){if(sp>=128)goto done;stack[sp++]=(Pending){n.child,(int)count};}count++;
    }
    if(!count)goto done;
    unsigned maps=c->model.texture_map?c->model.texture_slots:0;
    size_t bytes=sizeof *result+count*sizeof *nodes+maps*sizeof(uint16_t);
    if(bytes>ROW_REUSE_BYTES || !(result=calloc(1,bytes)))goto done;
    result->bytes=bytes;result->nodes=count;result->maps=maps;result->options=c->model;
    result->options.texture_map=NULL;result->source=c->source;result->base=c->base;
    result->force_alpha=c->force_alpha;result->force_two_sided=c->force_two_sided;result->effect_blend=c->effect_blend;
    memcpy(reuse_nodes(result),nodes,count*sizeof *nodes);
    if(maps)memcpy(reuse_maps(result),c->model.texture_map,maps*sizeof(uint16_t));
 done:free(nodes);return result;
}
static int reuse_options_equal(const ReuseRow *a,const ReuseRow *z)
{
    if(a->nodes!=z->nodes || a->maps!=z->maps || a->source.offset!=z->source.offset ||
       a->source.size!=z->source.size || a->base!=z->base || a->effect_blend!=z->effect_blend ||
       a->force_alpha!=z->force_alpha || a->force_two_sided!=z->force_two_sided ||
       memcmp(&a->options,&z->options,sizeof a->options) ||
       memcmp(reuse_maps((ReuseRow *)a),reuse_maps((ReuseRow *)z),a->maps*sizeof(uint16_t)))return 0;
    for(unsigned i=0;i<a->nodes;i++)if(!reuse_pose_equal(reuse_nodes((ReuseRow *)a)+i,reuse_nodes((ReuseRow *)z)+i))return 0;
    return 1;
}
static int reuse_chunk_equal(Reader *a,uint32_t ap,Reader *z,uint32_t zp,int vertex)
{
    unsigned char x[1024],y[1024];unsigned guard=vertex?MAX_VERTICES:MAX_REFERENCES;
    while(guard--){
        unsigned head=vertex?4:2;if(read_at(a,ap,x,head) || read_at(z,zp,y,head) || memcmp(x,y,head))return 0;
        unsigned type=x[0],bytes=head;if(type==255)return 1;
        if(vertex){if(type){if(type<32 || type>50)return 0;bytes=4+4*u16(x+2);}}
        else if(type==8 || type==9)bytes=4;
        else if(type>5){if(!((type>=16 && type<=31)||(type>=56 && type<=58)||(type>=64 && type<=75)))return 0;
            if(read_at(a,ap,x,4) || read_at(z,zp,y,4) || memcmp(x,y,4))return 0;
            bytes=4+2*u16(x+2);}
        if(ap>a->file->size || bytes>a->file->size-ap || zp>z->file->size || bytes>z->file->size-zp)return 0;
        for(unsigned off=0;off<bytes;){unsigned n=bytes-off<sizeof x?bytes-off:sizeof x;
            if(read_at(a,ap+off,x,n) || read_at(z,zp+off,y,n) || memcmp(x,y,n))return 0;
            off+=n;}
        ap+=bytes;zp+=bytes;
    }return 0;
}
static int reuse_payload_equal(JgrJgcBuild *b,const ReuseRow *a,const ReuseRow *z)
{
    Reader ar,zr;memset(&ar,0,sizeof ar);memset(&zr,0,sizeof zr);ar.disc=zr.disc=b->disc;
    ar.file=&a->source;ar.base=a->base;zr.file=&z->source;zr.base=z->base;
    for(unsigned i=0;i<a->nodes;i++){
        const ReuseNode *an=reuse_nodes((ReuseRow *)a)+i,*zn=reuse_nodes((ReuseRow *)z)+i;
        if(!an->model || an->model==zn->model)continue;
        uint32_t ap,zp;unsigned char x[8],y[8];
        if(pointer(&ar,an->model,24,&ap)||pointer(&zr,zn->model,24,&zp)||read_at(&ar,ap,x,8)||read_at(&zr,zp,y,8))return 0;
        if(!u32(x)||!u32(x+4)){if(u32(y)&&u32(y+4))return 0;continue;}
        if(!u32(y)||!u32(y+4))return 0;
        for(unsigned kind=0;kind<2;kind++){
            if(pointer(&ar,u32(x+4*kind),kind?2:4,&ap)||pointer(&zr,u32(y+4*kind),kind?2:4,&zp))return 0;
            if(ap!=zp && !reuse_chunk_equal(&ar,ap,&zr,zp,kind==0))return 0;
        }
    }return 1;
}
static int reuse_copy(FILE *f,uint32_t from,uint32_t to,uint32_t bytes)
{
    unsigned char data[4096];while(bytes){unsigned n=bytes<sizeof data?bytes:sizeof data;
        if(file_read(f,from,data,n)||fseek(f,to,SEEK_SET)||append_bytes(f,data,n))return -1;
        from+=n;to+=n;bytes-=n;
    }return 0;
}
static int reuse_emit(JgrJgcBuild *b,const ReuseRow *r)
{
    unsigned char row[48];
    if(r->nodes>32767-b->nbones || r->nbatches>65535-b->nbatches || r->nvertices>UINT32_MAX-b->nvertices ||
       r->nindices>UINT32_MAX-b->nindices || (r->nvertices && b->stride && b->stride!=r->stride))return -1;
    if(r->nvertices && (r->vertices>UINT32_MAX/r->stride || b->nvertices>UINT32_MAX/r->stride || r->nvertices>UINT32_MAX/r->stride))return -1;
    if(r->indices>UINT32_MAX/2 || b->nindices>UINT32_MAX/2 || r->nindices>UINT32_MAX/2)return -1;
    for(unsigned i=0;i<r->nodes;i++){
        if(file_read(b->section[1],48*(r->bone+i),row,48))return -1;
        int parent=(int16_t)u16(row);if(parent>=0)w16(row,(unsigned)parent-r->bone+b->nbones);
        w32(row+40,u32(row+40)-r->batches+b->nbatches);
        if(fseek(b->section[1],48*(b->nbones+i),SEEK_SET)||append_bytes(b->section[1],row,48))return -1;
    }
    for(unsigned i=0;i<r->nbatches;i++){
        if(file_read(b->section[2],16*(r->batches+i),row,16))return -1;
        w32(row,u32(row)-r->vertices+b->nvertices);w32(row+4,u32(row+4)-r->indices+b->nindices);
        unsigned texture=u16(row+12);if(texture!=65535)b->used[texture]=1;
        if(fseek(b->section[2],16*(b->nbatches+i),SEEK_SET)||append_bytes(b->section[2],row,16))return -1;
    }
    if(r->nvertices && reuse_copy(b->section[3],r->vertices*r->stride,b->nvertices*r->stride,r->nvertices*r->stride))return -1;
    if(r->nindices && reuse_copy(b->section[4],r->indices*2,b->nindices*2,r->nindices*2))return -1;
    if(r->nvertices)b->stride=r->stride;
    b->nbones+=r->nodes;b->nbatches+=r->nbatches;b->nvertices+=r->nvertices;b->nindices+=r->nindices;
    b->row_hits++;b->row_vertices+=r->nvertices;return 0;
}
static void reuse_store(JgrJgcBuild *b,ReuseRow *r)
{
    while(b->rows && (b->row_count>=ROW_REUSE_ENTRIES || b->row_bytes>ROW_REUSE_BYTES-r->bytes)){
        ReuseRow *previous=NULL,*last=b->rows;while(last->next){previous=last;last=last->next;}
        if(previous)previous->next=NULL;else b->rows=NULL;
        b->row_bytes-=last->bytes;b->row_count--;free(last);
    }
    r->next=b->rows;b->rows=r;b->row_bytes+=r->bytes;b->row_count++;
}

JgrJgcBuild *jgr_jgc_create(JgrDisc *disc,FILE *sections[10],int legacy)
{
    if(!disc || !sections)return NULL;
    for(unsigned i=0;i<10;i++)if(!sections[i] || fseek(sections[i],0,SEEK_END) || ftell(sections[i])!=0)return NULL;
    JgrJgcBuild *b=calloc(1,sizeof *b);if(!b)return NULL;
    b->disc=disc;b->legacy=!!legacy;memcpy(b->section,sections,sizeof b->section);b->cache=jgr_model_cache_create();b->key_hash=calloc(JGC_KEY_HASH,sizeof *b->key_hash);
    if(!b->legacy)b->source_keys=calloc(JGC_SOURCE_CACHE,sizeof *b->source_keys);
    if(!b->cache || !b->key_hash){jgr_jgc_free(b);return NULL;}return b;
}
void jgr_jgc_free(JgrJgcBuild *b)
{if(b){reuse_free(b);jgr_model_cache_free(b->cache);free(b->keys);free(b->key_hash);free(b->source_keys);free(b);}}
static int jgc_emit(const JgrModelBatch *batch,void *opaque)
{
    JgrJgcBuild *b=opaque;const JgrJgcCharacter *c=b->current;unsigned char row[16];unsigned flags=batch->flags|c->effect_blend;
    if(b->nbatches>=65535 || b->nvertices>UINT32_MAX-batch->vertices || b->nindices>UINT32_MAX-batch->indices-1 || (b->stride && b->stride!=batch->stride))return -1;
    if(c->force_alpha)flags|=1;
    if(c->force_two_sided)flags|=2;
    if(flags>65535)return -1;
    b->stride=batch->stride;w32(row,b->nvertices);w32(row+4,b->nindices);w16(row+8,batch->vertices);w16(row+10,batch->indices);w16(row+12,batch->texture);w16(row+14,flags);
    if(append_bytes(b->section[2],row,16) || append_bytes(b->section[3],batch->vertex_bytes,batch->vertices*batch->stride) || append_bytes(b->section[4],batch->index_bytes,batch->indices*2))return -1;
    if(c->lighting && (!batch->lighting_bytes || append_bytes(c->lighting,batch->lighting_bytes,batch->vertices*20)))return -1;
    b->nvertices+=batch->vertices;b->nindices+=batch->indices;b->nbatches++;
    if(b->nindices&1){unsigned char zero[2]={0};if(append_bytes(b->section[4],zero,2))return -1;b->nindices++;}
    if(batch->texture!=65535){if(batch->texture>=JGC_MAX_TEXTURES)return -1;b->used[batch->texture]=1;}return 0;
}
static int jgc_node(const JgrModelNode *node,const double world[16],unsigned ordinal,int parent,void *opaque)
{
    JgrJgcBuild *b=opaque;const JgrJgcCharacter *c=b->current;JgrModelOptions options=c->model;unsigned char row[48];unsigned first=b->nbatches;
    if(b->nbones>=32767 || node->eval>65535)return -1;
    options.owner=b->legacy?-1:(int)ordinal;options.cache_vertices=1;memcpy(options.bind,world,128);
    /* Only legacy vertices consume inverse_bind. A finite translation has
     * a finite inverse by construction; avoid solving it merely to validate
     * an owner-local node. Keep the original solver/validation for every
     * other matrix, including singular or nonfinite source transforms. */
    int translation=options.owner>=0;
    for(unsigned i=0;i<16 && translation;i++){
        if(i==3 || i==7 || i==11)translation=isfinite(world[i]);
        else translation=world[i]==((i%5)==0?1.0:0.0);
    }
    if(!translation && jgr_model_inverse(options.inverse_bind,world))return -1;
    if(node->model && jgr_model_mesh(b->cache,b->disc,&c->source,c->base,node->model,&options,jgc_emit,b))return -1;
    int global_parent=parent<0?-1:parent+(int)b->bone_start;w16(row,(unsigned)global_parent);w16(row+2,node->eval);
    for(unsigned i=0;i<3;i++){wf(row+4+4*i,node->position[i]);w32(row+16+4*i,(uint32_t)node->angle[i]);wf(row+28+4*i,node->scale[i]);}
    w32(row+40,first);w32(row+44,b->nbatches-first);if(append_bytes(b->section[1],row,48))return -1;b->nbones++;return 0;
}
static int source_span(const JgrDiscFile *f,uint32_t base,uint32_t address,unsigned size,uint32_t *off)
{if(address<base || address-base>f->size || size>f->size-(address-base))return -1;*off=address-base;return 0;}
static int key_stream_uncached(JgrJgcBuild *b,const JgrDiscFile *f,uint32_t at,unsigned size,unsigned alignment,uint32_t *result)
{
    unsigned char data[4096],existing[4096];uint32_t hash=UINT32_MAX,append_at;
    if(at>f->size || size>f->size-at || position(b->section[7],&append_at))return -1;
    if(!b->legacy){
        for(unsigned n=0;n<size;){unsigned bytes=size-n<sizeof data?size-n:sizeof data;if(jgr_disc_read(b->disc,f,at+n,data,bytes))return -1;hash=jgr_crc32_add(hash,data,bytes);n+=bytes;}
        unsigned slot=((unsigned)hash^alignment)&(JGC_KEY_HASH-1);
        while(b->key_hash[slot]){
            KeyRecord *key=b->keys+b->key_hash[slot]-1;
            if(key->hash==hash && key->size==size && key->alignment==alignment){int equal=1;
                for(unsigned n=0;n<size;){unsigned bytes=size-n<sizeof data?size-n:sizeof data;if(jgr_disc_read(b->disc,f,at+n,data,bytes) || file_read(b->section[7],key->offset+n,existing,bytes))return -1;if(memcmp(data,existing,bytes)){equal=0;break;}n+=bytes;}
                if(fseek(b->section[7],(long)append_at,SEEK_SET))return -1;
                if(equal){*result=key->offset;return 0;}
            }slot=(slot+1)&(JGC_KEY_HASH-1);
        }
        if(b->key_count>=JGC_MAX_KEYS)return -1;
        if(b->key_count==b->key_capacity){unsigned cap=b->key_capacity?2*b->key_capacity:256;KeyRecord *records=realloc(b->keys,cap*sizeof *records);if(!records)return -1;b->keys=records;b->key_capacity=cap;}
        if(align_file(b->section[7],alignment) || position(b->section[7],result))return -1;
        KeyRecord *key=b->keys+b->key_count;key->hash=hash;key->offset=*result;key->size=size;key->alignment=alignment;b->key_hash[slot]=++b->key_count;
    }else if(align_file(b->section[7],alignment) || position(b->section[7],result))return -1;
    for(unsigned n=0;n<size;){unsigned bytes=size-n<sizeof data?size-n:sizeof data;if(jgr_disc_read(b->disc,f,at+n,data,bytes) || append_bytes(b->section[7],data,bytes))return -1;n+=bytes;}return 0;
}
static int key_stream(JgrJgcBuild *b,const JgrDiscFile *f,uint32_t at,unsigned size,unsigned alignment,uint32_t *result)
{
    if(at>f->size || size>f->size-at || f->offset>UINT64_MAX-at)return -1;
    SourceKey *entry=NULL;
    uint64_t source=f->offset+at;
    /* The disc is immutable throughout conversion. A repeated source extent
       has already passed content interning; avoid hashing and disk comparison
       again. Collisions and allocation failure retain the exact old path. */
    if(b->source_keys){
        uint32_t key=(uint32_t)source^(uint32_t)(source>>32)^(size*2654435761u)^alignment;
        key^=key>>16;key*=2246822519u;key^=key>>13;
        entry=b->source_keys+(key&(JGC_SOURCE_CACHE-1));
        if(entry->alignment==alignment && entry->source==source && entry->size==size){*result=entry->output;return 0;}
    }
    int status=key_stream_uncached(b,f,at,size,alignment,result);
    if(!status && entry)*entry=(SourceKey){source,size,alignment,*result};
    return status;
}
int jgr_jgc_add(JgrJgcBuild *b,const JgrJgcCharacter *c)
{
    unsigned char row[48];double identity[16];if(!b || !c || b->failed || b->nchars>=4096 || c->motion_count>4096 || c->model.texture_base>JGC_MAX_TEXTURES || c->model.texture_count>JGC_MAX_TEXTURES-c->model.texture_base)return -1;
    b->failed=1;b->current=c;b->bone_start=b->nbones;unsigned first_motion=b->nmotions;
    int can_reuse=reuse_eligible(b,c);
#ifdef __PSP__
    /* Keep the same workspace reserve as the texture/scratch phase. The
     * optional collector can briefly own both its vector and final record. */
    if(can_reuse && jgr_scratch_heap_available()<6u*1024u*1024u+
       2u*ROW_REUSE_NODES*sizeof(ReuseNode)+sizeof(ReuseRow)+512u*sizeof(uint16_t))can_reuse=0;
#endif
    ReuseRow *candidate=can_reuse?reuse_collect(b->disc,c):NULL,*match=NULL;
    if(candidate){
        for(ReuseRow *r=b->rows;r;r=r->next)if(reuse_options_equal(candidate,r) && reuse_payload_equal(b,candidate,r)){match=r;break;}
    }else reuse_free(b); /* Optional metadata must not pin memory after OOM. */
    unsigned batch_start=b->nbatches,vertex_start=b->nvertices,index_start=b->nindices;
    if(match){int status=reuse_emit(b,match);free(candidate);candidate=NULL;if(status)return -1;}
    else{
        jgr_model_cache_clear(b->cache);jgr_model_identity(identity);
        if(walk_nodes(b->disc,&c->source,c->base,c->root,identity,b->legacy,1,jgc_node,b,!!c->collision_tree)){free(candidate);return -1;}
        if(candidate){candidate->bone=b->bone_start;candidate->batches=batch_start;candidate->vertices=vertex_start;candidate->indices=index_start;
            candidate->nbatches=b->nbatches-batch_start;candidate->nvertices=b->nvertices-vertex_start;candidate->nindices=b->nindices-index_start;candidate->stride=b->stride;
            reuse_store(b,candidate);candidate=NULL;}
    }
    unsigned bones=b->nbones-b->bone_start;if(!bones)return -1;
    for(unsigned i=0;i<c->motion_count;i++){
        uint32_t address,off;const JgrDiscFile *motion=c->motion_source.size?&c->motion_source:&c->source;uint32_t motion_base=c->motion_source.size?c->motion_base:c->base;
        if(c->motion_headers)address=c->motion_headers[i];
        else{if(c->motion_table>UINT32_MAX-i*4 || source_span(&c->source,c->base,c->motion_table+i*4,4,&off) || jgr_disc_read(b->disc,&c->source,off,row,4))return -1;address=u32(row);}
        if(!address)break;
        if(b->nmotions>=65535 || source_span(motion,motion_base,address,8,&off) || jgr_disc_read(b->disc,motion,off,row,8))return -1;
        unsigned frames=u32(row)>>16;if(!frames)frames=1;uint32_t md=u32(row+4),channels;
        if(source_span(motion,motion_base,md,bones*16,&off) || position(b->section[9],&channels))return -1;
        w32(row,frames);w32(row+4,channels);if(append_bytes(b->section[6],row,8))return -1;
        for(unsigned bone=0;bone<bones;bone++){
            unsigned char source[16],channel[24];if(jgr_disc_read(b->disc,motion,off+bone*16,source,16))return -1;memset(channel,0,sizeof channel);
            for(unsigned type=0;type<3;type++){
                uint32_t pointer=u32(source+4+4*type),keyoff,stored;unsigned stride=type==1?6:12;
                if(pointer){if(source_span(motion,motion_base,pointer,frames*stride,&keyoff) || key_stream(b,motion,keyoff,frames*stride,type==1?2:4,&stored))return -1;w32(channel+8*type,frames);w32(channel+8*type+4,stored);}
            }if(append_bytes(b->section[9],channel,24))return -1;
        }b->nmotions++;
    }
    memset(row,0,sizeof row);w32(row,b->bone_start);w32(row+4,bones);w32(row+8,first_motion);w32(row+12,b->nmotions-first_motion);
    for(unsigned i=0;i<4;i++){if(!isfinite(c->placement[i]))return -1;wf(row+16+4*i,c->placement[i]);}
    unsigned length=0;while(length<15 && c->name[length])length++;memcpy(row+32,c->name,length);
    if(append_bytes(b->section[0],row,48))return -1;
    b->nchars++;b->failed=0;b->current=NULL;return 0;
}
int jgr_jgc_keep_texture(JgrJgcBuild *b,unsigned index)
{if(!b || b->failed || index>=JGC_MAX_TEXTURES)return -1;b->used[index]=1;return 0;}
int jgr_jgc_next_texture(const JgrJgcBuild *b,unsigned start)
{if(!b)return -1;for(unsigned i=start;i<JGC_MAX_TEXTURES;i++)if(b->used[i])return (int)i;return -1;}
int jgr_jgc_finish(JgrJgcBuild *b,FILE *out,JgrJgcTextureEmit texture,void *user)
{
    uint32_t size[10],offset[9];uint16_t *mapping=NULL;unsigned char *alpha=NULL,record[68];unsigned textures=0;int result=-1;
    if(!b || !out || b->failed || !b->nchars)return -1;
    b->failed=1;
    /* Texture decoding has a separate peak allocation. Geometry cache/key
     * lookup metadata is no longer needed once all characters are assembled. */
    reuse_free(b);jgr_model_cache_free(b->cache);b->cache=NULL;
    free(b->keys);b->keys=NULL;free(b->key_hash);b->key_hash=NULL;
    free(b->source_keys);b->source_keys=NULL;
    mapping=calloc(JGC_MAX_TEXTURES,sizeof *mapping);alpha=calloc(JGC_MAX_TEXTURES,1);if(!mapping || !alpha)goto done;
    for(unsigned i=0;i<JGC_MAX_TEXTURES;i++)if(b->used[i]){
        JgrJgcTexture t;memset(&t,0,sizeof t);if(!texture || texture(i,b->section[8],&t,user) || !t.width || !t.height || t.width>1024 || t.height>1024 || (t.width&15) || (t.height&7) || (t.flags&~1))goto done;
        uint32_t bytes;if(position(b->section[8],&bytes) || !span(t.clut,1024,1,bytes) || !span(t.pixels,t.width,t.height,bytes))goto done;
        mapping[i]=(uint16_t)textures;alpha[textures]=(unsigned char)t.flags;w16(record,t.width);w16(record+2,t.height);w16(record+4,t.flags);w16(record+6,0);w32(record+8,t.clut);w32(record+12,t.pixels);
        if(append_bytes(b->section[5],record,16))goto done;
        textures++;
    }
    for(unsigned i=0;i<10;i++)if(position(b->section[i],size+i))goto done;
    if(size[6]>UINT32_MAX-size[9])goto done;
    size[6]+=size[9];uint32_t at=80;
    for(unsigned i=0;i<9;i++){offset[i]=at;if(size[i]>UINT32_MAX-at-15)goto done;at=(at+size[i]+15)&~15u;}
    memcpy(record,b->legacy?"JGC1":b->stride==40?"JGC3":"JGC2",4);
    uint32_t counts[]={b->nchars,b->nbones,b->nbatches,b->nvertices,b->nindices,textures,b->nmotions};for(unsigned i=0;i<7;i++)w32(record+4+4*i,counts[i]);for(unsigned i=0;i<9;i++)w32(record+32+4*i,offset[i]);
    if(fseek(out,0,SEEK_SET) || append_bytes(out,record,68))goto done;
    for(unsigned section=0;section<9;section++){
        if(align_file(out,16))goto done;
        if(section==2){for(unsigned i=0;i<b->nbatches;i++){
            if(file_read(b->section[2],i*16,record,16))goto done;
            unsigned original=u16(record+12),index=original==65535?0:mapping[original],flags=u16(record+14);
            if(original!=65535 && alpha[index])flags|=1;
            w16(record+12,index);w16(record+14,flags);if(append_bytes(out,record,16))goto done;
        }}else if(section==5){for(unsigned i=0;i<textures;i++){
            if(file_read(b->section[5],i*16,record,16))goto done;
            w32(record+8,u32(record+8)+offset[8]);w32(record+12,u32(record+12)+offset[8]);if(append_bytes(out,record,16))goto done;
        }}else if(section==6){for(unsigned i=0;i<b->nmotions;i++){
            if(file_read(b->section[6],i*8,record,8))goto done;
            w32(record+4,u32(record+4)+offset[6]+b->nmotions*8);if(append_bytes(out,record,8))goto done;
        }if(file_copy(b->section[9],out,0,size[9]))goto done;
        }else if(file_copy(b->section[section],out,0,size[section]))goto done;
    }if(fflush(out))goto done;result=0;
done:free(mapping);free(alpha);return result;
}

int jgr_jgc_resolve_play(JgrDisc *disc,const JgrDiscFile *source,const JgrDiscFile *textures,unsigned lod,unsigned texture_base,uint16_t map[512],JgrJgcCharacter *c)
{
    unsigned char head[16];uint32_t *offsets=NULL;unsigned ntextures=0,roots=0;int result=-1;
    if(!disc || !source || !textures || !map || !c || lod>=64 || jgr_disc_read(disc,source,0,head,12) || memcmp(head,"PLAY",4))return -1;
    uint32_t table=u32(head+4);c->root=c->motion_table=0;c->base=0;c->source=*source;c->motion_source.size=0;c->motion_headers=NULL;
    for(unsigned i=0;i<256;i++){
        if(table>source->size || 8>source->size-table || jgr_disc_read(disc,source,table,head,8))return -1;
        uint32_t kind=u32(head),address=u32(head+4);if(!kind)break;
        if(kind==1 && roots++==lod)c->root=address;
        if(kind==3)c->motion_table=address;
        table+=8;if(i==255)return -1;
    }if(!c->root || !c->motion_table || c->root>=source->size || c->motion_table>=source->size)return -1;
    offsets=malloc(JGC_MAX_TEXTURES*sizeof *offsets);if(!offsets)return -1;
    Reader r;memset(&r,0,sizeof r);r.disc=disc;r.file=textures;
    for(uint32_t at=0;at<textures->size && textures->size-at>=4;){
        if(read_at(&r,at,head,4))goto done;
        if(!memcmp(head,"GBIX",4)){if(ntextures>=JGC_MAX_TEXTURES || textures->size-at<8)goto done;offsets[ntextures++]=at;at+=8;}else at++;
    }
    if(!ntextures || texture_base>JGC_MAX_TEXTURES || ntextures>JGC_MAX_TEXTURES-texture_base || jgr_disc_read(disc,textures,0,head,12))goto done;
    c->model.texture_base=texture_base;c->model.texture_count=ntextures;c->model.texture_map=NULL;c->model.texture_slots=0;
    if(!memcmp(head,"PTEX",4)){
        uint32_t list=u32(head+4),node_table=u32(head+8),selected;
        if(node_table>textures->size || textures->size-node_table<8 || read_at(&r,node_table+4,head,4))goto done;
        selected=u32(head);
        if(selected!=UINT32_MAX && selected){int found=0;
            for(unsigned i=0;i<256;i++){
                if(read_at(&r,list,head,8))goto done;
                uint32_t pointer=u32(head),array=u32(head+4);if(!pointer)break;
                if(pointer==selected){if(read_at(&r,pointer,head,8))goto done;unsigned count=u32(head+4);if(count>512)goto done;
                    for(unsigned j=0;j<count;j++){
                        if(array>UINT32_MAX-j*4 || read_at(&r,array+j*4,head,4))goto done;
                        uint32_t wanted=u32(head);unsigned lo=0,hi=ntextures;
                        while(lo<hi){unsigned mid=lo+(hi-lo)/2;if(offsets[mid]<wanted)lo=mid+1;else hi=mid;}
                        map[j]=(uint16_t)(lo<ntextures && offsets[lo]==wanted?lo:0);
                    }if(count){c->model.texture_map=map;c->model.texture_slots=count;}found=1;break;
                }if(list>UINT32_MAX-8)goto done;list+=8;
            }/* A missing texlist matches the host's ordinary file-order map. */
            (void)found;
        }
    }result=0;
done:free(offsets);return result;
}

static int light_number(JgrDisc *d,const JgrDiscFile *file,uint32_t offset,double *out)
{
    unsigned char raw[4];if(jgr_disc_read(d,file,offset,raw,4))return -1;double value=number(raw);if(!isfinite(value))return -1;
    for(unsigned digits=1;digits<=9;digits++){char text[64];if(snprintf(text,sizeof text,"%.*g",(int)digits,value)<0)return -1;double candidate=strtod(text,NULL);float f=(float)candidate;uint32_t bits;memcpy(&bits,&f,4);if(bits==u32(raw)){*out=candidate;return 0;}}
    return -1;
}
int jgr_model_baked_light(JgrDisc *d,JgrModelLight *p)
{
    JgrDiscFile executable;unsigned char b[4];if(!d || !p || jgr_disc_find(d,"1ST_READ.BIN",&executable) || jgr_disc_read(d,&executable,0x1f74c,b,2) || (u16(b)&0xf0ff)!=0xf09d)return -1;
    memset(p,0,sizeof *p);double axis;if(light_number(d,&executable,0x1f910,&axis) || light_number(d,&executable,0x1f914,p->direction+1) || light_number(d,&executable,0x1f90c,&p->diffuse) || light_number(d,&executable,0x1f904,&p->ambient))return -1;
    p->direction[0]=p->direction[2]=axis;for(unsigned i=0;i<3;i++){p->rgb[i]=1;p->cel_direction[i]=axis;}
    uint32_t offsets[2][3]={{0x1f924,0x1f924,0x1f924},{0x1f920,0x1f920,0x1f91c}};
    for(unsigned i=0;i<2;i++)for(unsigned k=0;k<3;k++){if(jgr_disc_read(d,&executable,offsets[i][k],b,4) || u32(b)>255)return -1;p->cel_colors[i][k]=u32(b);}
    return 0;
}
