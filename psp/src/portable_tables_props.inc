/* Original collider and UI bindings, serialized into the portable BSS ABI. */
typedef struct {JgrDisc *disc;JgrDiscFile exe,stage;} TableStage;
static int ts_bytes(TableStage *s,uint32_t a,void *p,unsigned n)
{return a>=0x8cb00000u?table_bytes(s->disc,&s->stage,0x8cb00000u,a,p,n):table_bytes(s->disc,&s->exe,0x8c010000u,a,p,n);}
static int ts_word(TableStage *s,uint32_t a,uint32_t *p)
{unsigned char b[4];if(ts_bytes(s,a,b,4))return -1;*p=word(b);return 0;}
typedef struct {uint32_t address,mesh;unsigned kind,flags;int parent;} TableCollider;
static int table_colliders(TableStage *s,uint32_t root,TableCollider *nodes,unsigned *count)
{
    struct {uint32_t address;int parent;} pending[64];unsigned n=1,used=0;
    pending[0].address=root;pending[0].parent=-1;
    while(n) {
        uint32_t address=pending[--n].address;int parent=pending[n].parent;unsigned char raw[68];
        if(!address)continue;
        if(used>=64 || ts_bytes(s,address,raw,sizeof raw))return -1;
        for(unsigned i=0;i<used;i++)if(nodes[i].address==address)return -1;
        unsigned index=used++;nodes[index]=(TableCollider){address,word(raw+4),raw[0]|(unsigned)raw[1]<<8,raw[2]|(unsigned)raw[3]<<8,parent};
        if(word(raw+64)){if(n>=64)return -1;pending[n].address=word(raw+64);pending[n++].parent=parent;}
        if(word(raw+60)){if(n>=64)return -1;pending[n].address=word(raw+60);pending[n++].parent=(int)index;}
    }
    *count=used;return 0;
}
static int table_train_surfaces(TableStage *s,uint32_t root,const TableSection *vertices,const TableSection *triangles,unsigned char *data)
{
    TableCollider nodes[64];unsigned count,nvertices=0,ntriangles=0;
    if(vertices->size%16 || triangles->size%20 || table_colliders(s,root,nodes,&count))return -1;
    for(unsigned bone=0;bone<count;bone++) {
        const TableCollider *node=nodes+bone;if(!node->kind)continue;
            if(node->kind!=6)return -1;
        unsigned char head[12];if(ts_bytes(s,node->mesh,head,12))return -1;
        uint32_t vb=word(head),pb=word(head+4);unsigned nv=head[8]|(unsigned)head[9]<<8,np=head[10]|(unsigned)head[11]<<8;
        if(nv>65535 || np>8192)return -1;
        for(unsigned patch=0;patch<np;patch++) {
            unsigned char record[12];if(ts_bytes(s,pb+patch*36+24,record,12))return -1;
            uint32_t indices=word(record),flags=word(record+4),kind=word(record+8);unsigned n=flags&65535,wind=!!(flags&0x40000000);
            if(n>triangles->size/20-ntriangles)return -1;
            for(unsigned tri=0;tri<n;tri++,wind=!wind) {
                unsigned char ids[6],*out=data+triangles->offset+ntriangles++*20;
                if(ts_bytes(s,indices+tri*2,ids,6))return -1;
                for(unsigned corner=0;corner<3;corner++) {
                    unsigned k=corner?(wind?corner:3-corner):0;int index=(int16_t)(ids[k*2]|(unsigned)ids[k*2+1]<<8);unsigned char point[16];
                    if(index<0 || (unsigned)index>=nv || ts_bytes(s,vb+(unsigned)index*12,point+4,12))return -1;
                    put(point,bone,4);for(unsigned k2=0;k2<3;k2++)if(!isfinite(enemy_float(point+4+k2*4)))return -1;
                    unsigned match;for(match=0;match<nvertices;match++)if(!memcmp(data+vertices->offset+match*16,point,16))break;
                    if(match==nvertices){if(nvertices>=vertices->size/16)return -1;memcpy(data+vertices->offset+nvertices++*16,point,16);}
                    put(out+corner*4,match,4);
                }
                put(out+12,kind>>16,2);put(out+14,kind&65535,2);put(out+16,(!!(flags&0x80000000))|(wind?0:2),2);
            }
        }
    }
    return nvertices*16==vertices->size && ntriangles*20==triangles->size?0:-1;
}
static int populate_train_data(JgrDisc *disc,const JgrDiscFile *exe,const TableRecipe *r,unsigned char *data)
{
    unsigned stage=r->special==49?5:3;TableStage s;memset(&s,0,sizeof s);s.disc=disc;s.exe=*exe;char path[80];snprintf(path,sizeof path,"decompile/afs/STAGE%u/000_st%udat.bin",stage,stage);
    if(jgr_disc_source(disc,path,&s.stage)||r->section_count!=(stage==5?4u:8u))return -1;
    unsigned body=stage==5?0:2;
    if(stage==3) {
        if(r->sections[0].size!=64 || r->sections[1].size!=400)return -1;
        for(unsigned i=0;i<8;i++){unsigned char row[8];if(ts_bytes(&s,0x8c1b9c60+i*8,row,8))return -1;float value=enemy_float(row+4);if(!isfinite(value)||value<0||value>4294967295.)return -1;put(data+r->sections[0].offset+i*8,word(row),4);put(data+r->sections[0].offset+i*8+4,(uint32_t)value,4);}
        uint32_t table;if(ts_word(&s,0x8c031e00,&table))return -1;
        for(unsigned i=0;i<5;i++){uint32_t address;unsigned index=i<4?5+i:13;if(ts_word(&s,table+index*4,&address)||ts_bytes(&s,address,data+r->sections[1].offset+i*80,64))return -1;}
    }
    TableCollider nodes[64];unsigned count;if(table_colliders(&s,stage==5?0x8cc419b4:0x8cd75300,nodes,&count)||count*148!=r->sections[body].size)return -1;
    for(unsigned i=0;i<count;i++){TableCollider *node=nodes+i;unsigned char *out=data+r->sections[body].offset+i*148;if(node->kind!=4 || node->flags!=24 || ts_bytes(&s,node->address+12,out+4,36)||ts_bytes(&s,node->mesh,out+40,96)||ts_bytes(&s,node->address+48,out+136,12))return -1;put(out,(uint32_t)node->parent,4);}
    if(table_train_surfaces(&s,stage==5?0x8cc427dc:0x8cd762fc,r->sections+body+1,r->sections+body+2,data))return -1;
    if(stage==3 && table_train_surfaces(&s,0x8cd76128,r->sections+body+3,r->sections+body+4,data))return -1;
    return 0;
}
static int table_book_model(JgrDisc *disc,const JgrDiscFile *bank,uint32_t cell,uint32_t motion,uint32_t pairs[256][2],unsigned *count,unsigned char *out)
{
    uint32_t root,mot=0,frames=0;if(table_word(disc,bank,0x8cf00000u,cell,&root) || (motion && table_word(disc,bank,0x8cf00000u,motion,&mot)) || (mot && table_word(disc,bank,0x8cf00000u,mot,&frames)))return -1;
    unsigned i;for(i=0;i<*count;i++)if(pairs[i][0]==root && pairs[i][1]==mot)break;
    if(i==*count){if(i>=256)return -1;pairs[i][0]=root;pairs[i][1]=mot;(*count)++;}
    put(out,i,4);put(out+4,mot?frames>>16:0,4);return 0;
}
static int populate_record_book(JgrDisc *disc,const JgrDiscFile *exe,const TableRecipe *r,unsigned char *data)
{
    static const uint32_t sources[][2]={
     {0x8c10e738,0x8c10e754},{0x8c10e770,0x8c10e784},{0x8c10e798,0x8c10e7a8},{0x8c10e7b8,0x8c10e7cc},{0x8c10e7e0,0x8c10e7e8},
     {0x8c1b9d08,0x8c1b9d50},{0x8c1b9cf0,0x8c1b9cfc},{0x8c1b9dc8,0x8c1b9dec},{0x8c1b9dd8,0x8c1b9dec},{0x8c1b9d98,0x8c1b9db0},{0x8c1b9e00,0x8c1b9e54},
     {0x8c1b9fa0,0x8c1b9fac},{0x8c1b9fb8,0x8c1b9fc8},{0x8c1b9fd8,0x8c1b9fe0},{0x8c1b9fe8,0x8c1b9ff0},{0x8c1b9ff8,0x8c1ba000},{0x8c1ba008,0x8c1ba010},
     {0x8c1ba018,0x8c1ba028},{0x8c1ba038,0x8c1ba044},{0x8c1ba050,0x8c1ba05c},{0x8c1ba068,0x8c1ba074},{0x8c1ba080,0x8c1ba08c}};
    JgrDiscFile bank,garage;uint32_t pairs[256][2],list;unsigned count=0,used=0;
    if(r->section_count!=40 || r->fixups!=68 || jgr_disc_source(disc,"decompile/afs/U_SYSOPTION/001_sysoptiondat.bin",&bank) || jgr_disc_source(disc,"decompile/afs/U_GARAGE/001_garagedat.bin",&garage))return -1;
    for(unsigned s=0;s<22;s++) {
        const TableSection *section=r->sections+s;unsigned n=section->size/8;if(section->size%8 || n>=32)return -1;
        for(unsigned i=0;i<n;i++){uint32_t cell,motion;if(table_word(disc,exe,0x8c010000u,sources[s][0]+i*4,&cell)||!cell||table_word(disc,exe,0x8c010000u,sources[s][1]+i*4,&motion)||table_book_model(disc,&bank,cell,motion,pairs,&count,data+section->offset+i*8))return -1;}
        uint32_t end;if(table_word(disc,exe,0x8c010000u,sources[s][0]+n*4,&end)||end)return -1;
    }
    for(unsigned i=0;i<2;i++)if(r->sections[22+i].size!=8||table_book_model(disc,&bank,0x8cf00194+i*8,0x8cf00198+i*8,pairs,&count,data+r->sections[22+i].offset))return -1;
    if(r->sections[24].size!=15*8 || r->sections[38].size!=68*4)return -1;
    for(unsigned i=0;i<15;i++){uint32_t cell;if(table_word(disc,exe,0x8c010000u,0x8c1b9ea8+i*4,&cell)||table_book_model(disc,&bank,cell,0,pairs,&count,data+r->sections[24].offset+i*8))return -1;}
    if(table_word(disc,&garage,0x8c800000u,0x8c80034c,&list))return -1;
    for(unsigned i=0;i<68;i++){
        uint32_t pointer;unsigned char text[512]={0};unsigned n=1;
        if(table_word(disc,&garage,0x8c800000u,list+i*4,&pointer))return -1;
        if(pointer>=0x8c800000u && pointer-0x8c800000u<garage.size && table_string(disc,&garage,0x8c800000u,pointer,text,sizeof text,&n))return -1;
        if(table_string_fixup(r,data,i,38,i,text,n,&used))return -1;
    }
    return used==r->sections[39].size?0:-1;
}
typedef struct {uint32_t address;unsigned type,sector;unsigned char raw[52];} TablePlacement;
static int ts_open(TableStage *s,JgrDisc *disc,const JgrDiscFile *exe,unsigned stage)
{
    char path[80];memset(s,0,sizeof *s);s->disc=disc;s->exe=*exe;snprintf(path,sizeof path,"decompile/afs/STAGE%u/000_st%udat.bin",stage,stage);return jgr_disc_source(disc,path,&s->stage);
}
static int ts_is_data(TableStage *s,uint32_t p){return !(p&3)&&p>=0x8cb00000u&&p-0x8cb00000u<s->stage.size;}
static int table_placements(TableStage *s,uint32_t table,unsigned sectors,unsigned stride,unsigned types,TablePlacement **out,unsigned *count)
{
    TablePlacement *rows=calloc(4096,sizeof *rows);unsigned used=0;int result=-1;if(!rows)return -1;
    for(unsigned sector=0;sector<sectors;sector++){
        uint32_t first,p;if(ts_word(s,table+sector*4,&first))goto done;if(!ts_is_data(s,first))continue;if(ts_word(s,first,&p))goto done;int indirect=ts_is_data(s,p);unsigned guard=0;
        for(uint32_t at=first;;at+=indirect?4:stride){uint32_t a=at;unsigned char raw[52];if(indirect){if(ts_word(s,at,&a))goto done;if(!ts_is_data(s,a))break;}if(ts_bytes(s,a,raw,stride))goto done;int type=(int32_t)word(raw);if(!indirect&&type< -1)break;if(++guard>10000)goto done;if(type<0||(unsigned)type>=types)continue;
            if(used>=4096)goto done;
            TablePlacement *r=rows+used++;r->address=a;r->sector=sector;r->type=type;memcpy(r->raw,raw,stride);if(stride==40)for(unsigned k=0;k<3;k++)table_put_float(r->raw+28+k*4,1.f);
        }
    }
    *out=rows;*count=used;rows=NULL;result=0;
 done:free(rows);return result;
}
static void tm_identity(double m[16]){memset(m,0,16*sizeof(double));for(unsigned i=0;i<4;i++)m[i*5]=1;}
static void tm_multiply(double out[16],const double a[16],const double b[16]){double temp[16];for(unsigned i=0;i<4;i++)for(unsigned j=0;j<4;j++){double v=0;for(unsigned k=0;k<4;k++)v+=a[i*4+k]*b[k*4+j];temp[i*4+j]=v;}memcpy(out,temp,sizeof temp);}
static void tm_rotation(double m[16],unsigned axis,double a){tm_identity(m);double c=cos(a),s=sin(a);if(axis==0){m[5]=c;m[6]=-s;m[9]=s;m[10]=c;}else if(axis==1){m[0]=c;m[2]=s;m[8]=-s;m[10]=c;}else{m[0]=c;m[1]=-s;m[4]=s;m[5]=c;}}
static int tm_pose(double out[16],const unsigned char *raw){
    double translate[16],scale[16],rotate[3][16],a[16],b[16];tm_identity(translate);tm_identity(scale);
    for(unsigned k=0;k<3;k++){double p=enemy_float(raw+k*4),sc=enemy_float(raw+24+k*4);if(!isfinite(p)||!isfinite(sc))return -1;translate[k*4+3]=p;scale[k*5]=sc;tm_rotation(rotate[k],k,(int32_t)word(raw+12+k*4)*(3.141592653589793*2/65536));}
    tm_multiply(a,rotate[1],rotate[0]);tm_multiply(b,rotate[2],a);tm_multiply(a,translate,b);tm_multiply(out,a,scale);return 0;
}
static int tm_point(const double matrix[16],const unsigned char point[12],unsigned char out[12]){
    double p[3];for(unsigned i=0;i<3;i++){p[i]=enemy_float(point+i*4);if(!isfinite(p[i]))return -1;}
    for(unsigned i=0;i<3;i++){double d=matrix[i*4]*p[0]+matrix[i*4+1]*p[1]+matrix[i*4+2]*p[2]+matrix[i*4+3];if(!isfinite(d))return -1;table_put_float(out+i*4,(float)d);}return 0;
}
static int table_boxes(TableStage *s,uint32_t root,unsigned char *out,unsigned capacity,unsigned *written){
    TableCollider nodes[64];unsigned count;double matrices[64][16];if(table_colliders(s,root,nodes,&count)||count>capacity)return -1;
    for(unsigned i=0;i<count;i++){unsigned char raw[36],point[12];double pose[16];TableCollider *n=nodes+i;if(n->kind!=4||n->flags!=25||ts_bytes(s,n->address+12,raw,36)||tm_pose(pose,raw))return -1;if(n->parent<0)memcpy(matrices[i],pose,sizeof pose);else if((unsigned)n->parent>=i)return -1;else tm_multiply(matrices[i],matrices[n->parent],pose);
        for(unsigned v=0;v<9;v++){if(ts_bytes(s,v<8?n->mesh+v*12:n->address+48,point,12)||tm_point(matrices[i],point,out+i*108+v*12))return -1;}
    }
    *written=count;return 0;
}
static int table_coltri(TableStage *s,uint32_t root,unsigned char *out,unsigned capacity,unsigned *written){
    TableCollider nodes[64];unsigned count,total=0;if(table_colliders(s,root,nodes,&count))return -1;
    for(unsigned bone=0;bone<count;bone++){TableCollider *node=nodes+bone;if(!node->kind)continue;
        if(node->kind!=6)return -1;
        unsigned char head[12];if(ts_bytes(s,node->mesh,head,12))return -1;uint32_t vb=word(head),pb=word(head+4);unsigned nv=head[8]|(unsigned)head[9]<<8,np=head[10]|(unsigned)head[11]<<8;
        for(unsigned j=0;j<np;j++){unsigned char p[12];if(ts_bytes(s,pb+j*36+24,p,12))return -1;uint32_t ip=word(p),flags=word(p+4),kind=word(p+8);unsigned n=flags&65535,wind=!!(flags&0x40000000u);if(n>capacity-total)return -1;
            for(unsigned tri=0;tri<n;tri++,wind=!wind){unsigned char ids[6],*dest=out+total++*44;if(ts_bytes(s,ip+tri*2,ids,6))return -1;for(unsigned k=0;k<3;k++){unsigned at=k?(wind?k:3-k):0;int index=(int16_t)(ids[at*2]|(unsigned)ids[at*2+1]<<8);if(index<0||(unsigned)index>=nv||ts_bytes(s,vb+(unsigned)index*12,dest+k*12,12))return -1;}put(dest+36,kind>>16,2);put(dest+38,kind&65535,2);put(dest+40,(!!(flags&0x80000000u))|(wind?0:2),2);put(dest+42,2,2);}
        }
    }
    *written=total;return 0;
}
static int populate_openings(JgrDisc *disc,const JgrDiscFile *exe,const TableRecipe *r,unsigned char *data){
    unsigned stage=r->special==56?3:5;TableStage s;TablePlacement *places=NULL;unsigned nplaces=0,used=0,boxes=0;int result=-1;
    static const unsigned types3[]={23,48,6,47,50},types5[]={1,5};
    static const uint32_t motions3[]={0x8cd99c98,0x8cd9c710,0x8cd98ee0,0x8cd9faa8,0x8cda0dec},motions5[]={0x8cc54544,0x8cc545e8};
    const unsigned *types=stage==3?types3:types5;const uint32_t *motions=stage==3?motions3:motions5;unsigned count=stage==3?5:2;
    if(r->section_count!=(stage==3?4u:5u)||r->sections[0].size!=count*28||r->sections[1].size!=count*108||r->sections[2].size%48||ts_open(&s,disc,exe,stage)||table_placements(&s,stage==3?0x8c1ba918:0x8c203298,stage==3?46:71,52,stage==3?70:20,&places,&nplaces))goto done;
    for(unsigned i=0;i<count;i++){
        uint32_t root,header;unsigned n;unsigned type=types[i],kind,sound,bank=16;if(stage==3){kind=type==23?0:type==48?1:type==50?6:5;sound=type==23||type==50?31:type==48?5:type==6?34:33;if(type==48)bank=4;}else{kind=type==1?2:3;sound=type==1?30:39;}
        if(ts_word(&s,(stage==3?0x8c1bc294:0x8c204084)+type*4,&root)||ts_word(&s,motions[i],&header)||table_boxes(&s,root,data+r->sections[1].offset+boxes*108,count-boxes,&n))goto done;
        unsigned values[7]={type,kind,header>>16,boxes,n,sound,bank};for(unsigned k=0;k<7;k++)put(data+r->sections[0].offset+i*28+k*4,values[k],4);boxes+=n;
    }
    for(unsigned i=0;i<nplaces;i++){TablePlacement *p=places+i;unsigned type;for(type=0;type<count;type++)if(types[type]==p->type)break;if(type==count)continue;if(used>=r->sections[2].size/48)goto done;unsigned char *dest=data+r->sections[2].offset+used++*48;put(dest,p->address,4);put(dest+4,type,4);put(dest+8,p->sector,4);memcpy(dest+12,p->raw+4,36);}
    if(used*48!=r->sections[2].size||boxes*108!=r->sections[1].size)goto done;
    if(stage==5){unsigned n1,n2;if(r->sections[4].size!=176||table_coltri(&s,0x8cc3e2f8,data+r->sections[4].offset,4,&n1)||table_coltri(&s,0x8cc3e3a4,data+r->sections[4].offset+n1*44,4-n1,&n2)||n1+n2!=4)goto done;}
    result=0;
 done:free(places);return result;
}
static int populate_house(JgrDisc *disc,const JgrDiscFile *exe,const TableRecipe *r,unsigned char *data){
    static const unsigned types[]={4,5,17,18,58,76,85,86,87};
    static const uint32_t motions[]={0x8c107d00,0x8c107cfc,0x8c107d10,0x8c107d20,0x8c107d24,0x8c107d1c,0x8c107d14,0x8c107d18,0x8c107d10};
    TableStage s;TablePlacement *places=NULL;unsigned nplaces=0,used=0,boxes=0;int result=-1;
    if(r->section_count!=5||r->sections[1].size!=9*20||r->sections[2].size%108||r->sections[3].size%36||ts_open(&s,disc,exe,2)||table_placements(&s,0x8c108284,114,40,91,&places,&nplaces))goto done;
    for(unsigned i=0;i<9;i++) {uint32_t root,motion,header;unsigned n,type=types[i],sound=type==4?0:type==5||type==18?35:type==17?37:39;
        if(ts_word(&s,0x8c10a2dc+type*4,&root)||ts_word(&s,motions[i],&motion)||ts_word(&s,motion,&header)||table_boxes(&s,root,data+r->sections[2].offset+boxes*108,r->sections[2].size/108-boxes,&n))goto done;
        unsigned values[5]={type,header>>16,boxes,n,sound};for(unsigned k=0;k<5;k++)put(data+r->sections[1].offset+i*20+k*4,values[k],4);boxes+=n;
    }
    for(unsigned i=0;i<nplaces;i++){TablePlacement *p=places+i;unsigned type;for(type=0;type<9;type++)if(types[type]==p->type)break;if(type==9)continue;if(used>=r->sections[3].size/36)goto done;unsigned char *dest=data+r->sections[3].offset+used++*36;put(dest,p->address,4);put(dest+4,type,4);put(dest+8,p->sector,4);memcpy(dest+12,p->raw+4,24);}
    result=used*36==r->sections[3].size&&boxes*108==r->sections[2].size?0:-1;
 done:free(places);return result;
}
static int populate_crane(JgrDisc *disc,const JgrDiscFile *exe,const TableRecipe *r,unsigned char *data){
    TableStage s;TablePlacement *places=NULL;unsigned nplaces=0,used=0;int result=-1;
    if(r->section_count!=5||r->sections[0].size!=44||r->sections[1].size!=88||r->sections[2].size%12||r->sections[3].size%20||ts_open(&s,disc,exe,3)||table_placements(&s,0x8c1ba918,46,52,70,&places,&nplaces))goto done;
    for(unsigned i=0;i<nplaces;i++)if(places[i].type==46){if(used++)goto done;unsigned char *dest=data+r->sections[0].offset;put(dest,places[i].address,4);put(dest+4,places[i].sector,4);memcpy(dest+8,places[i].raw+4,36);}
    if(used!=1)goto done;
    uint32_t root;if(ts_word(&s,0x8c1bc294+46*4,&root))goto done;TableCollider nodes[64];unsigned count;if(table_colliders(&s,root,nodes,&count)||count!=2)goto done;
    for(unsigned i=0;i<count;i++){unsigned char raw[36],*out=data+r->sections[1].offset+i*44;if(ts_bytes(&s,nodes[i].address+12,raw,36))goto done;memcpy(out,raw,12);memcpy(out+12,raw+24,12);memcpy(out+24,raw+12,12);}
    unsigned char head[12];if(nodes[0].kind||nodes[1].kind!=6||ts_bytes(&s,nodes[1].mesh,head,12))goto done;uint32_t vb=word(head),pb=word(head+4);unsigned nv=head[8]|(unsigned)head[9]<<8,np=head[10]|(unsigned)head[11]<<8;
    if(nv*12!=r->sections[2].size||ts_bytes(&s,vb,data+r->sections[2].offset,nv*12))goto done;
    used=0;
    for(unsigned j=0;j<np;j++){unsigned char p[12];if(ts_bytes(&s,pb+j*36+24,p,12))goto done;uint32_t ip=word(p),flags=word(p+4),kind=word(p+8);unsigned n=flags&65535,wind=!!(flags&0x40000000u);if(n>r->sections[3].size/20-used)goto done;
        for(unsigned tri=0;tri<n;tri++,wind=!wind){unsigned char ids[6],*dest=data+r->sections[3].offset+used++*20;if(ts_bytes(&s,ip+tri*2,ids,6))goto done;for(unsigned k=0;k<3;k++){unsigned at=k?(wind?k:3-k):0;int index=(int16_t)(ids[at*2]|(unsigned)ids[at*2+1]<<8);if(index<0||(unsigned)index>=nv)goto done;put(dest+k*4,(unsigned)index,4);}put(dest+12,kind>>16,2);put(dest+14,kind&65535,2);put(dest+16,(!!(flags&0x80000000u))|(wind?0:2),2);}
    }
    result=used*20==r->sections[3].size?0:-1;
 done:free(places);return result;
}
static int populate_arrow_skater(JgrDisc *disc,const TableRecipe *r,unsigned char *data){
    unsigned seen=0;if(r->section_count!=1||r->sections[0].size!=384)return -1;
    for(unsigned model=0;model<16;model++){JgrDiscFile file;uint32_t table,identity,descriptor;if(jgr_disc_source(disc,table_player_sources[model],&file)||table_word(disc,&file,0,8,&table)||table_word(disc,&file,0,table,&identity)||identity>=16||(seen&(1u<<identity))||table_word(disc,&file,0,table+0x74,&descriptor)||table_bytes(disc,&file,0,descriptor+0x40,data+r->sections[0].offset+identity*24,24))return -1;seen|=1u<<identity;}
    return seen==65535?0:-1;
}

