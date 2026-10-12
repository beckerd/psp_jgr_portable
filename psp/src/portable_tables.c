#include "portable_tables.h"
#include "portable_convert.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef JgrTableSection TableSection;
typedef struct {
    uint32_t address,add,destination;
    unsigned char source_kind,output_kind,flags;
    signed char precision;
} TableField;
typedef struct {
    const char *output,*source;uint32_t base;
    const TableSection *sections;unsigned section_count;
    const TableField *fields;unsigned field_count,sort;
    uint32_t bgm;unsigned bgm_count,bgm_names,special,version,fixups;
} TableRecipe;
typedef struct {
    const char *source;uint32_t table;unsigned count,root_index;
    int alias_destination,alias_source;
} EnemyRecipe;
#include "portable_tables_data.inc"

static uint32_t word(const unsigned char *p)
{return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;}
static void put(unsigned char *p,uint32_t value,unsigned width)
{unsigned i;for(i=0;i<width;i++)p[i]=(unsigned char)(value>>(i*8));}
static uint32_t crc32(const unsigned char *p,unsigned size)
{
    uint32_t c=0xffffffffu;unsigned i;
    while(size--){c^=*p++;for(i=0;i<8;i++)c=(c>>1)^(0xedb88320u & (0u-(c&1u)));}
    return c^0xffffffffu;
}
static const TableRecipe *find_recipe(const char *name)
{
    unsigned i;if(!name)return NULL;
    for(i=0;i<sizeof table_recipes/sizeof table_recipes[0];i++)
        if(!strcmp(name,table_recipes[i].output))return table_recipes+i;
    return NULL;
}
int jgr_convert_table_known(const char *name) {return find_recipe(name)!=NULL;}
int jgr_convert_table_deferred(const char *name) {const TableRecipe *r=find_recipe(name);return r && r->special>=63;}
static int read_value(JgrDisc *disc,const JgrDiscFile *file,uint32_t base,uint32_t address,
                      unsigned kind,double *value)
{
    unsigned char bytes[4];uint32_t v;unsigned width;
    if(kind=='x') {
        double pointer,instruction;uint32_t pc;
        if(read_value(disc,file,base,address,'I',&pointer))return -1;
        pc=(uint32_t)pointer;
        if(pc>UINT32_MAX-2 || read_value(disc,file,base,pc,'H',&instruction) ||
           ((uint32_t)instruction&0xff00u)!=0xd000)return -1;
        pc+=2;
        if(read_value(disc,file,base,pc,'H',&instruction))return -1;
        v=(uint32_t)instruction;
        if((v&0xf000u)==0xa000) {
            int displacement=(int)(v&4095);int64_t target;
            if(displacement>=2048)displacement-=4096;
            target=(int64_t)pc+4+displacement*2;
            if(target<0 || target>UINT32_MAX)return -1;
            pc=(uint32_t)target;
            if(read_value(disc,file,base,pc,'H',&instruction))return -1;
            v=(uint32_t)instruction;
        }
        if((v&0xff00u)!=0xe200)return -1;
        *value=v&255;return 0;
    }
    if(kind=='m') {
        double pointer;
        if(read_value(disc,file,base,address,'I',&pointer))return -1;
        v=(uint32_t)pointer;
        if(v>=0x8c800140u && v<=0x8c800150u && !(v&3))*value=(v-0x8c800140u)/4;
        else if(v>=0x8c800158u && v<=0x8c800164u && !(v&3))*value=(v-0x8c800144u)/4;
        else return -1;
        return 0;
    }
    if(kind=='c') {
        double logical,reference;
        if(address>UINT32_MAX-4 || read_value(disc,file,base,address,'i',&logical) ||
           read_value(disc,file,base,address+4,'i',&reference))return -1;
        if(logical<0)*value=(uint32_t)(int64_t)reference&65535u;
        else if(logical==18) {
            if(reference<0 || reference>=5)return -1;
            *value=((int)reference+3)%5;
        }else *value=reference;
        return 0;
    }
    switch(kind){case 'b':case 'B':width=1;break;case 'h':case 'H':width=2;break;
        case 'i':case 'I':case 'f':width=4;break;default:return -1;}
    if(address<base || jgr_disc_read(disc,file,address-base,bytes,width))return -1;
    v=width==1 ? bytes[0] : width==2 ? bytes[0] | (uint32_t)bytes[1]<<8 : word(bytes);
    switch(kind) {
        case 'b':*value=(int8_t)v;break;case 'h':*value=(int16_t)v;break;
        case 'i':*value=(int32_t)v;break;
        case 'f':{float f;memcpy(&f,&v,4);if(!isfinite(f))return -1;*value=f;break;}
        default:*value=v;break;
    }
    return 0;
}
static int compare_value(const void *a,const void *b)
{double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y);}
static int populate_fields(JgrDisc *disc,const JgrDiscFile *file,const TableRecipe *r,unsigned char *data,unsigned size)
{
    double *values;unsigned i;int result=-1;
    if(r->field_count>4096)return -1;
    values=(double *)calloc(r->field_count ? r->field_count : 1,sizeof *values);if(!values)return -1;
    for(i=0;i<r->field_count;i++) {
        const TableField *f=r->fields+i;uint32_t address=f->address;
        if(f->flags&2)continue;
        if(f->flags&1) {
            double pointer;
            if(read_value(disc,file,r->base,address,'I',&pointer) || pointer>UINT32_MAX-f->add)goto done;
            address=(uint32_t)pointer+f->add;
        }
        if(read_value(disc,file,r->base,address,f->source_kind,values+i))goto done;
        if(f->flags&8)values[i]=(uint32_t)(int64_t)values[i];
    }
    if(r->sort)qsort(values,r->field_count,sizeof *values,compare_value);
    for(i=0;i<r->field_count;i++) {
        const TableField *f=r->fields+i;double value=values[i];uint32_t encoded;unsigned width;
        if(f->flags&4)value=-value;
        if(f->flags&16) {
            uint32_t v=(uint32_t)(int64_t)value;
            value=(v&0xff00ff00u) | ((v>>16)&255u) | ((v&255u)<<16);
        }
        if(f->precision>=0) {
            char text[96];int n=snprintf(text,sizeof text,"%.*f",f->precision,value);
            if(n<0 || (unsigned)n>=sizeof text)goto done;
            value=strtod(text,NULL);
        }
        if(f->output_kind=='f') {float fv=(float)value;memcpy(&encoded,&fv,4);width=4;}
        else {
            if(!isfinite(value) || value<-2147483648.0 || value>4294967295.0)goto done;
            encoded=(uint32_t)(int64_t)value;
            width=(f->output_kind=='b' || f->output_kind=='B') ? 1 :
                  (f->output_kind=='h' || f->output_kind=='H') ? 2 : 4;
        }
        if(f->destination>size || width>size-f->destination)goto done;
        put(data+f->destination,encoded,width);
    }
    result=0;
done:free(values);return result;
}
static int populate_bgm(JgrDisc *disc,const JgrDiscFile *file,const TableRecipe *r,unsigned char *data)
{
    unsigned i;const TableSection *section=r->sections;
    if(r->section_count!=1 || section->size!=r->bgm_count*44)return -1;
    for(i=0;i<r->bgm_count;i++) {
        unsigned char *dest=data+section->offset+i*44;double p,k,zero;uint32_t node,seen[32];unsigned count=0;int loop=-1;
        if(read_value(disc,file,r->base,r->bgm+i*12,'I',&p) ||
           read_value(disc,file,r->base,r->bgm+i*12+4,'I',&k) ||
           read_value(disc,file,r->base,r->bgm+i*12+8,'I',&zero) || zero || k>5)return -1;
        node=(uint32_t)p;
        while(node) {
            unsigned j;double entry,next;
            for(j=0;j<count;j++)if(seen[j]==node){loop=(int)j;break;}
            if(loop>=0)break;
            if(count>=32 || node>UINT32_MAX-4 || read_value(disc,file,r->base,node,'I',&entry) ||
               read_value(disc,file,r->base,node+4,'I',&next) || entry>=r->bgm_names)return -1;
            seen[count]=node;dest[12+count++]=(unsigned char)entry;node=(uint32_t)next;
        }
        if(!count)return -1;
        put(dest,(uint32_t)k,4);put(dest+4,count,4);put(dest+8,(uint32_t)loop,4);
    }
    return 0;
}
static int populate_strings(JgrDisc *disc,const JgrDiscFile *file,const TableRecipe *r,unsigned char *data)
{
    unsigned i,offset=0;const TableSection *strings=r->sections+1;
    if(r->version!=2 || r->section_count!=2 || r->sections[0].size!=r->fixups*4)return -1;
    for(i=0;i<r->fixups;i++) {
        unsigned char raw[24],*fixup=data+40+16*r->section_count+16*i;unsigned length=0;
        uint32_t address=r->special==2 ? 0x8c1d5bccu+i*24 : r->special==18 ? 0x8c1d0fd8u+i*68 : 0x8c1b9f60u+i*7;
        unsigned amount=r->special==2 ? 16 : r->special==18 ? 24 : 6;
        if(address<r->base || jgr_disc_read(disc,file,address-r->base,raw,amount))return -1;
        if(r->special==2) {
            while(length<16 && raw[length])length++;
            if(length==16 || length<5 || memcmp(raw+length-4,".ADX",4))return -1;
            length-=4;
        }else if(r->special==18) {
            while(length<amount && raw[length])length++;
            if(length==amount)return -1;
        }else length=6;
        offset=(offset+3)&~3u;
        if(offset>strings->size || length+1>strings->size-offset)return -1;
        memcpy(data+strings->offset+offset,raw,length);
        put(fixup,0,4);put(fixup+4,i*4,4);put(fixup+8,1,4);put(fixup+12,offset,4);
        offset+=length+1;
    }
    return offset==strings->size ? 0 : -1;
}
#include "portable_tables_enemy.inc"
#include "portable_tables_hud.inc"
#include "portable_tables_text.inc"
#include "portable_tables_controllers.inc"
#include "portable_tables_world.inc"
#include "portable_tables_player.inc"
#include "portable_tables_scenes.inc"
#include "portable_tables_garage.inc"
#include "portable_tables_garage_ui.inc"
#include "portable_tables_effects.inc"
#include "portable_tables_pause.inc"
#include "portable_tables_props.inc"
#include "portable_tables_event.inc"
#include "portable_tables_street.inc"
int jgr_convert_table_fill(JgrDisc *disc,const char *name,FILE *output,JgrTableFill fill,void *context)
{
    const TableRecipe *r=find_recipe(name);JgrDiscFile file;unsigned char *data;unsigned size,header,prefix,i;int result=-1;
    if(!disc || !r || !output || !r->section_count || r->section_count>256)return -1;
    size=r->sections[r->section_count-1].offset+r->sections[r->section_count-1].size;
    prefix=r->version==2 ? 40 : 32;
    header=prefix+16*(r->section_count+r->fixups);if(size>256*1024 || size<header)return -1;
    if(jgr_disc_source(disc,r->source,&file))return -1;
    data=(unsigned char *)calloc(size,1);if(!data)return -1;
    memcpy(data,"JGSD",4);put(data+4,r->version,4);put(data+8,r->section_count,4);put(data+12,size,4);put(data+24,header,4);
    if(r->version==2){put(data+28,r->fixups,4);put(data+32,4,4);}
    for(i=0;i<r->section_count;i++) {
        const TableSection *s=r->sections+i;unsigned char *entry=data+prefix+i*16;
        if(s->offset<header || s->offset>size || s->size>size-s->offset)goto done;
        put(entry,s->identity,4);put(entry+4,s->offset,4);put(entry+8,s->size,4);put(entry+12,s->alignment,4);
    }
    if(r->bgm ? populate_bgm(disc,&file,r,data) : populate_fields(disc,&file,r,data,size))goto done;
    if(r->special==1) {
        const TableSection *gain=r->sections+r->section_count-1;
        if(gain->size!=512)goto done;
        for(i=0;i<256;i++)put(data+gain->offset+i*2,i==255 ? 0 : (uint32_t)(32768.0/pow(2.0,i/16.0)),2);
    }else if(r->special==2 || r->special==3 || r->special==18) {
        if(populate_strings(disc,&file,r,data))goto done;
    }else if(r->special>=4 && r->special<=10) {
        if(populate_enemy(disc,r,data))goto done;
    }else if(r->special>=11 && r->special<=17) {
        if(populate_hud(disc,&file,r,data))goto done;
    }else if(r->special==19 || r->special==20) {
        if(populate_hud_text(disc,&file,r,data))goto done;
    }else if(r->special==21 || r->special==22) {
        if(populate_source_frames(disc,&file,r,data))goto done;
    }else if(r->special>=23 && r->special<=28) {
        if(populate_controller(disc,&file,r,data))goto done;
    }else if(r->special==29) {
        if(populate_exit(disc,&file,r,data))goto done;
    }else if(r->special==30) {
        if(populate_water(disc,&file,r,data))goto done;
    }else if(r->special==31) {
        if(populate_emitter_map(disc,&file,r,data))goto done;
    }else if(r->special==32) {
        if(r->section_count!=2 || populate_player_frames(disc,r->sections+1,data))goto done;
    }else if(r->special==33) {
        if(populate_player_body(disc,r,data))goto done;
    }else if(r->special==34) {
        if(populate_player_motion(disc,&file,r,data))goto done;
    }else if(r->special==35) {
        if(populate_tricks(disc,&file,r,data))goto done;
    }else if(r->special==36) {
        if(populate_intro(disc,&file,r,data))goto done;
    }else if(r->special==37) {
        if(populate_movie(disc,&file,r,data))goto done;
    }else if(r->special==38) {
        if(populate_departure(disc,&file,r,data))goto done;
    }else if(r->special==39) {
        if(populate_rival_graffiti(disc,&file,r,data))goto done;
    }else if(r->special==40) {
        if(populate_reverb(disc,&file,r,data))goto done;
    }else if(r->special==41) {
        if(populate_hardpoints(disc,&file,r,data))goto done;
    }else if(r->special==42) {
        if(populate_skate_anchors(disc,&file,r,data))goto done;
    }else if(r->special==43) {
        if(populate_gas_body(disc,&file,r,data))goto done;
    }else if(r->special==44) {
        if(populate_garage_cast(disc,&file,r,data))goto done;
    }else if(r->special==45) {
        if(populate_pending(disc,&file,r,data))goto done;
    }else if(r->special==46) {
        if(populate_garage_ui(disc,&file,r,data))goto done;
    }else if(r->special==47) {
        if(populate_pause_map(disc,&file,r,data))goto done;
    }else if(r->special==48) {
        if(populate_dialogue(disc,r,data))goto done;
    }else if(r->special==49 || r->special==50) {
        if(populate_train_data(disc,&file,r,data))goto done;
    }else if(r->special==51) {
        if(populate_record_book(disc,&file,r,data))goto done;
    }else if(r->special==52) {
        if(populate_enemy_audio(disc,&file,r,data))goto done;
    }else if(r->special==53) {
        if(populate_event_audio(disc,&file,r,data))goto done;
    }else if(r->special==54) {
        if(populate_scene_models(disc,&file,r,data))goto done;
    }else if(r->special==55) {
        if(populate_arrow_skater(disc,r,data))goto done;
    }else if(r->special==56 || r->special==57) {
        if(populate_openings(disc,&file,r,data))goto done;
    }else if(r->special==58) {
        if(populate_house(disc,&file,r,data))goto done;
    }else if(r->special==59) {
        if(populate_crane(disc,&file,r,data))goto done;
    }else if(r->special==60 || r->special==61) {
        if(populate_street_props(disc,&file,r,data))goto done;
    }else if(r->special==62) {
        if(populate_cover(disc,&file,r,data))goto done;
    }else if(r->special>=63) {
        if(r->special==63 && populate_grind(disc,&file,r,data))goto done;
        if((r->special==64 || r->special==65) && populate_shibuya_bindings(disc,&file,r,data))goto done;
        if(!fill || fill(disc,name,r->sections,r->section_count,data,size,context))goto done;
    }else if(r->special)goto done;
    put(data+16,crc32(data+prefix,header-prefix),4);put(data+20,crc32(data+header,size-header),4);
    if(fwrite(data,1,size,output)!=size)goto done;
    result=0;
done:free(data);return result;
}

int jgr_convert_table(JgrDisc *disc,const char *name,FILE *output)
{return jgr_convert_table_fill(disc,name,output,NULL,NULL);}
