/* Streaming collision conversion. Double precision preserves the desktop
 * preparation math; only emitted triangle positions round to binary32. */
typedef struct {double v[16];} ColMatrix;
typedef struct {float low[3],high[3];unsigned first,count;} ColBounds;
typedef struct {uint32_t address,kind,first,count;} ColOwner;
typedef struct {ColBounds box;unsigned owner;} ColItemPatch;
typedef struct {
    World *w;unsigned triangles,legacy,skip_boxes;int surface_override;
    unsigned nodes,cip,owner_count,item_patch_count;int active_owner;
    ColOwner *owners;ColItemPatch *item_patches;
    ColBounds patch_bounds,owner_bounds;
} ColBuild;
static int col_ok(World *w,uint32_t address)
{uint32_t offset;return source(w,address,4,&offset)!=NULL;}
static double col_real(World *w,uint32_t address)
{uint32_t bits=word(w,address);float value;memcpy(&value,&bits,4);if(!isfinite(value))w->error=1;return value;}
static ColMatrix col_identity(void)
{ColMatrix m={{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}};return m;}
static ColMatrix col_mul(ColMatrix a,ColMatrix b)
{
    ColMatrix c;for(unsigned i=0;i<4;i++)for(unsigned j=0;j<4;j++) {
        double value=0;for(unsigned k=0;k<4;k++)value=value+a.v[i*4+k]*b.v[k*4+j];c.v[i*4+j]=value;
    }
    return c;
}
static ColMatrix col_rotate(unsigned axis,double angle)
{
    double c=cos(angle),s=sin(angle);ColMatrix m=col_identity();
    if(axis==0){m.v[5]=c;m.v[6]=-s;m.v[9]=s;m.v[10]=c;}
    else if(axis==1){m.v[0]=c;m.v[2]=s;m.v[8]=-s;m.v[10]=c;}
    else {m.v[0]=c;m.v[1]=-s;m.v[4]=s;m.v[5]=c;}return m;
}
static ColMatrix col_placement(World *w,uint32_t pos,uint32_t rot,unsigned legacy)
{
    ColMatrix t=col_identity(),x,y,z;const double bam=2.0*3.141592653589793/65536.0;
    for(unsigned i=0;i<3;i++)t.v[i*4+3]=col_real(w,pos+i*4);
    x=col_rotate(0,(int32_t)word(w,rot)*bam);y=col_rotate(1,(int32_t)word(w,rot+4)*bam);z=col_rotate(2,(int32_t)word(w,rot+8)*bam);
    return col_mul(t,legacy ? col_mul(x,col_mul(y,z)):col_mul(z,col_mul(y,x)));
}
static void col_transform(const ColMatrix *m,const double p[3],double out[3])
{
    for(unsigned i=0;i<3;i++)out[i]=m ? m->v[i*4]*p[0]+m->v[i*4+1]*p[1]+m->v[i*4+2]*p[2]+m->v[i*4+3]:p[i];
}
static void col_point(ColBuild *c,uint32_t vertices,int index,const ColMatrix *m,double out[3])
{
    if(index<0 || (uint64_t)vertices+(unsigned)index*12u+12>UINT32_MAX){c->w->error=1;memset(out,0,24);return;}
    double p[3];for(unsigned i=0;i<3;i++)p[i]=col_real(c->w,vertices+(unsigned)index*12+i*4);col_transform(m,p,out);
}
static void col_bound_add(ColBounds *b,const unsigned char row[44])
{
    for(unsigned i=0;i<3;i++)for(unsigned j=0;j<3;j++) {
        uint32_t bits=u32(row+(i*3+j)*4);float value;memcpy(&value,&bits,4);
        if(!b->count && !i){b->low[j]=value;b->high[j]=value;}
        else {if(value<b->low[j])b->low[j]=value;if(value>b->high[j])b->high[j]=value;}
    }
    b->count++;
}
static void col_begin_patch(ColBuild *c)
{memset(&c->patch_bounds,0,sizeof c->patch_bounds);c->patch_bounds.first=c->triangles;}
static void col_end_patch(ColBuild *c)
{
    if(c->cip!=1 || c->active_owner<0 || !c->patch_bounds.count)return;
    if(c->item_patch_count==8192){c->w->error=1;return;}
    ColItemPatch *p=c->item_patches+c->item_patch_count++;p->box=c->patch_bounds;p->owner=(unsigned)c->active_owner;
}
static void col_tri(ColBuild *c,const double a[3],const double b[3],const double d[3],unsigned surface,unsigned mask,unsigned flags,unsigned src)
{
    unsigned char row[44];const double *vertices[3]={a,b,d};
    if(c->triangles==100000){c->w->error=1;return;}
    for(unsigned i=0;i<3;i++)for(unsigned j=0;j<3;j++) {
        float value=(float)vertices[i][j];uint32_t bits;
        if(!isfinite(value)){c->w->error=1;return;}memcpy(&bits,&value,4);s32(row+(i*3+j)*4,bits);
    }
    unsigned values[4]={c->surface_override>=0 ? (unsigned)c->surface_override:surface,mask,flags,src};
    for(unsigned i=0;i<4;i++){row[36+i*2]=(unsigned char)values[i];row[37+i*2]=(unsigned char)(values[i]>>8);}
    write_bytes(c->w,row,sizeof row);
    if(c->cip && c->active_owner>=0){col_bound_add(&c->owner_bounds,row);col_bound_add(&c->patch_bounds,row);}
    c->triangles++;
}
static void col_patch(ColBuild *c,uint32_t vertices,uint32_t patch,const ColMatrix *m,unsigned src)
{
    unsigned char raw[36];World *w=c->w;read_at(w,patch,raw,36);
    uint32_t indices=u32(raw+24),flags=u32(raw+28);unsigned count=flags&65535,wind=!!(flags&0x40000000u);
    if(!count || !col_ok(w,indices))return;
    col_begin_patch(c);
    double previous[2][3];unsigned char index[2];
    read_at(w,indices,index,2);col_point(c,vertices,(int16_t)u16(index),m,previous[0]);
    read_at(w,indices+2,index,2);col_point(c,vertices,(int16_t)u16(index),m,previous[1]);
    for(unsigned i=0;i<count && !w->error;i++) {
        double next[3];read_at(w,indices+(i+2)*2,index,2);col_point(c,vertices,(int16_t)u16(index),m,next);
        /* Native queries retain authored strip order for exact plane and
           barycentric carry arithmetic; legacy output stays byte-identical. */
        col_tri(c,previous[0],wind ? previous[1]:next,wind ? next:previous[1],u16(raw+34),u16(raw+32),(!!(flags&0x80000000u)) | (w->native_solid && !wind ? 2u:0),src);
        memcpy(previous[0],previous[1],24);memcpy(previous[1],next,24);wind^=1;
    }
    col_end_patch(c);
}
static void col_mesh(ColBuild *c,uint32_t mesh,const ColMatrix *m,unsigned src)
{
    unsigned char row[12];read_at(c->w,mesh,row,12);uint32_t vertices=u32(row),patches=u32(row+4);unsigned count=u16(row+10);
    if(!col_ok(c->w,vertices) || !col_ok(c->w,patches))return;
    for(unsigned i=0;i<count && !c->w->error;i++)col_patch(c,vertices,patches+i*36,m,src);
}
static void col_walk(ColBuild *c,uint32_t address,ColMatrix parent,unsigned src,unsigned depth)
{
    World *w=c->w;
    while(col_ok(w,address) && depth<16 && !w->error) {
        unsigned char row[68];if(c->nodes++==4096){w->error=1;return;}read_at(w,address,row,68);
        ColMatrix scale=col_identity();for(unsigned i=0;i<3;i++)scale.v[i*5]=col_real(w,address+36+i*4);
        ColMatrix local=col_mul(parent,col_mul(col_placement(w,address+12,address+24,c->legacy),scale));
        unsigned kind=u16(row);uint32_t mesh=u32(row+4);
        if(kind==6 && col_ok(w,mesh))col_mesh(c,mesh,&local,src);
        else if(kind==4 && col_ok(w,mesh) && !c->skip_boxes) {
            double hx=col_real(w,mesh),y0=col_real(w,mesh+4),hz=col_real(w,mesh+8),y1=col_real(w,mesh+16);
            double p[8][3]={{-hx,y0,-hz},{hx,y0,-hz},{hx,y0,hz},{-hx,y0,hz},{-hx,y1,-hz},{hx,y1,-hz},{hx,y1,hz},{-hx,y1,hz}},transformed[8][3];
            static const unsigned char faces[12][3]={{4,7,6},{4,6,5},{0,1,2},{0,2,3},{0,4,5},{0,5,1},{1,5,6},{1,6,2},{2,6,7},{2,7,3},{3,7,4},{3,4,0}};
            col_begin_patch(c);
            for(unsigned i=0;i<8;i++)col_transform(&local,p[i],transformed[i]);
            for(unsigned i=0;i<12;i++)col_tri(c,transformed[faces[i][0]],transformed[faces[i][1]],transformed[faces[i][2]],0x1c,0xffff,0,src);
            col_end_patch(c);
        }
        uint32_t child=u32(row+60);if(col_ok(w,child) && child!=address)col_walk(c,child,local,src,depth+1);
        address=u32(row+64);
    }
}
static void col_patch_word(World *w,long at,uint32_t value)
{
    long end=ftell(w->out);if(end<0 || fseek(w->out,at,SEEK_SET)){w->error=1;return;}
    put(w,value);if(fseek(w->out,end,SEEK_SET))w->error=1;
}
static void set_colliders(World *w,const WorldSpec *s)
{
    const uint32_t *spec=NULL;for(unsigned i=0;i<5;i++)if(world_set_colliders[i][0]==s->stage)spec=world_set_colliders[i];
    if(!spec){w->error=1;return;}unsigned types=spec[2]>spec[4] ? spec[2]:spec[4];
    ColBuild c={0};c.w=w;c.surface_override=-1;write_bytes(w,"SOB1",4);put(w,types);
    for(unsigned i=0;i<types && !w->error;i++) {
        long offset=ftell(w->out);unsigned before=c.triangles;put(w,0);
        for(unsigned group=0;group<2;group++)if(i<spec[2+group*2]) {
            uint32_t node=word(w,spec[1+group*2]+i*4);unsigned char half[2];
            if(!col_ok(w,node))continue;
            read_at(w,node,half,2);
            if(u16(half)==4 || u16(half)==6){c.nodes=0;col_walk(&c,node,col_identity(),0,0);}
        }
        col_patch_word(w,offset,c.triangles-before);
    }
}