/* Preserve the shipped XYZ contact orientation for the retractable construction
 * bodies; ordinary native placements use ZYX. No vertex is stored in recipes. */
static int tm_legacy(double out[16],const unsigned char raw[36])
{
    double translate[16],scale[16],rotate[3][16],a[16],b[16];tm_identity(translate);tm_identity(scale);
    for(unsigned k=0;k<3;k++){double p=enemy_float(raw+k*4),sc=enemy_float(raw+24+k*4);if(!isfinite(p)||!isfinite(sc))return -1;translate[k*4+3]=p;scale[k*5]=sc;tm_rotation(rotate[k],k,(int32_t)word(raw+12+k*4)*(3.141592653589793*2/65536));}
    tm_multiply(a,rotate[1],rotate[2]);tm_multiply(b,rotate[0],a);tm_multiply(a,translate,b);tm_multiply(out,a,scale);return 0;
}
static unsigned table_kogane_contact(unsigned type)
{
    switch(type){
    case 2:case 6:case 10:case 11:case 13:case 14:case 16:case 22:case 34:case 43:case 64:case 15:case 35:case 36:case 62:case 63:case 65:case 70:case 88:case 89:case 27:case 28:case 29:case 66:case 59:case 60:return 0x1f;
    case 3:case 12:case 20:case 21:case 90:case 67:return 0x1b;
    case 4:case 8:case 19:case 30:return 0x21;case 5:case 18:return 0x23;
    case 7:return 8;case 17:return 0x25;case 23:case 24:case 25:case 26:return 0x1e;
    case 58:case 76:case 85:case 86:case 87:return 0x27;default:return 0x1c;
    }
}
static int populate_cover(JgrDisc *disc,const JgrDiscFile *exe,const TableRecipe *r,unsigned char *data)
{
    TableStage s;TablePlacement *open=NULL,*closed=NULL;unsigned no=0,nc=0,used=0,closed_count=0;int result=-1;
    if(r->section_count!=4||r->sections[0].size!=12||r->sections[2].size%44||r->sections[3].size%44||ts_open(&s,disc,exe,2)||table_placements(&s,0x8c108668,1,40,91,&open,&no)||table_placements(&s,0x8c108684,1,40,91,&closed,&nc))goto done;
    unsigned char record[40],pose[36];uint32_t root;double parent[16],local[16],matrix[16];
    if(ts_bytes(&s,0x8cd725e8,record,40)||word(record)>=77||ts_word(&s,0x8c109bf8+word(record)*4,&root)||ts_bytes(&s,root+12,pose,36)||tm_pose(local,pose))goto done;
    memcpy(data+r->sections[0].offset,record+4,12);memcpy(pose,record+4,24);for(unsigned k=0;k<3;k++)table_put_float(pose+24+k*4,1.f);
    if(tm_pose(parent,pose)||table_coltri(&s,root,data+r->sections[2].offset,r->sections[2].size/44,&closed_count)||closed_count*44!=r->sections[2].size)goto done;
    tm_multiply(matrix,parent,local);
    for(unsigned i=0;i<closed_count;i++){unsigned char *out=data+r->sections[2].offset+i*44;for(unsigned k=0;k<3;k++){unsigned char point[12];memcpy(point,out+k*12,12);if(tm_point(matrix,point,out+k*12))goto done;}put(out+42,1,2);}
    for(unsigned i=0;i<no;i++){
        TablePlacement *p=open+i;unsigned j;for(j=0;j<nc;j++)if(!memcmp(p->raw,closed[j].raw,28))break;if(j<nc)continue;
        if(ts_word(&s,0x8c10a2dc+p->type*4,&root)||tm_legacy(parent,p->raw+4))goto done;
        TableCollider nodes[64];unsigned count;double matrices[64][16];if(table_colliders(&s,root,nodes,&count))goto done;
        for(unsigned b=0;b<count;b++){
            TableCollider *n=nodes+b;if(n->kind!=4||ts_bytes(&s,n->address+12,pose,36)||tm_legacy(local,pose))goto done;
            tm_multiply(matrices[b],n->parent<0?parent:matrices[n->parent],local);
            unsigned char raw[20];if(ts_bytes(&s,n->mesh,raw,sizeof raw))goto done;
            double hx=enemy_float(raw),y0=enemy_float(raw+4),hz=enemy_float(raw+8),y1=enemy_float(raw+16);
            double points[8][3]={{-hx,y0,-hz},{hx,y0,-hz},{hx,y0,hz},{-hx,y0,hz},{-hx,y1,-hz},{hx,y1,-hz},{hx,y1,hz},{-hx,y1,hz}};
            static const unsigned char faces[12][3]={{4,7,6},{4,6,5},{0,1,2},{0,2,3},{0,4,5},{0,5,1},{1,5,6},{1,6,2},{2,6,7},{2,7,3},{3,7,4},{3,4,0}};
            if(r->sections[3].size/44-used<12)goto done;
            for(unsigned f=0;f<12;f++){
                unsigned char *out=data+r->sections[3].offset+used++*44;
                for(unsigned v=0;v<3;v++)for(unsigned a=0;a<3;a++){
                    const double *m=matrices[b],*pt=points[faces[f][v]];double value=m[a*4]*pt[0]+m[a*4+1]*pt[1]+m[a*4+2]*pt[2]+m[a*4+3];if(!isfinite(value))goto done;table_put_float(out+(v*3+a)*4,(float)value);
                }
                put(out+36,table_kogane_contact(p->type),2);put(out+38,65535,2);put(out+42,2,2);
            }
        }
    }
    result=used*44==r->sections[3].size?0:-1;
 done:free(open);free(closed);return result;
}
/* Geometry IDs are resolved through the freshly generated owner directory by
 * the deferred fill step. Temporary IDs below are original placement keys. */
