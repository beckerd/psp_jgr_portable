/* Conservative certified opaque coverage, rebuilt from generated JGS/JRS.
 * Plane islands are kept together as in the tested shipping recipe. */
#define VIS_BYTES (144u*1024u)
#define VIS_MAX_TRIANGLES 65536u
#define VIS_MAX_PATCHES 16384u
typedef struct {double p[3];} VPoint;
typedef struct {VPoint p[3];uint32_t next;} VTriangle;
typedef struct {int64_t key[4];unsigned first,last;} VPlane;
typedef struct {VPoint p;int64_t key[3];unsigned degree,adj[2];} VVertex;
typedef struct {unsigned a,b,count,forward,reverse;} VEdge;
typedef struct {double area;unsigned owner,count,order;VPoint points[4];} VPatch;
typedef struct {float center[3],extent[3];unsigned first,count,flags,owner,instance,pair,moving;} VObject;
typedef struct {unsigned vo,io,nv,ni,texture,flags;} VBatch;
typedef struct {Derived d;VObject *objects;VBatch *batches;double *matrices;VPatch *patches;unsigned nmatrices,npatches;float *spheres;} Visibility;
static double vdot(VPoint a,VPoint b){return a.p[0]*b.p[0]+a.p[1]*b.p[1]+a.p[2]*b.p[2];}
static VPoint vsub(VPoint a,VPoint b){VPoint p;for(unsigned i=0;i<3;i++)p.p[i]=a.p[i]-b.p[i];return p;}
static VPoint vcross(VPoint a,VPoint b)
{VPoint p={{a.p[1]*b.p[2]-a.p[2]*b.p[1],a.p[2]*b.p[0]-a.p[0]*b.p[2],a.p[0]*b.p[1]-a.p[1]*b.p[0]}};return p;}
static double vlength(VPoint a){return sqrt(vdot(a,a));}
static VPoint vdivide(VPoint a,double n){for(unsigned i=0;i<3;i++)a.p[i]/=n;return a;}
static float vfadd(float a,float b){volatile float out=a+b;return out;}
static float vfsub(float a,float b){volatile float out=a-b;return out;}
static float vfmul(float a,float b){volatile float out=a*b;return out;}
static float vfnorm(const float p[3])
{return sqrtf(vfadd(vfadd(vfmul(p[0],p[0]),vfmul(p[1],p[1])),vfmul(p[2],p[2])));}
static int vfile_read(FILE *f,uint32_t size,uint32_t at,void *p,uint32_t n)
{return at>size || n>size-at || fseek(f,at,SEEK_SET) || fread(p,1,n,f)!=n ? -1:0;}
static int vis_load(Visibility *v,FILE *jgs,FILE *jrs,unsigned stage)
{
    if(der_open(&v->d,jgs) || (!jrs && stage))return -1;
    Derived *d=&v->d;
    v->objects=calloc(d->no,sizeof *v->objects);v->batches=calloc(d->nb,sizeof *v->batches);v->spheres=calloc(d->no*4,sizeof(float));
    if(!v->objects || !v->batches || !v->spheres)return -1;
    for(unsigned i=0;i<d->no;i++) {
        unsigned char row[32];if(der_read(d,d->oo+i*32,row,32))return -1;VObject *o=v->objects+i;
        for(unsigned j=0;j<4;j++){v->spheres[i*4+j]=(float)der_real(row+j*4);if(!isfinite(v->spheres[i*4+j]))return -1;}
        o->first=der_word(row+16);o->count=der_word(row+20);o->flags=der_word(row+24);o->owner=der_word(row+28);
        if(o->first>d->nb || o->count>d->nb-o->first)return -1;
    }
    for(unsigned i=0;i<d->nb;i++) {
        unsigned char row[16];if(der_read(d,d->ob+i*16,row,16))return -1;VBatch *b=v->batches+i;
        b->vo=der_word(row);b->io=der_word(row+4);b->nv=der_half(row+8);b->ni=der_half(row+10);b->texture=der_half(row+12);b->flags=der_half(row+14);
        if(b->vo>d->nv || b->nv>d->nv-b->vo || b->io>d->ni || b->ni>d->ni-b->io || b->ni%3)return -1;
    }
    uint32_t instance_at=(d->ob+d->nb*16+15)&~15u;unsigned char instance_header[16];unsigned has_instances=0;
    if(instance_at+16<=d->ov) {
        if(der_read(d,instance_at,instance_header,16))return -1;
        has_instances=!memcmp(instance_header,"JGI1",4);
    }
    if(has_instances) {
        unsigned no=der_word(instance_header+4),count=der_word(instance_header+8),reserved=der_word(instance_header+12);
        uint32_t records=(instance_at+16+d->no*2+15)&~15u;
        if(no!=d->no || !count || count>65535 || reserved || records>d->ov || count*64>d->ov-records)return -1;
        v->matrices=calloc(count*12,sizeof(double));if(!v->matrices)return -1;v->nmatrices=count;
        for(unsigned i=0;i<count;i++) {
            unsigned char row[64];if(der_read(d,records+i*64,row,64))return -1;
            for(unsigned j=0;j<16;j++){double value=der_real(row+j*4);if(!isfinite(value) || (j==15 && value<0))return -1;if(j<12)v->matrices[i*12+j]=value;}
        }
        unsigned char *used=calloc(d->nb,1);if(!used)return -1;
        for(unsigned i=0;i<d->no;i++) {
            unsigned char id[2];if(der_read(d,instance_at+16+i*2,id,2)){free(used);return -1;}
            VObject *o=v->objects+i;o->instance=der_half(id);
            if(o->instance>count || !!o->instance!=!!(o->flags&4)){free(used);return -1;}
            if(o->instance)for(unsigned b=o->first;b<o->first+o->count;b++){if(used[b]){free(used);return -1;}used[b]=1;}
        }
        free(used);
    }else for(unsigned i=0;i<d->no;i++)if(v->objects[i].flags&4)return -1;
    if(!jrs)return 0;
    if(fseek(jrs,0,SEEK_END))return -1;
    long length=ftell(jrs);if(length<32 || (unsigned long)length>16u*1024u*1024u)return -1;
    unsigned char header[32];if(vfile_read(jrs,(unsigned)length,0,header,32) || memcmp(header,"JRS1",4))return -1;
    unsigned sectors=der_word(header+8),records=der_word(header+12),near=der_word(header+16);
    if(der_word(header+4)!=stage || sectors>65536 || records>65535 || (uint64_t)32+16u*sectors+32u*records!=(unsigned long)length || der_word(header+20)!=d->no || der_word(header+24)!=d->nb || der_word(header+28)!=d->key || near>d->no)return -1;
    unsigned char *moving=calloc(records+1,1);if(!moving)return -1;
    for(unsigned i=0;i<records;i++) {
        unsigned char row[32];if(vfile_read(jrs,(unsigned)length,32+16*sectors+32*i,row,32)){free(moving);return -1;}
        unsigned first=der_half(row),count=der_half(row+2),far=der_half(row+4),far_count=der_half(row+6),kind=der_half(row+22);double distance=der_real(row+24);
        if(kind!=9 && (!count || !far_count))continue;
        if(!count || first>d->no || count>d->no-first || far<near || far>d->no || far_count>d->no-far || !isfinite(distance) || distance<0 || (far_count && distance<=0) || (kind!=9 && first+count>near)){free(moving);return -1;}
        if(kind==9)moving[i+1]=1;
        for(unsigned lod=0;lod<2;lod++) {
            unsigned start=lod ? far:first,n=lod ? far_count:count;
            for(unsigned owner=start;owner<start+n;owner++) {
                VObject *o=v->objects+owner;if((o->flags&2)!=(lod?2u:0u) || o->owner!=i+1){free(moving);return -1;}
                if(kind==9)o->moving=1;else o->pair=i+1;
            }
        }
    }
    for(unsigned i=0;i<d->no;i++)if(v->objects[i].owner<=records && moving[v->objects[i].owner] && !v->objects[i].moving){free(moving);return -1;}
    free(moving);return 0;
}
static int vis_batch(Visibility *v,const VObject *object,const VBatch *batch,VPoint *points,unsigned short *indices)
{
    unsigned char block[24*256];Derived *d=&v->d;
    for(unsigned at=0;at<batch->nv;) {
        unsigned n=batch->nv-at;if(n>256)n=256;
        if(der_read(d,d->ov+(batch->vo+at)*24,block,n*24))return -1;
        for(unsigned i=0;i<n;i++) {
            VPoint p;for(unsigned j=0;j<3;j++){p.p[j]=der_real(block+i*24+12+j*4);if(!isfinite(p.p[j]))return -1;}
            if(object->instance) {
                const double *m=v->matrices+(object->instance-1)*12;
                for(unsigned j=0;j<3;j++)points[at+i].p[j]=m[j*4]*p.p[0]+m[j*4+1]*p.p[1]+m[j*4+2]*p.p[2]+m[j*4+3];
            }else points[at+i]=p;
        }
        at+=n;
    }
    if(indices)for(unsigned at=0;at<batch->ni;) {
        unsigned n=batch->ni-at;if(n>sizeof block/2)n=sizeof block/2;
        if(der_read(d,d->oi+(batch->io+at)*2,block,n*2))return -1;
        for(unsigned i=0;i<n;i++){unsigned index=der_half(block+i*2);if(index>=batch->nv)return -1;indices[at+i]=(unsigned short)index;}at+=n;
    }
    return 0;
}
static int vis_boxes(Visibility *v,unsigned char *boxes)
{
    unsigned max_vertices=1;for(unsigned i=0;i<v->d.nb;i++)if(v->batches[i].nv>max_vertices)max_vertices=v->batches[i].nv;
    VPoint *points=malloc(max_vertices*sizeof *points);if(!points)return -1;
    for(unsigned i=0;i<v->d.no;i++) {
        VObject *o=v->objects+i;double low[3]={0},high[3]={0};unsigned count=0;
        for(unsigned j=o->first;j<o->first+o->count;j++) {
            VBatch *b=v->batches+j;if(vis_batch(v,o,b,points,NULL)){free(points);return -1;}
            for(unsigned k=0;k<b->nv;k++) {
                for(unsigned axis=0;axis<3;axis++){double p=points[k].p[axis];if(!count || p<low[axis])low[axis]=p;if(!count || p>high[axis])high[axis]=p;}count++;
            }
        }
        for(unsigned axis=0;axis<3;axis++) {
            o->center[axis]=(float)((low[axis]+high[axis])*.5);double a=high[axis]-o->center[axis],b=o->center[axis]-low[axis];o->extent[axis]=(float)((a>b ? a:b)+.1);
            if(low[axis]<(double)vfsub(o->center[axis],o->extent[axis]) || high[axis]>(double)vfadd(o->center[axis],o->extent[axis])){free(points);return -1;}
            der_float(boxes+i*24+axis*4,o->center[axis]);der_float(boxes+i*24+12+axis*4,o->extent[axis]);
        }
    }
    free(points);return 0;
}
static uint32_t vis_key_hash(const int64_t *key,unsigned n)
{uint32_t hash=2166136261u;for(unsigned i=0;i<n;i++){uint64_t v=(uint64_t)key[i];hash=(hash^(uint32_t)v)*16777619u;hash=(hash^(uint32_t)(v>>32))*16777619u;}return hash;}
static int vis_quantize(double value,double scale,int64_t *out)
{if(!isfinite(value) || fabs(value)>1.e10)return -1;*out=(int64_t)nearbyint(value*scale);return 0;}
static int vertex_compare(const VVertex *a,const VVertex *b)
{for(unsigned i=0;i<3;i++){if(a->key[i]<b->key[i])return -1;if(a->key[i]>b->key[i])return 1;}return 0;}
static int vis_certificate(Visibility *v,unsigned owner,const VTriangle *triangles,unsigned first)
{
    unsigned triangle_count=0;for(unsigned i=first;i!=UINT_MAX;i=triangles[i].next)if(++triangle_count>VIS_MAX_TRIANGLES)return -1;
    unsigned vertices_limit=triangle_count*3,edge_limit=triangle_count*3,slots=16;
    while(slots<vertices_limit*2)slots*=2;
    VVertex *vertices=calloc(vertices_limit,sizeof *vertices);VEdge *edges=calloc(edge_limit,sizeof *edges);
    unsigned *vertex_hash=calloc(slots,sizeof *vertex_hash),*edge_hash=calloc(slots,sizeof *edge_hash),*order=malloc(vertices_limit*sizeof *order),*keep=malloc(vertices_limit*sizeof *keep);
    int result=0;unsigned nv=0,ne=0;double area=0;
    if(!vertices || !edges || !vertex_hash || !edge_hash || !order || !keep){result=-1;goto done;}
    const VTriangle *initial=triangles+first;VPoint normal=vcross(vsub(initial->p[1],initial->p[0]),vsub(initial->p[2],initial->p[0]));normal=vdivide(normal,vlength(normal));double distance=-vdot(normal,initial->p[0]);
    for(unsigned at=first;at!=UINT_MAX;at=triangles[at].next) {
        const VTriangle *t=triangles+at;unsigned ids[3];
        for(unsigned i=0;i<3;i++) {
            if(fabs(vdot(t->p[i],normal)+distance)>.01)goto done;
            int64_t key[3];for(unsigned j=0;j<3;j++)if(vis_quantize(t->p[i].p[j],1000,key+j)){result=-1;goto done;}
            unsigned slot=vis_key_hash(key,3)&(slots-1);
            while(vertex_hash[slot] && memcmp(vertices[vertex_hash[slot]-1].key,key,sizeof key))slot=(slot+1)&(slots-1);
            unsigned id;if(vertex_hash[slot])id=vertex_hash[slot]-1;else {id=nv++;vertex_hash[slot]=id+1;memcpy(vertices[id].key,key,sizeof key);}
            vertices[id].p=t->p[i];ids[i]=id;
        }
        VPoint cross=vcross(vsub(t->p[1],t->p[0]),vsub(t->p[2],t->p[0]));area+=vlength(cross)*.5;
        if(vdot(cross,normal)<0){unsigned tmp=ids[0];ids[0]=ids[2];ids[2]=tmp;}
        for(unsigned i=0;i<3;i++) {
            unsigned a=ids[i],b=ids[(i+1)%3],forward=vertex_compare(vertices+a,vertices+b)<=0;
            if(!forward){unsigned tmp=a;a=b;b=tmp;}unsigned slot=((a*2654435761u)^b)&(slots-1);
            while(edge_hash[slot] && (edges[edge_hash[slot]-1].a!=a || edges[edge_hash[slot]-1].b!=b))slot=(slot+1)&(slots-1);
            unsigned id;if(edge_hash[slot])id=edge_hash[slot]-1;else {id=ne++;edge_hash[slot]=id+1;edges[id].a=a;edges[id].b=b;}
            VEdge *e=edges+id;e->count++;if(forward)e->forward++;else e->reverse++;
        }
    }
    unsigned boundary=0,start=UINT_MAX;
    for(unsigned i=0;i<ne;i++) {
        VEdge *e=edges+i;if(e->count>2 || (e->count==2 && (e->forward!=1 || e->reverse!=1)))goto done;
        if(e->count==1) {
            if(start==UINT_MAX)start=e->a;
            if(vertices[e->a].degree==2 || vertices[e->b].degree==2)goto done;
            vertices[e->a].adj[vertices[e->a].degree++]=e->b;vertices[e->b].adj[vertices[e->b].degree++]=e->a;
        }
    }
    for(unsigned i=0;i<nv;i++)if(vertices[i].degree){if(vertices[i].degree!=2)goto done;boundary++;}
    if(!boundary || start==UINT_MAX)goto done;
    unsigned count=1,previous=UINT_MAX,current=start;order[0]=start;
    for(unsigned i=0;i<boundary;i++) {
        unsigned next=vertices[current].adj[0]!=previous ? vertices[current].adj[0]:vertices[current].adj[1];
        if(next==start)break;
        order[count++]=next;previous=current;current=next;
    }
    if(count!=boundary)goto done;
    while(count>3) {
        unsigned n=0;
        for(unsigned i=0;i<count;i++) {
            VPoint a=vertices[order[(i+count-1)%count]].p,b=vertices[order[i]].p,c=vertices[order[(i+1)%count]].p;
            if(vlength(vcross(vsub(b,a),vsub(c,b)))>.01*vlength(vsub(c,a)))keep[n++]=order[i];
        }
        if(n==count || n<3)break;
        memcpy(order,keep,n*sizeof *order);count=n;
    }
    if(count<3 || count>4)goto done;
    int positive=1,negative=1;double polygon_area=0;
    for(unsigned i=0;i<count;i++) {
        VPoint a=vertices[order[(i+count-1)%count]].p,b=vertices[order[i]].p,c=vertices[order[(i+1)%count]].p;
        double cross=vdot(vcross(vsub(b,a),vsub(c,b)),normal);if(cross<=0)positive=0;if(cross>=0)negative=0;
    }
    if(!positive && !negative)goto done;
    for(unsigned i=1;i+1<count;i++)polygon_area+=fabs(vdot(vcross(vsub(vertices[order[i]].p,vertices[order[0]].p),vsub(vertices[order[i+1]].p,vertices[order[0]].p)),normal))*.5;
    if(fabs(polygon_area-area)>(area*.0001>.01 ? area*.0001:.01) || area<100)goto done;
    if(v->npatches==VIS_MAX_PATCHES){result=-1;goto done;}
    VPatch *patch=v->patches+v->npatches;patch->order=v->npatches++;patch->area=area;patch->owner=owner;patch->count=count;
    for(unsigned i=0;i<count;i++)patch->points[i]=vertices[order[i]].p;
 done:free(vertices);free(edges);free(vertex_hash);free(edge_hash);free(order);free(keep);return result;
}
static int vis_patches(Visibility *v)
{
    v->patches=calloc(VIS_MAX_PATCHES,sizeof *v->patches);if(!v->patches)return -1;
    for(unsigned owner=0;owner<v->d.no;owner++) {
        VObject *o=v->objects+owner;if(o->flags&1 || o->moving)continue;
        unsigned maximum=0,mv=1,mi=1;
        for(unsigned i=o->first;i<o->first+o->count;i++) {
            VBatch *b=v->batches+i;if(b->flags&1)continue;maximum+=b->ni/3;if(b->nv>mv)mv=b->nv;if(b->ni>mi)mi=b->ni;
        }
        if(!maximum)continue;
        if(maximum>VIS_MAX_TRIANGLES)return -1;
        VTriangle *triangles=malloc(maximum*sizeof *triangles);VPlane *planes=malloc(maximum*sizeof *planes);
        VPoint *points=malloc(mv*sizeof *points);unsigned short *indices=malloc(mi*sizeof *indices);unsigned slots=16;
        while(slots<maximum*2)slots*=2;
        unsigned *lookup=calloc(slots,sizeof *lookup),nt=0,np=0;int result=-1;
        if(!triangles || !planes || !points || !indices || !lookup)goto object_done;
        for(unsigned i=o->first;i<o->first+o->count;i++) {
            VBatch *b=v->batches+i;if(b->flags&1)continue;if(vis_batch(v,o,b,points,indices))goto object_done;
            for(unsigned j=0;j<b->ni;j+=3) {
                VPoint p[3]={points[indices[j]],points[indices[j+1]],points[indices[j+2]]};VPoint normal=vcross(vsub(p[1],p[0]),vsub(p[2],p[0]));double length=vlength(normal);
                if(length<.01)continue;
                normal=vdivide(normal,length);unsigned axis=0;
                for(unsigned k=1;k<3;k++)if(fabs(normal.p[k])>fabs(normal.p[axis]))axis=k;
                if(normal.p[axis]<0)normal=vdivide(normal,-1);
                int64_t key[4];
                for(unsigned k=0;k<3;k++)if(vis_quantize(normal.p[k],1000,key+k))goto object_done;
                if(vis_quantize(-vdot(normal,p[0]),10,key+3))goto object_done;
                unsigned slot=vis_key_hash(key,4)&(slots-1);while(lookup[slot] && memcmp(planes[lookup[slot]-1].key,key,sizeof key))slot=(slot+1)&(slots-1);
                unsigned id;if(lookup[slot]){id=lookup[slot]-1;triangles[planes[id].last].next=nt;planes[id].last=nt;}
                else {id=np++;lookup[slot]=id+1;memcpy(planes[id].key,key,sizeof key);planes[id].first=planes[id].last=nt;}
                memcpy(triangles[nt].p,p,sizeof p);triangles[nt++].next=UINT_MAX;
            }
        }
        for(unsigned i=0;i<np;i++)if(vis_certificate(v,owner,triangles,planes[i].first))goto object_done;
        result=0;
 object_done:free(triangles);free(planes);free(points);free(indices);free(lookup);if(result)return result;
    }
    return 0;
}
static int vis_patch_compare(const void *a,const void *b)
{
    const VPatch *x=a,*y=b;if(x->area!=y->area)return x->area>y->area ? -1:1;
    return x->order<y->order ? -1:x->order>y->order;
}
static int vis_equivalent(const VPatch *a,const VPatch *b)
{
    if(a->count!=b->count)return 0;
    for(unsigned i=0;i<a->count;i++) {
        unsigned j;for(j=0;j<b->count;j++)if(a->points[i].p[0]==b->points[j].p[0] && a->points[i].p[1]==b->points[j].p[1] && a->points[i].p[2]==b->points[j].p[2])break;
        if(j==b->count)return 0;
    }
    return 1;
}
static int vis_inset_amount(const VPatch *patch,float out[4][3],float area[3],double amount)
{
    unsigned count=patch->count;if(count<3 || count>4)return 0;
    VPoint origin=patch->points[0],u=vsub(patch->points[1],origin),normal=vcross(u,vsub(patch->points[2],origin));
    normal=vdivide(normal,vlength(normal));u=vdivide(u,vlength(u));VPoint axis=vcross(normal,u);
    double xy[4][2],inward[4][2],limit[4],result[4][2];
    for(unsigned i=0;i<count;i++){VPoint delta=vsub(patch->points[i],origin);xy[i][0]=vdot(delta,u);xy[i][1]=vdot(delta,axis);}
    for(unsigned i=0;i<count;i++) {
        double x=xy[(i+1)%count][0]-xy[i][0],y=xy[(i+1)%count][1]-xy[i][1],length=sqrt(x*x+y*y);
        if(!length)return 0;
        inward[i][0]=-y/length;inward[i][1]=x/length;
        limit[i]=inward[i][0]*xy[i][0]+inward[i][1]*xy[i][1]+amount;
    }
    for(unsigned i=0;i<count;i++) {
        unsigned previous=(i+count-1)%count;const double *a=inward[previous],*b=inward[i];double det=a[0]*b[1]-a[1]*b[0];
        if(fabs(det)<1.e-6)return 0;
        double d=limit[previous],e=limit[i];
        result[i][0]=(d*b[1]-a[1]*e)/det;result[i][1]=(a[0]*e-d*b[0])/det;
    }
    for(unsigned i=0;i<count;i++)for(unsigned j=0;j<count;j++)if(result[i][0]*inward[j][0]+result[i][1]*inward[j][1]<limit[j]-.0001)return 0;
    for(unsigned i=0;i<count;i++) {
        VPoint delta;for(unsigned k=0;k<3;k++){out[i][k]=(float)(origin.p[k]+result[i][0]*u.p[k]+result[i][1]*axis.p[k]);delta.p[k]=(double)out[i][k]-origin.p[k];}
        double x=vdot(delta,u),y=vdot(delta,axis);
        for(unsigned j=0;j<count;j++)if(x*inward[j][0]+y*inward[j][1]<limit[j]-1)return 0;
    }
    area[0]=area[1]=area[2]=0;
    for(unsigned i=1;i+1<count;i++) {
        float a[3],b[3];for(unsigned j=0;j<3;j++){a[j]=vfsub(out[i][j],out[0][j]);b[j]=vfsub(out[i+1][j],out[0][j]);}
        for(unsigned j=0;j<3;j++)area[j]=vfadd(area[j],vfsub(vfmul(a[(j+1)%3],b[(j+2)%3]),vfmul(a[(j+2)%3],b[(j+1)%3])));
    }
    for(unsigned j=0;j<3;j++)area[j]=vfmul(area[j],.5f);
    return vfnorm(area)>=25;
}
typedef struct {unsigned char record[80];float center[3],area[3];} VSelected;
static unsigned vis_select(Visibility *v,VSelected selected[1024],unsigned capacity)
{
    unsigned count=0;unsigned *seen=malloc(v->npatches*sizeof *seen),seen_count=0;if(!seen)return UINT_MAX;
    qsort(v->patches,v->npatches,sizeof *v->patches,vis_patch_compare);
    for(unsigned i=0;i<v->npatches;i++) {
        VPatch *patch=v->patches+i;unsigned found=0;
        for(unsigned j=0;j<seen_count;j++)if(vis_equivalent(patch,v->patches+seen[j])){found=1;break;}
        if(found)continue;
        seen[seen_count++]=i;
        float points[4][3],area[3];if(!vis_inset_amount(patch,points,area,2.0))continue;
        unsigned alternate=UINT_MAX,pair=v->objects[patch->owner].pair;
        if(pair)for(unsigned j=0;j<v->npatches;j++) {
            unsigned owner=v->patches[j].owner;
            if(owner<=8190 && v->objects[owner].pair==pair && (v->objects[owner].flags&2)!=(v->objects[patch->owner].flags&2) && vis_equivalent(patch,v->patches+j)){alternate=owner;break;}
        }
        VSelected *s=selected+count++;memcpy(s->area,area,sizeof s->area);
        for(unsigned axis=0;axis<3;axis++) {
            float sum=0;for(unsigned j=0;j<patch->count;j++)sum=vfadd(sum,points[j][axis]);volatile float mean=sum/(float)patch->count;s->center[axis]=mean;
        }
        float radius=0;
        for(unsigned j=0;j<patch->count;j++) {
            float delta[3];for(unsigned axis=0;axis<3;axis++)delta[axis]=vfsub(points[j][axis],s->center[axis]);float length=vfnorm(delta);if(length>radius)radius=length;
        }
        if(patch->count==3)memcpy(points[3],points[0],sizeof points[0]);
        for(unsigned j=0;j<4;j++)for(unsigned k=0;k<3;k++)der_float(s->record+(j*3+k)*4,points[j][k]);
        for(unsigned k=0;k<3;k++){der_float(s->record+48+k*4,area[k]);der_float(s->record+60+k*4,s->center[k]);}
        der_float(s->record+72,(double)radius+.1);unsigned encoded=patch->count|(alternate==UINT_MAX ? 0:(alternate+1)<<3);
        s->record[76]=patch->owner;s->record[77]=patch->owner>>8;s->record[78]=encoded;s->record[79]=encoded>>8;
        if(count==capacity)break;
    }
    free(seen);return count;
}
typedef struct {Visibility *v;unsigned char *groups;unsigned *members;unsigned count,used;} VGroups;
static int vis_groups(VGroups *g,unsigned *ids,unsigned n)
{
    if(n>8) {
        unsigned axis=0;float largest=-1;
        for(unsigned k=0;k<3;k++) {
            float low=g->v->objects[ids[0]].center[k],high=low;
            for(unsigned i=1;i<n;i++){float p=g->v->objects[ids[i]].center[k];if(p<low)low=p;if(p>high)high=p;}
            float extent=vfsub(high,low);if(extent>largest){largest=extent;axis=k;}
        }
        /* Stable insertion order also breaks equal-center ties identically. */
        for(unsigned i=1;i<n;i++){unsigned id=ids[i],at=i;float p=g->v->objects[id].center[axis];while(at && g->v->objects[ids[at-1]].center[axis]>p){ids[at]=ids[at-1];at--;}ids[at]=id;}
        unsigned mid=n/2;return vis_groups(g,ids,mid) || vis_groups(g,ids+mid,n-mid) ? -1:0;
    }
    float low[3],high[3],center[3],extent[3];
    for(unsigned k=0;k<3;k++) {
        low[k]=vfsub(g->v->objects[ids[0]].center[k],g->v->objects[ids[0]].extent[k]);high[k]=vfadd(g->v->objects[ids[0]].center[k],g->v->objects[ids[0]].extent[k]);
        for(unsigned i=1;i<n;i++){VObject *o=g->v->objects+ids[i];float a=vfsub(o->center[k],o->extent[k]),b=vfadd(o->center[k],o->extent[k]);if(a<low[k])low[k]=a;if(b>high[k])high[k]=b;}
        center[k]=vfmul(vfadd(low[k],high[k]),.5f);float a=fabsf(vfsub(low[k],center[k])),b=fabsf(vfsub(high[k],center[k]));extent[k]=a>b ? a:b;
    }
    unsigned char *row=g->groups+g->count++*20;for(unsigned k=0;k<3;k++)der_float(row+k*4,center[k]);/* Pinned desktop NumPy 1-D dot uses two lanes: (x*x+z*z)+y*y.
       Axis-wise norms above use the ordinary left-to-right reduction. */
    float squared=vfadd(vfadd(vfmul(extent[0],extent[0]),vfmul(extent[2],extent[2])),vfmul(extent[1],extent[1]));
    der_float(row+12,(double)sqrtf(squared)+.2);
    row[16]=g->used;row[17]=g->used>>8;row[18]=n;row[19]=n>>8;
    for(unsigned i=0;i<n;i++)g->members[g->used++]=ids[i];
    return 0;
}
/* Neighboring cells share their boundary camera samples. Keep six-height
 * maxima at three rolling rows of half-cell nodes, never a whole-city table.
 * This is optional: unusual grids or a fragmented heap use the original scan. */