/* Original world grid metadata is small relative to its repeated triangles.
 * Stream triangles directly and retain only bounded directory/bounds arrays. */
typedef struct {uint32_t vertices,patch;unsigned char box[32];} ColWorldPatch;
typedef struct {unsigned char row[12];} ColWorldEntry;
static void col_write_bounds(World *w,const ColBounds *box)
{
    unsigned char row[32];for(unsigned j=0;j<3;j++) {
        uint32_t bits;memcpy(&bits,box->low+j,4);s32(row+j*4,bits);memcpy(&bits,box->high+j,4);s32(row+16+j*4,bits);
    }
    s32(row+12,box->count);s32(row+28,box->first);write_bytes(w,row,32);
}
static void col_begin_owner(ColBuild *c,uint32_t address,uint32_t kind)
{
    c->nodes=0;if(!c->cip)return;
    if(c->owner_count==4096){c->w->error=1;return;}
    c->active_owner=(int)c->owner_count;ColOwner *o=c->owners+c->owner_count;
    *o=(ColOwner){address,kind,c->triangles,0};memset(&c->owner_bounds,0,sizeof c->owner_bounds);c->owner_bounds.first=c->triangles;
}
static void col_end_owner(ColBuild *c)
{
    if(!c->cip || c->active_owner<0)return;
    ColOwner *o=c->owners+c->owner_count;o->count=c->triangles-o->first;
    if(o->count) {
        if(c->cip==2) {
            if(c->item_patch_count==8192){c->w->error=1;return;}
            ColItemPatch *p=c->item_patches+c->item_patch_count++;p->owner=c->owner_count;p->box=c->owner_bounds;
        }
        c->owner_count++;
    }
    c->active_owner=-1;
}
static void col_items(ColBuild *c,const WorldCollisionSpec *s)
{
    World *w=c->w;if(!s->items)return;
    for(unsigned sector=0;sector<=s->sectors && !w->error;sector++) {
        uint32_t address=word(w,s->items+sector*4);if(!col_ok(w,address))continue;
        unsigned count=0;
        while(address>=STAGE_BASE && (uint64_t)address-STAGE_BASE+s->stride<=w->stage.size && !w->error) {
            if(count++==4096){w->error=1;break;}
            int32_t type=(int32_t)word(w,address);if(type<-1)break;
            if(w->native_solid && s->stage==1){
                /* 03144c registers ordinary item meshes only in the four
                 * sectors selected by the original table. Floor kinds
                 * 0x2e..0x34 are also registered in the other sectors. */
                unsigned enabled=type>=0x2e && type<=0x34;
                for(unsigned i=0;i<4;i++)if(word(w,0x8c106384u+i*4)==sector)enabled=1;
                if(!enabled){address+=s->stride;continue;}
            }
            uint32_t node=s->prototypes_only ? 0:word(w,address+s->collider_offset);
            if(!col_ok(w,node) && type>=0 && (unsigned)type<s->prototype_count) {
                uint32_t candidate=word(w,s->prototypes+(unsigned)type*4);unsigned char kind[2];
                if(col_ok(w,candidate)){read_at(w,candidate,kind,2);if(u16(kind)==6)node=candidate;}
            }
            if(col_ok(w,node)) {
                ColMatrix placement=col_placement(w,address+4,address+16,s->legacy);
                col_begin_owner(c,address,(uint32_t)type);col_walk(c,node,placement,1,0);col_end_owner(c);
            }
            address+=s->stride;
        }
    }
}
static void col_dynamic_record(ColBuild *c,const WorldCollisionSpec *s,uint32_t address)
{
    World *w=c->w;int32_t type=(int32_t)word(w,address);
    if(type<0 || (unsigned)type>=s->dynamic_types ||
       (!w->native_solid && s->stage==3 && world_benten_excluded[type]))return;
    uint32_t prototype=word(w,s->dynamic_prototypes+(unsigned)type*4),nodes[64];unsigned count=0;
    if(col_ok(w,prototype)) {
        unsigned char half[2];read_at(w,prototype,half,2);
        if(u16(half)==4 || u16(half)==6)nodes[count++]=prototype;
        else {
            uint32_t list=word(w,prototype),n=word(w,prototype+4);
            if(col_ok(w,list) && n<64)for(unsigned i=0;i<n;i++) {
                uint32_t node=word(w,list+i*12);if(col_ok(w,node))nodes[count++]=node;
            }
        }
    }
    if(!count)return;
    ColMatrix placement=col_placement(w,address+4,address+16,s->legacy);
    c->surface_override=w->native_solid && s->stage==3 ? 31:
        s->stage==2 ? world_kogane_contact[type]:world_benten_contact[type];
    col_begin_owner(c,address,(uint32_t)type);for(unsigned i=0;i<count && !w->error;i++)col_walk(c,nodes[i],placement,2,0);
    col_end_owner(c);c->surface_override=-1;
}
static void col_dynamic(ColBuild *c,const WorldCollisionSpec *s)
{
    World *w=c->w;if(!s->dynamic)return;
    for(unsigned sector=0;sector<=s->sectors && !w->error;sector++) {
        uint32_t list=word(w,s->dynamic+sector*4);if(!col_ok(w,list))continue;
        unsigned count=0;uint32_t at=list;
        if(col_ok(w,word(w,list))) {
            while(col_ok(w,at) && at-list<40000 && !w->error) {
                uint32_t address=word(w,at);if(!col_ok(w,address))break;
                col_dynamic_record(c,s,address);at+=4;
            }
        } else while(col_ok(w,at) && (int32_t)word(w,at)>=-1 && !w->error) {
            if(count++==4096){w->error=1;break;}
            col_dynamic_record(c,s,at);at+=s->dynamic_stride;
        }
    }
}
static void world_collision(World *w,const WorldSpec *world)
{
    const WorldCollisionSpec *s=NULL;for(unsigned i=0;i<5;i++)if(world_collision_specs[i].stage==world->stage)s=world_collision_specs+i;
    if(!s){w->error=1;return;}
    WorldCollisionSpec native;
    if(w->native_solid){
        native=*s;native.skip_boxes=1;native.cip=1;
        if(s->stage==1){native.sectors=62;native.static_nodes=0x8c175cc0u;native.static_count=41;native.legacy=0;}
        s=&native;
    }
    ColBuild c={0};c.w=w;c.legacy=s->legacy;c.skip_boxes=s->skip_boxes;c.surface_override=-1;c.active_owner=-1;c.cip=s->cip;
    ColWorldPatch *patches=calloc(8192,sizeof *patches);ColWorldEntry *entries=calloc(16384,sizeof *entries);
    uint32_t *lookup=calloc(16384,sizeof *lookup);uint32_t starts[1028]={0};unsigned np=0,ne=0,nworld=0;
    c.owners=calloc(4096,sizeof *c.owners);c.item_patches=calloc(8192,sizeof *c.item_patches);
    if(!patches || !entries || !lookup || !c.owners || !c.item_patches){w->error=1;goto done;}
    uint32_t grid=s->grid_pointer ? word(w,s->grid):s->grid;
    write_bytes(w,"COL1",4);put(w,0);put(w,0);put(w,0x31535043u);
    for(unsigned cell=0;cell<1024 && !w->error;cell++) {
        starts[cell]=ne;uint32_t list=word(w,grid+cell*8),count=word(w,grid+cell*8+4);
        if(!col_ok(w,list) || !count || count>4096)continue;
        for(unsigned i=0;i<count && !w->error;i++) {
            unsigned char row[16];read_at(w,list+i*16,row,16);uint32_t vertices=u32(row),patch=u32(row+4);
            if(!col_ok(w,vertices) || !col_ok(w,patch))continue;
            unsigned first=c.triangles;col_patch(&c,vertices,patch,NULL,0);unsigned triangles=c.triangles-first;if(!triangles)continue;
            if(c.triangles>65536 || ne==16384){w->error=1;break;}
            unsigned hash=((vertices>>2)*2654435761u^(patch>>2))&16383u,id;
            while(lookup[hash] && (patches[lookup[hash]-1].vertices!=vertices || patches[lookup[hash]-1].patch!=patch))hash=(hash+1)&16383u;
            if(!lookup[hash]) {
                if(np==8192){w->error=1;break;}id=np++;lookup[hash]=id+1;
                ColWorldPatch *p=patches+id;p->vertices=vertices;p->patch=patch;
                double bounds[6];for(unsigned j=0;j<6;j++)bounds[j]=col_real(w,patch+j*4);
                for(unsigned j=0;j<3;j++)if(bounds[j]>bounds[j+3])w->error=1;
                read_at(w,patch,p->box,12);s32(p->box+12,triangles);read_at(w,patch+12,p->box+16,12);s32(p->box+28,first);
            } else id=lookup[hash]-1;
            unsigned char *e=entries[ne++].row;e[0]=(unsigned char)first;e[1]=(unsigned char)(first>>8);e[2]=(unsigned char)id;e[3]=(unsigned char)(id>>8);memcpy(e+4,row+8,8);
        }
    }
    nworld=c.triangles;starts[1024]=ne;
    col_items(&c,s);
    for(unsigned i=0;i<s->static_count && !w->error;i++) {
        uint32_t node=word(w,s->static_nodes+i*4);if(!node)continue;
        col_begin_owner(&c,node,0xffffffffu);col_walk(&c,node,col_identity(),1,0);col_end_owner(&c);
    }
    col_dynamic(&c,s);
    put(w,1);put(w,nworld);put(w,np);put(w,ne);for(unsigned i=0;i<1028;i++)put(w,starts[i]);
    for(unsigned i=0;i<np;i++)write_bytes(w,patches[i].box,32);
    for(unsigned i=0;i<ne;i++)write_bytes(w,entries[i].row,12);
    if(c.owner_count) {
        put(w,0x31504943u);put(w,c.owner_count);put(w,c.item_patch_count);put(w,0);
        for(unsigned i=0;i<c.owner_count;i++){ColOwner *o=c.owners+i;put(w,o->address);put(w,o->kind);put(w,o->first);put(w,o->count);}
        for(unsigned i=0;i<c.item_patch_count;i++)col_write_bounds(w,&c.item_patches[i].box);
        for(unsigned i=0;i<c.item_patch_count;i++)put(w,c.item_patches[i].owner);
    }
    col_patch_word(w,4,c.triangles);col_patch_word(w,8,16+c.triangles*44);
 done:free(patches);free(entries);free(lookup);free(c.owners);free(c.item_patches);
}