static int populate_shibuya_bindings(JgrDisc *disc,const JgrDiscFile *exe,const TableRecipe *r,unsigned char *data)
{
    TableStage s;TablePlacement *places=NULL;unsigned count=0,used=0;int result=-1,doors=r->special==65;
    if(ts_open(&s,disc,exe,1)||table_placements(&s,0x8c10618c,63,40,28,&places,&count))goto done;
    if(!doors){uint32_t root;unsigned n;
        if(r->section_count!=2||r->sections[0].size%64||r->sections[1].size%44||ts_word(&s,0x8c107644,&root)||table_coltri(&s,root,data+r->sections[1].offset,r->sections[1].size/44,&n)||n*44!=r->sections[1].size)goto done;
    }else{
        static const uint32_t motions[4][2]={{0x8c105e64,0x8c105e68},{0x8c105e70,0x8c105e6c},{0x8c105e78,0x8c105e74},{0x8c105e7c,0x8c105e80}};
        if(r->section_count!=6||r->sections[0].size!=112||r->sections[1].size!=32||r->sections[2].size!=432||r->sections[3].size%48||r->sections[4].size%36||r->sections[5].size!=63*32)goto done;
        for(unsigned i=0;i<4;i++){uint32_t root;unsigned boxes,frames=0;
            for(unsigned m=0;m<2;m++){uint32_t ptr,n;if(ts_word(&s,motions[i][m],&ptr)||ts_word(&s,ptr+4,&n)||(m&&n!=frames))goto done;frames=n;put(data+r->sections[1].offset+(i*2+m)*4,ptr,4);}
            unsigned fields[7]={i+6,4,frames,i,1,5,4};for(unsigned k=0;k<7;k++)put(data+r->sections[0].offset+i*28+k*4,fields[k],4);
            if(ts_word(&s,0x8c107644+(i+6)*4,&root)||table_boxes(&s,root,data+r->sections[2].offset+i*108,1,&boxes)||boxes!=1)goto done;
        }
        /* Port lighting calibration, shared with the existing native door path. */
        const float light[8]={1,1,1,-1,-.71f,-1,.95f,.47f};
        for(unsigned i=0;i<63;i++)for(unsigned k=0;k<8;k++)table_put_float(data+r->sections[5].offset+i*32+k*4,light[k]);
    }
    for(unsigned i=0;i<count;i++){
        TablePlacement *p=places+i;if(doors?(p->type<6||p->type>9):p->type!=0)continue;
        unsigned section=doors?3:0,width=doors?48:64;if(used>=r->sections[section].size/width)goto done;unsigned char *out=data+r->sections[section].offset+used*width;
        if(doors){if(used>=r->sections[4].size/36)goto done;put(out,p->address,4);put(out+4,p->type-6,4);put(out+8,p->sector,4);memcpy(out+12,p->raw+4,36);put(data+r->sections[4].offset+used*36,p->address,4);}
        else {put(out,p->sector,4);put(out+4,p->address,4);memcpy(out+8,p->raw+4,24);}
        used++;
    }
    result=used*(doors?48:64)==r->sections[doors?3:0].size?0:-1;
 done:free(places);return result;
}
static int table_model_count(TableStage *s,uint32_t root,unsigned *result)
{
    uint32_t stack[256],seen[1024];unsigned sp=0,nseen=0,count=0;uint32_t first_mesh=0;stack[sp++]=root;
    while(sp){uint32_t p=stack[--sp];unsigned char raw[52];if(!p)continue;for(unsigned i=0;i<nseen;i++)if(seen[i]==p)return -1;if(nseen==1024||ts_bytes(s,p,raw,sizeof raw))return -1;seen[nseen++]=p;
        unsigned ev=word(raw);if(word(raw+4)&&!(ev&8)){if(!count)first_mesh=word(raw+4);count++;}if(sp>254)return -1;stack[sp++]=word(raw+48);if(!(ev&16))stack[sp++]=word(raw+44);
    }
    /* The first lamp mesh is the animated slot-zero surface. Verify this
       from its CNK material stream before binding the first output batch. */
    uint32_t at;if(!first_mesh||ts_word(s,first_mesh+4,&at))return -1;
    unsigned guard=0;for(;;){unsigned char h[4];if(++guard>4096||ts_bytes(s,at,h,2))return -1;unsigned kind=h[0];if(kind==255)break;
        if(kind<8){at+=2;continue;}if(ts_bytes(s,at,h,4))return -1;
        unsigned n=h[2]|(unsigned)h[3]<<8;if(kind==8||kind==9){if(n&8191)return -1;at+=4;}else at+=(n+2)*2;
    }
    *result=count;return 0;
}
static int populate_grind(JgrDisc *disc,const JgrDiscFile *exe,const TableRecipe *r,unsigned char *data)
{
    TableStage s;TablePlacement *places=NULL;unsigned count=0,lifts=0,lights=0,objects=0,triangles=0;int result=-1;
    if(r->section_count!=8||r->sections[0].size%36||r->sections[5].size%52||r->sections[7].size%64||ts_open(&s,disc,exe,6)||table_placements(&s,0x8c20ae08,81,52,36,&places,&count))goto done;
    for(unsigned i=0;i<count;i++){
        TablePlacement *p=places+i;if(p->type==10){if(lifts>=r->sections[0].size/36)goto done;memcpy(data+r->sections[0].offset+lifts++*36,p->raw+4,36);}
        unsigned type=p->type;if(type>=10&&type!=13&&type!=25&&type!=34)continue;
        if(lights>=r->sections[7].size/64)goto done;
        uint32_t root,animation,keys,channels,descriptor,n;unsigned nobjects;
        uint32_t at=type<10?0x8c0f51fc+type*16:type==13?0x8c0f52ac:type==25?0x8c0f535c:0x8c0f53f4;
        if(ts_word(&s,0x8c20b37c+type*4,&root)||table_model_count(&s,root,&nobjects)||ts_word(&s,at,&animation)||ts_word(&s,animation,&descriptor)||ts_word(&s,animation+4,&channels)||channels!=1||ts_word(&s,descriptor+4,&keys)||ts_word(&s,descriptor+8,&n)||n!=8)goto done;
        unsigned char *out=data+r->sections[7].offset+lights++*64;put(out,objects,4);put(out+4,nobjects,4);objects+=nobjects;unsigned time=0;
        for(unsigned k=0;k<8;k++){uint32_t dt;if(ts_word(&s,keys+k*16+8,&dt)||dt>UINT32_MAX-time)goto done;time+=dt;put(out+16+k*4,time,4);}
        if(time==UINT32_MAX)goto done;
        put(out+12,time+1,4);memcpy(out+48,p->raw+4,12);put(out+60,type<10?10:2,4);
    }
    for(unsigned group=0;group<2;group++){
        uint32_t root=0x8cc6a594;TableCollider nodes[64];unsigned n;
        if(group&&ts_word(&s,0x8c20b5bc+40,&root))goto done;
        if(table_colliders(&s,root,nodes,&n)||r->sections[1+group*2].size!=n*44||r->sections[2+group*2].size!=n*4)goto done;
        for(unsigned b=0;b<n;b++){
            TableCollider *node=nodes+b;unsigned char pose[36],*dest=data+r->sections[1+group*2].offset+b*44;
            if(ts_bytes(&s,node->address+12,pose,36))goto done;
            memcpy(dest,pose,12);memcpy(dest+12,pose+24,12);memcpy(dest+24,pose+12,12);put(data+r->sections[2+group*2].offset+b*4,(unsigned)node->parent,4);
            if(group){if(!b){if(node->kind!=4||r->sections[6].size!=96||ts_bytes(&s,node->mesh,data+r->sections[6].offset,96))goto done;}else if(node->kind)goto done;continue;}
            if(!node->kind)continue;
            if(node->kind!=6)goto done;
            unsigned char header[12];if(ts_bytes(&s,node->mesh,header,12))goto done;uint32_t vb=word(header),pb=word(header+4);unsigned nv=header[8]|(unsigned)header[9]<<8,np=header[10]|(unsigned)header[11]<<8;
            for(unsigned patch=0;patch<np;patch++){unsigned char h[12];if(ts_bytes(&s,pb+patch*36+24,h,12))goto done;unsigned flags=word(h+4),num=flags&65535,wind=!!(flags&0x40000000u);uint32_t ip=word(h),kind=word(h+8);
                if(num>r->sections[5].size/52-triangles)goto done;
                for(unsigned t=0;t<num;t++,wind=!wind){unsigned char indices[6],*out=data+r->sections[5].offset+triangles++*52;if(ts_bytes(&s,ip+t*2,indices,6))goto done;
                    for(unsigned v=0;v<3;v++){unsigned k=v?(wind?v:3-v):0;int index=(int16_t)(indices[k*2]|(unsigned)indices[k*2+1]<<8);if(index<0||(unsigned)index>=nv||ts_bytes(&s,vb+(unsigned)index*12,out+v*12,12))goto done;}
                    put(out+36,b,4);put(out+40,kind>>16,4);put(out+44,kind&65535,4);put(out+48,(!!(flags&0x80000000u))|(wind?0:2),4);
                }
            }
        }
    }
    result=lifts*36==r->sections[0].size&&lights*64==r->sections[7].size&&triangles*52==r->sections[5].size?0:-1;
 done:free(places);return result;
}