#define VIS_GRID_CACHE_BYTES (340u*1024u)
static void vis_grid_node(const VSelected *selected,unsigned count,double x,double z,double *best)
{
    static const double heights[]={-128,0,64,192,384,768};
    for(unsigned i=0;i<count;i++) {
        double dx=(double)selected[i].center[0]-x,dz=(double)selected[i].center[2]-z;
        double xx=dx*dx,zz=dz*dz,fx=selected[i].area[0]*dx,fz=selected[i].area[2]*dz;
        double current=best[i];
        for(unsigned iy=0;iy<6;iy++) {
            double dy=(double)selected[i].center[1]-heights[iy];
            /* Preserve each original multiply and left-to-right addition. */
            double squared=xx+dy*dy+zz;if(squared<25)squared=25;
            double facing=fx+selected[i].area[1]*dy+fz;
            double fraction=facing/squared,score=fraction*fraction/squared;if(score>current)current=score;
        }
        best[i]=current;
    }
}
static int vis_grid(FILE *out,const VSelected *selected,unsigned count,int gx,int gz,unsigned nx,unsigned nz)
{
    double *best=calloc(count ? count:1,sizeof *best);if(!best)return -1;
    size_t row_values=((size_t)nx*2+1)*count;
    double *cache=count && row_values<=VIS_GRID_CACHE_BYTES/(3*sizeof(double)) ? malloc(row_values*3*sizeof(double)):NULL;
    for(unsigned z=0;z<nz;z++) {
        if(cache)for(unsigned iz=z ? 1:0;iz<3;iz++) {
            unsigned node_z=z*2+iz;double *row=cache+(node_z%3)*row_values;
            memset(row,0,row_values*sizeof *row);
            for(unsigned node_x=0;node_x<=nx*2;node_x++)
                /* All validated grid coordinates are small integers or halves,
                 * so these camera bits equal the original cell + offset form. */
                vis_grid_node(selected,count,(gx+node_x*.5)*512,(gz+node_z*.5)*512,row+(size_t)node_x*count);
        }
        for(unsigned x=0;x<nx;x++) {
            memset(best,0,count*sizeof *best);
            for(unsigned ix=0;ix<3;ix++)for(unsigned iz=0;iz<3;iz++) {
                if(cache) {
                    const double *node=cache+((z*2+iz)%3)*row_values+(size_t)(x*2+ix)*count;
                    for(unsigned i=0;i<count;i++)if(node[i]>best[i])best[i]=node[i];
                }else vis_grid_node(selected,count,(gx+(int)x+ix*.5)*512,(gz+(int)z+iz*.5)*512,best);
            }
            unsigned ids[64],n=0;for(unsigned i=0;i<count;i++) {
                unsigned at=n;while(at && best[i]>best[ids[at-1]]){if(at<64)ids[at]=ids[at-1];at--;}
                if(at<64){ids[at]=i;if(n<64)n++;}
            }
            unsigned char row[128];for(unsigned i=0;i<64;i++){unsigned id=i<n ? ids[i]:65535;row[i*2]=id;row[i*2+1]=id>>8;}
            if(der_write(out,row,sizeof row)){free(cache);free(best);return -1;}
        }
    }
    free(cache);free(best);return 0;
}
int jgr_stage_visibility(FILE *jgs,FILE *jrs,unsigned stage,FILE *out)
{
    Visibility v={0};unsigned char *boxes=NULL,*groups=NULL;unsigned *ids=NULL,*members=NULL;VSelected *selected=NULL;int result=-1;
    unsigned maximum=stage==1 ? 629:stage==2 ? 402:stage==3 ? 635:stage==5 ? 1024:stage==6 ? 749:0;
    if(!out || !maximum || vis_load(&v,jgs,jrs,stage))goto done;
    Derived *d=&v.d;boxes=malloc(d->no*24);groups=malloc(d->no*20);ids=malloc(d->no*sizeof *ids);members=malloc(d->no*sizeof *members);selected=calloc(1024,sizeof *selected);
    if(!boxes || !groups || !ids || !members || !selected || vis_boxes(&v,boxes))goto done;
    uint32_t fingerprint=d->key;
    for(unsigned i=0;i<d->nb;i++) {
        VBatch *b=v.batches+i;if(der_hash(d,d->ov+b->vo*24,b->nv*24,NULL,&fingerprint) || der_hash(d,d->oi+b->io*2,b->ni*2,NULL,&fingerprint))goto done;
    }
    for(unsigned i=0;i<d->no;i++)ids[i]=i;
    VGroups grouping={&v,groups,members,0,0};if(vis_groups(&grouping,ids,d->no))goto done;
    int gx=INT_MAX,gz=INT_MAX,mx=INT_MIN,mz=INT_MIN;
    for(unsigned i=0;i<d->no;i++)if(v.spheres[i*4+3]>0 && v.spheres[i*4+3]<1000) {
        double px=floor((double)v.spheres[i*4]/512),pz=floor((double)v.spheres[i*4+2]/512);
        if(fabs(px)>65535 || fabs(pz)>65535)goto done;
        int x=(int)px,z=(int)pz;if(x<gx)gx=x;if(x>mx)mx=x;if(z<gz)gz=z;if(z>mz)mz=z;
    }
    if(gx==INT_MAX || gz==INT_MAX)goto done;
    gx--;gz--;mx++;mz++;unsigned nx=(unsigned)(mx-gx+1),nz=(unsigned)(mz-gz+1);
    if(!nx || !nz || nx>1024 || nz>1024 || (uint64_t)nx*nz*128>VIS_BYTES)goto done;
    unsigned fixed=36+d->no*28+nx*nz*128+grouping.count*20+d->no*2;if(fixed>=VIS_BYTES)goto done;
    unsigned capacity=(VIS_BYTES-fixed)/80;if(capacity>1024)capacity=1024;if(capacity>maximum)capacity=maximum;if(!capacity || vis_patches(&v))goto done;
    unsigned count=vis_select(&v,selected,capacity);if(count==UINT_MAX)goto done;
    if(der_write(out,"JVO5",4) || der_put(out,d->no) || der_put(out,count) || der_put(out,fingerprint) || der_put(out,(uint32_t)gx) || der_put(out,(uint32_t)gz) || der_put(out,nx) || der_put(out,nz) || der_put(out,grouping.count) || der_write(out,boxes,d->no*24))goto done;
    for(unsigned i=0;i<count;i++)if(der_write(out,selected[i].record,80))goto done;
    if(vis_grid(out,selected,count,gx,gz,nx,nz) || der_write(out,groups,grouping.count*20))goto done;
    for(unsigned i=0;i<d->no;i++){unsigned char p[2]={(unsigned char)members[i],(unsigned char)(members[i]>>8)};if(der_write(out,p,2))goto done;}
    result=0;
 done:free(v.objects);free(v.batches);free(v.matrices);free(v.patches);free(v.spheres);free(boxes);free(groups);free(ids);free(members);free(selected);return result;
}
static void vis_free(Visibility *v){free(v->objects);free(v->batches);free(v->matrices);free(v->patches);free(v->spheres);}
static int ped_bank(Visibility *v,unsigned kind,unsigned char out[28])
{
    Derived *d=&v->d;uint32_t key=d->key;
    for(unsigned i=0;i<d->nb;i++){VBatch *b=v->batches+i;if(der_hash(d,d->ov+b->vo*24,b->nv*24,NULL,&key)||der_hash(d,d->oi+b->io*2,b->ni*2,NULL,&key))return -1;}
    unsigned fields[7]={key,d->nt,d->no,d->nb,d->nv,d->ni,kind};for(unsigned i=0;i<7;i++)der_encode(out+i*4,fields[i]);return 0;
}
int jgr_stage_ped_city_proxy(FILE *city,FILE *placements,void *roof_data,unsigned roof_bytes,void *bank_data,unsigned bank_bytes)
{
    Visibility v={0};VPoint *points=NULL;unsigned short *indices=NULL;int result=-1;unsigned used=0,bus_count=0;double (*bus)[2]=NULL;
    if(!placements||!roof_data||roof_bytes%76||roof_bytes/76>80||bank_bytes!=28||!bank_data||vis_load(&v,city,NULL,0)||vis_patches(&v))goto done;
    if(fseek(placements,0,SEEK_END))goto done;
    long length=ftell(placements);if(length<8||(unsigned long)length>4*1024*1024)goto done;
    unsigned char h[8];if(vfile_read(placements,(unsigned)length,0,h,8)||memcmp(h,"SOB1",4))goto done;unsigned sectors=der_word(h+4),cursor=8;if(sectors>1024)goto done;
    bus=calloc(4096,sizeof *bus);if(!bus)goto done;
    for(unsigned i=0;i<sectors;i++){
        if(vfile_read(placements,(unsigned)length,cursor,h,4))goto done;
        cursor+=4;unsigned n=der_word(h);if(n>((unsigned)length-cursor)/24)goto done;
        for(unsigned j=0;j<n;j++){unsigned char row[24];if(vfile_read(placements,(unsigned)length,cursor,row,24))goto done;cursor+=24;unsigned type=der_word(row+4);if(type==0||type==2){if(bus_count==4096)goto done;bus[bus_count][0]=der_real(row+8);bus[bus_count++][1]=der_real(row+16);}}
    }
    if(cursor!=(unsigned)length||!bus_count)goto done;
    unsigned mv=1,mi=1;for(unsigned i=0;i<v.d.nb;i++){if(v.batches[i].nv>mv)mv=v.batches[i].nv;if(v.batches[i].ni>mi)mi=v.batches[i].ni;}
    points=malloc(mv*sizeof *points);indices=malloc(mi*sizeof *indices);if(!points||!indices)goto done;
    qsort(v.patches,v.npatches,sizeof *v.patches,vis_patch_compare);
    for(unsigned p=0;p<v.npatches;p++){
        VPatch *patch=v.patches+p;VPoint normal=vcross(vsub(patch->points[1],patch->points[0]),vsub(patch->points[2],patch->points[0])),center={{0,0,0}};normal=vdivide(normal,vlength(normal));
        for(unsigned i=0;i<patch->count;i++)for(unsigned k=0;k<3;k++)center.p[k]+=patch->points[i].p[k];
        center=vdivide(center,patch->count);
        if(fabs(normal.p[1])<.98||patch->area<500||center.p[1]<=15||center.p[1]>=150)continue;
        double nearest=HUGE_VAL;for(unsigned i=0;i<bus_count;i++){double x=bus[i][0]-center.p[0],z=bus[i][1]-center.p[2],distance=x*x+z*z;if(distance<nearest)nearest=distance;}if(nearest>200*200)continue;
        float inner[4][3],area[3];if(!vis_inset_amount(patch,inner,area,1.0))continue;
        VObject *o=v.objects+patch->owner;unsigned first=UINT_MAX,last=0;double distance=vdot(patch->points[0],normal);
        for(unsigned i=o->first;i<o->first+o->count;i++){
            VBatch *b=v.batches+i;if(b->flags&1)continue;if(vis_batch(&v,o,b,points,indices))goto done;
            for(unsigned t=0;t<b->ni;t+=3){int on=1;for(unsigned k=0;k<3;k++)if(fabs(vdot(points[indices[t+k]],normal)-distance)>=.01)on=0;if(on){if(i<first)first=i;if(i>last)last=i;break;}}
        }
        if(first==UINT_MAX||first>65535||last>65535||used>=roof_bytes/76)goto done;
        unsigned char *out=(unsigned char*)roof_data+used++*76;float mean[3],radius=0;
        for(unsigned k=0;k<3;k++){float sum=0;for(unsigned i=0;i<patch->count;i++)sum=vfadd(sum,inner[i][k]);volatile float value=sum/(float)patch->count;mean[k]=value;der_float(out+48+k*4,value);}
        for(unsigned i=0;i<patch->count;i++){float delta[3];for(unsigned k=0;k<3;k++)delta[k]=vfsub(inner[i][k],mean[k]);float n=vfnorm(delta);if(n>radius)radius=n;}
        if(patch->count==3)memcpy(inner[3],inner[0],sizeof inner[0]);
        for(unsigned i=0;i<4;i++)for(unsigned k=0;k<3;k++)der_float(out+(i*3+k)*4,inner[i][k]);
        der_float(out+60,(double)radius+.1);float area_norm=sqrtf(vfadd(vfadd(vfmul(area[0],area[0]),vfmul(area[2],area[2])),vfmul(area[1],area[1])));der_float(out+64,1.0/area_norm);unsigned values[4]={patch->owner,patch->count,first,last};for(unsigned k=0;k<4;k++){out[68+k*2]=values[k];out[69+k*2]=values[k]>>8;}
    }
    if(used*76!=roof_bytes||ped_bank(&v,1,bank_data))goto done;
    result=0;
 done:vis_free(&v);free(points);free(indices);free(bus);return result;
}
int jgr_stage_ped_object_proxy(FILE *objects,void *body_data,unsigned body_bytes,void *bank_data,unsigned bank_bytes)
{
    Visibility prop={0};VPoint *points=NULL;int result=-1;
    if(body_bytes!=4*28||bank_bytes!=28||!body_data||!bank_data||vis_load(&prop,objects,NULL,0))goto done;
    unsigned mv=1;for(unsigned i=0;i<prop.d.nb;i++)if(prop.batches[i].nv>mv)mv=prop.batches[i].nv;
    points=malloc(mv*sizeof *points);if(!points)goto done;
    for(unsigned type=0;type<2;type++)for(unsigned lod=0;lod<2;lod++){
        unsigned model=type*2+(lod?prop.d.no/2:0);if(model>=prop.d.no)goto done;VObject *o=prop.objects+model;double low[3]={0},high[3]={0};unsigned count=0;
        for(unsigned i=o->first;i<o->first+o->count;i++){VBatch *b=prop.batches+i;if(vis_batch(&prop,o,b,points,NULL))goto done;for(unsigned j=0;j<b->nv;j++){for(unsigned k=0;k<3;k++){double value=points[j].p[k];if(!count||value<low[k])low[k]=value;if(!count||value>high[k])high[k]=value;}count++;}}
        if(!count)goto done;
        unsigned char *out=(unsigned char*)body_data+(type*2+lod)*28;
        for(unsigned k=0;k<3;k++){double inset=k==1?4:2;low[k]+=inset;high[k]-=inset;if(high[k]<=low[k])goto done;der_float(out+k*4,low[k]);der_float(out+12+k*4,high[k]);}der_encode(out+24,model);
    }
    if(ped_bank(&prop,2,bank_data))goto done;
    result=0;
 done:vis_free(&prop);free(points);return result;
}
int jgr_stage_ped_proxy(FILE *city,FILE *objects,FILE *placements,void *roof_data,unsigned roof_bytes,void *body_data,unsigned body_bytes,void *bank_data,unsigned bank_bytes)
{
    if(!bank_data||bank_bytes!=56)return -1;
    if(jgr_stage_ped_city_proxy(city,placements,roof_data,roof_bytes,bank_data,28))return -1;
    return jgr_stage_ped_object_proxy(objects,body_data,body_bytes,(unsigned char*)bank_data+28,28);
}
