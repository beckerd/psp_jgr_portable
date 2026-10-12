/* Source object/motion decoding for setup; no local clip/frame tables. The
 * source bank stays below256KiB and candidate scratch below144KiB. */
#define ENEMY_BASE 0x8ca00000u
#define ENEMY_BANK_LIMIT (256u*1024u)
#define ENEMY_OBJECT_LIMIT 4096u
typedef struct {uint32_t address,child,sibling;unsigned parent;} EnemyObject;
typedef struct {unsigned count,index;uint32_t address;} EnemyRoot;
static int enemy_pointer(uint32_t p,unsigned size,unsigned alignment)
{return p>=ENEMY_BASE && p-ENEMY_BASE<size && p%alignment==0;}
static float enemy_float(const unsigned char *p)
{uint32_t value=word(p);float f;memcpy(&f,&value,4);return f;}
static int enemy_object_find(const EnemyObject *objects,unsigned count,uint32_t address)
{
    unsigned low=0,high=count;
    while(low<high){unsigned mid=low+(high-low)/2;if(objects[mid].address<address)low=mid+1;else high=mid;}
    return low<count && objects[low].address==address ? (int)low : -1;
}
static int enemy_root_order(const void *a,const void *b)
{
    const EnemyRoot *x=(const EnemyRoot *)a,*y=(const EnemyRoot *)b;
    if(x->count!=y->count)return x->count>y->count ? -1 : 1;
    return (x->address>y->address)-(x->address<y->address);
}
static int enemy_node_count(const unsigned char *bank,unsigned size,unsigned root_index,unsigned *nodes)
{
    EnemyObject *objects=NULL;EnemyRoot *roots=NULL;unsigned *stack=NULL;uint16_t *seen=NULL;
    unsigned count=0,root_count=0,offset,i;int result=-1;
    objects=(EnemyObject *)calloc(ENEMY_OBJECT_LIMIT,sizeof *objects);
    roots=(EnemyRoot *)calloc(ENEMY_OBJECT_LIMIT,sizeof *roots);
    stack=(unsigned *)malloc(ENEMY_OBJECT_LIMIT*sizeof *stack);
    seen=(uint16_t *)calloc(ENEMY_OBJECT_LIMIT,sizeof *seen);
    if(!objects || !roots || !stack || !seen)goto done;
    for(offset=0;offset+52<size;offset+=4) {
        uint32_t model,child,sibling;unsigned component;int valid=1;
        if(word(bank+offset)>65535)continue;
        model=word(bank+offset+4);child=word(bank+offset+44);sibling=word(bank+offset+48);
        if((!model && !child && !sibling) || (model && !enemy_pointer(model,size,4)) ||
           (child && !enemy_pointer(child,size,4)) || (sibling && !enemy_pointer(sibling,size,4)))continue;
        for(component=0;component<3;component++) {
            float scale=enemy_float(bank+offset+32+component*4);
            if(!(scale>0.0001 && scale<1000)){valid=0;break;}
        }
        if(!valid)continue;
        if(count==ENEMY_OBJECT_LIMIT)goto done;
        objects[count].address=ENEMY_BASE+offset;objects[count].child=child;objects[count].sibling=sibling;count++;
    }
    for(i=0;i<count;i++) {
        int child=enemy_object_find(objects,count,objects[i].child),sibling=enemy_object_find(objects,count,objects[i].sibling);
        if(child>=0)objects[child].parent=1;
        if(sibling>=0)objects[sibling].parent=1;
    }
    for(i=0;i<count;i++)if(!objects[i].parent) {
        unsigned pending=0,visited=0;uint16_t stamp=(uint16_t)(root_count+1);
        stack[pending++]=i;seen[i]=stamp;
        while(pending) {
            unsigned at=stack[--pending],j;uint32_t links[2]={objects[at].child,objects[at].sibling};visited++;
            for(j=0;j<2;j++) {
                int target=enemy_object_find(objects,count,links[j]);
                if(target>=0 && seen[target]!=stamp){seen[target]=stamp;stack[pending++]=(unsigned)target;}
            }
        }
        roots[root_count].count=visited;roots[root_count].index=i;roots[root_count].address=objects[i].address;root_count++;
    }
    if(root_index>=root_count)goto done;
    qsort(roots,root_count,sizeof *roots,enemy_root_order);*nodes=roots[root_index].count;result=0;
done:free(seen);free(stack);free(roots);free(objects);return result;
}
static int enemy_map_count(JgrDisc *disc,unsigned family,signed char map[64],signed char frames[128],unsigned *frame_count)
{
    const EnemyRecipe *r;JgrDiscFile source,exe;unsigned char *bank=NULL;uint32_t headers[128],slots[64];
    unsigned nodes,count=0,offset,i;int result=-1;
    if(family>=sizeof enemy_recipes/sizeof enemy_recipes[0])return -1;
    r=enemy_recipes+family;
    if(r->count>64 || jgr_disc_source(disc,r->source,&source) || source.size>ENEMY_BANK_LIMIT || source.size<56 ||
       jgr_disc_find(disc,"1ST_READ.BIN",&exe))return -1;
    bank=(unsigned char *)malloc(source.size);if(!bank)return -1;
    if(jgr_disc_read(disc,&source,0,bank,source.size) || enemy_node_count(bank,source.size,r->root_index,&nodes))goto done;
    for(offset=0;offset+8<=source.size;offset+=4) {
        uint32_t flags=word(bank+offset),motion=word(bank+offset+4);unsigned record,bone;int valid=1;
        if(flags<0x10000 || flags>=0x1000000 || !(flags&65535) || (flags&65535)>=512 ||
           !enemy_pointer(motion,source.size,4))continue;
        record=motion-ENEMY_BASE;
        if(nodes>(source.size-record)/16)continue;
        for(bone=0;bone<nodes;bone++) {
            const unsigned char *p=bank+record+bone*16;uint32_t pos=word(p+4),angle=word(p+8),scale=word(p+12);
            if(word(p)>=512 || (pos && !enemy_pointer(pos,source.size,4)) ||
               (angle && !enemy_pointer(angle,source.size,2)) || (scale && !enemy_pointer(scale,source.size,4))) {valid=0;break;}
        }
        if(valid) {
            unsigned length=flags>>16;
            if(count==128 || !length || length>127)goto done;
            headers[count]=offset;frames[count]=(signed char)length;count++;
        }
    }
    if(!count)goto done;
    for(i=0;i<r->count;i++) {
        double pointer;if(read_value(disc,&exe,0x8c010000u,r->table+i*4,'I',&pointer))goto done;
        slots[i]=(uint32_t)pointer;
    }
    if(r->alias_destination>=0) {
        if((unsigned)r->alias_destination>=r->count || (unsigned)r->alias_source>=r->count)goto done;
        slots[r->alias_destination]=slots[r->alias_source];
    }
    for(i=0;i<r->count;i++) {
        unsigned j;map[i]=-1;
        if(slots[i]<ENEMY_BASE || slots[i]>=0x8cb00000u)continue;
        for(j=0;j<count;j++)if(headers[j]==slots[i]-ENEMY_BASE){map[i]=(signed char)j;break;}
    }
    if(frame_count)*frame_count=count;
    result=0;
done:free(bank);return result;
}
static int enemy_map(JgrDisc *disc,unsigned family,signed char map[64],signed char frames[128])
{return enemy_map_count(disc,family,map,frames,NULL);}
static int enemy_copy_section(JgrDisc *disc,const TableRecipe *r,unsigned char *data,unsigned section,unsigned family)
{
    signed char map[64],frames[128];const TableSection *s;
    if(section>=r->section_count || enemy_map(disc,family,map,frames))return -1;
    s=r->sections+section;if(s->size!=enemy_recipes[family].count)return -1;
    memcpy(data+s->offset,map,s->size);return 0;
}
static int populate_enemy(JgrDisc *disc,const TableRecipe *r,unsigned char *data)
{
    signed char map[64],frames[128];unsigned i;
    if(r->special==4) {
        static const unsigned families[]={TABLE_ENEMY_KEIKAN,TABLE_ENEMY_KIDOUTAI,TABLE_ENEMY_INU,TABLE_ENEMY_KILLER,
            TABLE_ENEMY_BOMB,TABLE_ENEMY_KAIRIKI,TABLE_ENEMY_FIRE};
        if(r->section_count!=7)return -1;
        for(i=0;i<7;i++)if(enemy_copy_section(disc,r,data,i,families[i]))return -1;
    }else if(r->special==5) {
        static const unsigned families[]={TABLE_ENEMY_PARA,TABLE_ENEMY_PATCAR,TABLE_ENEMY_BENZ,TABLE_ENEMY_HERI,TABLE_ENEMY_BHERI};
        static const unsigned rivals[]={TABLE_ENEMY_RIVAL1,TABLE_ENEMY_RIVAL2,TABLE_ENEMY_RIVAL3};
        if(r->section_count!=6 || r->sections[5].size!=9)return -1;
        for(i=0;i<5;i++)if(enemy_copy_section(disc,r,data,i,families[i]))return -1;
        for(i=0;i<3;i++) {
            if(enemy_map(disc,rivals[i],map,frames))return -1;
            memcpy(data+r->sections[5].offset+i*3,map,3);
        }
    }else if(r->special==6) {
        static const unsigned families[]={TABLE_ENEMY_KEIKAN,TABLE_ENEMY_KIDOUTAI,TABLE_ENEMY_PARA,TABLE_ENEMY_KILLER};
        static const unsigned slots[]={9,26,27,28};
        if(r->section_count!=1 || r->sections[0].size!=32)return -1;
        for(i=0;i<4;i++) {
            unsigned j;unsigned char *out=data+r->sections[0].offset+i*8;
            if(enemy_map(disc,families[i],map,frames))return -1;
            for(j=0;j<4;j++){int clip=map[slots[j]];if(clip<0)return -1;out[j]=(unsigned char)clip;out[j+4]=(unsigned char)frames[clip];}
        }
    }else if(r->special==7) {
        static const unsigned primary[]={0,1,2,6,3,4,5,6},turn[]={9,10};
        if(r->section_count!=2 || r->sections[0].size!=8 || r->sections[1].size!=2 ||
           enemy_map(disc,TABLE_ENEMY_SNIPER,map,frames))return -1;
        for(i=0;i<8;i++)data[r->sections[0].offset+i]=(unsigned char)map[primary[i]];
        for(i=0;i<2;i++)data[r->sections[1].offset+i]=(unsigned char)map[turn[i]];
    }else if(r->special==8) {
        static const unsigned slots[]={11,23,8,20,12,13,14,15,9,0,18,19,5,16,17,4,21,21,19,16};
        if(r->section_count!=1 || r->sections[0].size!=20 || enemy_map(disc,TABLE_ENEMY_KEIKAN,map,frames))return -1;
        for(i=0;i<20;i++)data[r->sections[0].offset+i]=(unsigned char)map[slots[i]];
    }else if(r->special==9) {
        static const int slots[]={16,23,9,22,-1,16,16,16,3,16,17,20,21,6,18,19,5,3};
        if(r->section_count!=1 || r->sections[0].size!=18 || enemy_map(disc,TABLE_ENEMY_ONISIMA,map,frames))return -1;
        for(i=0;i<18;i++)data[r->sections[0].offset+i]=(unsigned char)(slots[i]<0 ? -1 : map[slots[i]]);
    }else if(r->special==10) {
        static const unsigned slots[]={13,14,15,10,11,12};
        if(r->section_count!=1 || r->sections[0].size!=6 || enemy_map(disc,TABLE_ENEMY_ONISIMA,map,frames))return -1;
        for(i=0;i<6;i++)data[r->sections[0].offset+i]=(unsigned char)map[slots[i]];
    }else return -1;
    return 0;
}
