/* First-launch asset preparation. All disc access is confined to this file's
   conversion phase; installed assets are never served from the original disc. */
#define JGR_PORTABLE_ASSETS_IMPLEMENTATION 1
#include "portable_assets.h"
#undef fopen
#undef stat
#undef remove
#undef rename
#undef sceIoOpen
#undef sceIoGetstat
#undef sceIoRemove
#undef sceIoRename
#include "portable_install.h"
#include "portable_disc.h"
#include "portable_copy_worker.h"
#include "portable_convert.h"
#include "portable_mission_convert.h"
#include "portable_tables.h"
#include "portable_resources.h"
#include "portable_misc.h"
#include "portable_models_jobs.h"
#include "portable_models.h"
#include "portable_crc32.h"
#ifndef JGR_PORTABLE_IDENTITY_HEADER
#define JGR_PORTABLE_IDENTITY_HEADER "portable_asset_identity.h"
#endif
#include JGR_PORTABLE_IDENTITY_HEADER
#include "portable_startup_cache.h"
#include "portable_startup_warning.h"
#include "portable_scratch.h"
#include "portable_world_convert.h"
#include "portable_stage.h"
#include "portable_stage_derived.h"
#include "portable_scene_convert.h"
#include "portable_graffiti_convert.h"
#include "portable_grind_convert.h"
#include "portable_roster_convert.h"
#include "portable_bubble.h"
#include "portable_replay_session.h"
#include "portable_people_convert.h"
#include "portable_stage_props.h"
#if JGR_SETUP_CRASH
#include "release_report.h"
#endif
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#ifdef __PSP__
#include <pspiofilemgr.h>
#include <pspiofilemgr_devctl.h>
#include <pspkernel.h>
#include <malloc.h>
#else
#include <sys/statvfs.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#define PLAN_HEADER 40u
#define PLAN_RECORD 440u
#define PLAN_MAX_FILES 20000u
#define PATH_MAXIMUM 768
#define COPY_BYTES (128u*1024u)
/* Mission records and source tables patch their headers before completion.
 * Keep those bounded outputs in RAM so seeks and checksum verification never
 * reopen the stick. The extra byte accommodates fmemopen's trailing NUL. */
#define SMALL_OUTPUT_BYTES (256u*1024u)
/* Strict C99 host builds can hide this POSIX declaration. The supported host
 * libraries and PSP newlib all provide this same interface. */
extern FILE *fmemopen(void *,size_t,const char *);
/* Three simultaneous route tracks, each bounded to30,000 raw and output
   records. Reserve their worst-case setup scratch independently of output. */
#define REPLAY_SCRATCH_BYTES (192u*1024u*1024u)
typedef struct {unsigned op,bytes,arg[4];unsigned char hash[32];char name[192],source[192];} Recipe;
typedef struct {uint32_t hash;unsigned index;} RecipeKey;
typedef struct {Recipe recipe;unsigned index;} RecipeMemo;
#define RECIPE_MEMOS 8u
typedef struct {char name[256];} DirectoryName;
/* One directory at a time, at most 1 MiB during preflight only. A larger or
 * unreadable directory falls back to the original individual stat checks. */
typedef struct {
    char path[PATH_MAXIMUM];DirectoryName *names;
    unsigned count,capacity;int fallback;
} DirectoryIndex;
typedef struct {char name[192],temporary[PATH_MAXIMUM];} AudioDestination;
typedef struct {
    FILE *plan;JgrDisc *disc;JgrInstallResult *result;JgrReplaySession *replay;
    JgrInstallProgress progress;void *user;
    unsigned char *buffer,*output_buffer,*bulk_buffer,*verified_files,*absent_files;unsigned *checksums;
    unsigned bulk_attempted;
    JgrCopyWorker *audio_worker;JgrCopyTask *audio_tasks;AudioDestination *audio_destinations;
    unsigned char *audio_pending;unsigned audio_count,audio_published;
    unsigned manifest_crc,appendix_offset,appendix_bytes,native_replay;char root[512];
    DirectoryIndex directory;
    char created_parent[PATH_MAXIMUM];
    JgrInstallWorkGroup *work;
    RecipeKey *recipe_keys;unsigned recipe_key_count;
    RecipeMemo recipe_memos[RECIPE_MEMOS];unsigned recipe_memo_count,recipe_memo_next;
} Installer;

#ifndef JGR_INSTALL_WORK_STATS
#define JGR_INSTALL_WORK_STATS JGR_DEVICE_SETUP
#endif
static JgrInstallWorkStats install_work;
void jgr_install_work_stats(JgrInstallWorkStats *out)
{if(out)*out=install_work;}
static JgrInstallWorkGroup *work_begin(unsigned op)
{
#if JGR_INSTALL_WORK_STATS
    JgrInstallWorkGroup *w=op==91?&install_work.replay:
        (op==50 || (op>=60 && op<=64) || op==71)?&install_work.city:NULL;
    if(w)w->started++;
    return w;
#else
    (void)op;return NULL;
#endif
}

static uint32_t copy_clock(void)
{
#ifdef __PSP__
    return sceKernelGetSystemTimeLow();
#else
    struct timeval now;gettimeofday(&now,NULL);
    return (uint32_t)((uint64_t)now.tv_sec*1000000u+now.tv_usec);
#endif
}
#ifdef JGR_INSTALL_TESTING
static int bulk_disabled,audio_disabled,audio_live,small_disabled;
int jgr_install_test_audio_live(void){return audio_live;}
void jgr_install_test_audio_disabled(int disabled){audio_disabled=disabled;}
void jgr_install_test_bulk_disabled(int disabled){bulk_disabled=disabled;}
void jgr_install_test_small_disabled(int disabled){small_disabled=disabled;}
#endif
static void copy_buffer_prepare(Installer *s)
{
    if(s->bulk_attempted)return;
    s->bulk_attempted=1;
#ifdef JGR_INSTALL_TESTING
    if(bulk_disabled)return;
#endif
    int saved=errno;
#ifdef __PSP__
    s->bulk_buffer=memalign(64,JGR_DISC_SEQUENTIAL_CAPACITY);
    if(s->bulk_buffer){
        /* Before the first converter, mallinfo may not know the available
         * reserved PSP heap yet. Prove headroom with an untouched temporary
         * allocation rather than mistaking that lower bound for free RAM. */
        static void *(*volatile reserve_heap)(size_t)=malloc;
        void *reserve=reserve_heap(6u*1024u*1024u);
        if(!reserve){free(s->bulk_buffer);s->bulk_buffer=NULL;}
        free(reserve);
    }
#else
    s->bulk_buffer=malloc(JGR_DISC_SEQUENTIAL_CAPACITY);
#endif
    errno=saved;
}
static void copy_buffer_release(Installer *s)
{free(s->bulk_buffer);s->bulk_buffer=NULL;s->bulk_attempted=0;}

static uint32_t word(const unsigned char *p)
{return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint32_t crc_add(uint32_t c,const void *data,size_t n)
{return jgr_crc32_add(c,data,n);}
static int fail(Installer *s,int code,const char *detail)
{
    s->result->status=code;
    if(detail)snprintf(s->result->detail,sizeof s->result->detail,"%s",detail);
    return code;
}
static int audio_poll(Installer *s);
static int audio_drain(Installer *s);
static int notify(Installer *s)
{
    if(s->result->status)return s->result->status;
    if(audio_poll(s))return s->result->status;
    return !s->progress || s->progress(s->result,s->user)?0:fail(s,JGR_INSTALL_CANCELLED,NULL);
}
static int phase(Installer *s,unsigned value,unsigned current,unsigned total)
{
    s->result->phase=value;s->result->phase_current=current;s->result->phase_total=total;
    return notify(s);
}
static int path(Installer *s,char *out,const char *prefix,const char *name)
{int n=snprintf(out,PATH_MAXIMUM,"%s/%s%s",s->root,prefix,name);return n>0 && n<PATH_MAXIMUM?0:-1;}
static int info(const char *p,struct stat *st)
{
#ifdef __PSP__
    return stat(p,st);
#else
    return lstat(p,st);
#endif
}
static int directory(const char *p)
{struct stat st;if(mkdir(p,0777)==0)return 0;return errno==EEXIST && info(p,&st)==0 && S_ISDIR(st.st_mode)?0:-1;}
static int parents(Installer *s,const char *prefix,const char *name)
{
    char p[PATH_MAXIMUM];if(path(s,p,prefix,name))return -1;
    char *last=strrchr(p,'/');if(!last)return -1;
    size_t parent_length=(size_t)(last-p);
    if(strlen(s->created_parent)==parent_length && !memcmp(s->created_parent,p,parent_length))return 0;
    for(char *at=p+strlen(s->root)+1;*at;at++)if(*at=='/') {
        *at=0;int bad=directory(p);*at='/';if(bad)return -1;
    }
    memcpy(s->created_parent,p,parent_length);s->created_parent[parent_length]=0;
    return 0;
}
static int safe(const char *p,int empty)
{
    if(!*p)return empty;
    const char *start=p;
    for(;;p++) {
        unsigned char c=*p;
        if(!c || c=='/') {
            size_t n=(size_t)(p-start);
            if(!n || (n==1 && *start=='.') || (n==2 && start[0]=='.' && start[1]=='.'))return 0;
            if(!c)return 1;
            start=p+1;
        } else if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return 0;
    }
}
static int fixed_string(char *out,const unsigned char *data)
{
    unsigned i;for(i=0;i<192 && data[i];i++);
    if(i==192)return -1;
    memcpy(out,data,192);
    for(;i<192;i++)if(data[i])return -1;
    return 0;
}
static int read_recipe(Installer *s,Recipe *r,unsigned char *raw)
{
    if(fread(raw,1,PLAN_RECORD,s->plan)!=PLAN_RECORD)return -1;
    r->op=word(raw);r->bytes=word(raw+4);
    for(unsigned i=0;i<4;i++)r->arg[i]=word(raw+8+4*i);
    memcpy(r->hash,raw+24,32);
    if(fixed_string(r->name,raw+56)||fixed_string(r->source,raw+248))return -1;
    return 0;
}
static uint32_t recipe_name_hash(const char *name)
{
    uint32_t hash=2166136261u;
    for(;*name;name++){hash^=(unsigned char)*name;hash*=16777619u;}
    return hash;
}
static int recipe_key_order(const void *a,const void *b)
{
    const RecipeKey *x=a,*y=b;
    if(x->hash!=y->hash)return x->hash<y->hash?-1:1;
    return x->index<y->index?-1:x->index>y->index;
}
/* FAT names are case insensitive. A conservative match on a case-sensitive
 * host only causes a harmless extra stat; it never accepts an unchecked file. */
static int name_compare(const char *a,const char *b)
{
    for(;;a++,b++) {
        unsigned x=(unsigned char)*a,y=(unsigned char)*b;
        if(x>='A' && x<='Z')x+='a'-'A';
        if(y>='A' && y<='Z')y+='a'-'A';
        if(x!=y || !x)return (int)x-(int)y;
    }
}
static int name_order(const void *a,const void *b)
{return name_compare(((const DirectoryName *)a)->name,((const DirectoryName *)b)->name);}
static void directory_index_clear(Installer *s)
{free(s->directory.names);memset(&s->directory,0,sizeof s->directory);}
/* Index only directories named by validated recipes, never recurse through
 * arbitrary card contents. Validate each parent before opening it so a host
 * symlink cannot redirect cleanup. Unknown directory entries are untouched. */
static int indexed_presence(Installer *s,const char *p)
{
    const char *slash=strrchr(p,'/');DirectoryIndex *index=&s->directory;
    if(!slash || (size_t)(slash-p)>=sizeof index->path)return -1;
    size_t length=(size_t)(slash-p);char directory_path[PATH_MAXIMUM];
    memcpy(directory_path,p,length);directory_path[length]=0;
    if(strcmp(index->path,directory_path)) {
        directory_index_clear(s);memcpy(index->path,directory_path,length+1);
        struct stat st;
        for(char *at=directory_path+strlen(s->root)+1;;at++)if(!*at || *at=='/') {
            char saved=*at;*at=0;int exists=info(directory_path,&st);*at=saved;
            if(exists){if(errno==ENOENT)return 0;return -1;}
            if(!S_ISDIR(st.st_mode))return -1;
            if(!saved)break;
        }
        DIR *dir=opendir(directory_path);
        if(!dir){index->fallback=1;return 1;}
        for(;;) {
            errno=0;struct dirent *entry=readdir(dir);
            if(!entry){if(errno)index->fallback=1;break;}
            if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,".."))continue;
            size_t n=strlen(entry->d_name);
            if(n>=sizeof index->names[0].name || index->count==4096){index->fallback=1;break;}
            if(index->count==index->capacity) {
                unsigned capacity=index->capacity?index->capacity*2:64;
                DirectoryName *names=realloc(index->names,capacity*sizeof *names);
                if(!names){index->fallback=1;break;}
                index->names=names;index->capacity=capacity;
            }
            memcpy(index->names[index->count++].name,entry->d_name,n+1);
            if(!(index->count&127) && notify(s)){closedir(dir);return -1;}
        }
        if(closedir(dir))index->fallback=1;
        if(index->fallback){free(index->names);index->names=NULL;index->count=index->capacity=0;}
        else if(index->count)qsort(index->names,index->count,sizeof *index->names,name_order);
    }
    if(index->fallback)return 1;
    unsigned low=0,high=index->count;
    while(low<high){unsigned mid=low+(high-low)/2;int order=name_compare(index->names[mid].name,slash+1);
        if(order<0)low=mid+1;else if(order>0)high=mid;else return 1;}
    return 0;
}
/* Only these converter-owned scratch files may be reclaimed after a forced
 * exit. Never scan by suffix: cache fixtures, saves and unknown files belong
 * to the caller. Check every parent before removing a leaf so a host symlink
 * cannot redirect cleanup outside the installation. PSP FAT has no symlinks. */
static int discard_scratch(Installer *s,const char *prefix,const char *name)
{
    char p[PATH_MAXIMUM];struct stat st;
    if(path(s,p,prefix,name))return -1;
    int present=indexed_presence(s,p);if(present<=0)return present;
    if(info(p,&st))return errno==ENOENT?0:-1;
    return S_ISREG(st.st_mode)?remove(p):-1;
}
static int reclaim_scratch(Installer *s)
{
    static const struct {const char *format;unsigned count;} series[]={
        {"replay-records-%u.tmp",3},{"replay-output-%u.tmp",5},
        {"jgc-setup-%02u.tmp",13},{"jgs-setup-%u.tmp",7},
        {"props-setup-%u.tmp",8},{"city-props-input-%u.tmp",2},
        {"roster-setup-%u.tmp",12},{"ped-table-%u.tmp",2},
        {"tunnel-setup-%u.tmp",2}
    };
    static const char *const single[]={"jgs-city-raw.tmp","jgs-city-keys.tmp",
        "tunnel-table.tmp","mission-cast-table.tmp","city-table-capture.tmp",JGR_MODEL_TEXTURE_CACHE_FILE};
    char name[224];
    if(discard_scratch(s,"data/assets/",".assets-manifest.tmp"))return -1;
    for(unsigned i=0;i<sizeof series/sizeof *series;i++)for(unsigned j=0;j<series[i].count;j++) {
        snprintf(name,sizeof name,series[i].format,j);
        if(discard_scratch(s,"data/cache/",name))return -1;
    }
    for(unsigned i=0;i<sizeof single/sizeof *single;i++)if(discard_scratch(s,"data/cache/",single[i]))return -1;
    /* Derive output staging names from the already validated, bounded plan.
     * Completed city sidecars (without .setup-tmp) remain reusable. */
    if(fseek(s->plan,PLAN_HEADER,SEEK_SET))return -1;
    for(unsigned i=0;i<s->result->total;i++) {
        Recipe r;unsigned char raw[PLAN_RECORD];
        if(read_recipe(s,&r,raw))return -1;
        snprintf(name,sizeof name,"%s.setup-tmp",r.name);
        if(discard_scratch(s,"data/assets/",name) ||
           ((r.op==63 || r.op==64) && discard_scratch(s,"data/cache/",name)))return -1;
        s->result->phase_current=i+1;
        if((!(i&31) || i+1==s->result->total) && notify(s))return -1;
    }
    return fseek(s->plan,PLAN_HEADER,SEEK_SET)?-1:0;
}
static int validate(Installer *s)
{
    unsigned char header[PLAN_HEADER],raw[PLAN_RECORD];Recipe r;char previous[192]={0};unsigned available=0;
    if(fread(header,1,sizeof header,s->plan)!=sizeof header || memcmp(header,"JGRINS1\0",8) ||
       (word(header+8)!=3 && word(header+8)!=4) || word(header+12)!=PLAN_RECORD || word(header+32)!=1)return -1;
    unsigned count=word(header+16);if(!count || count>PLAN_MAX_FILES)return -1;
    s->result->total=count;s->manifest_crc=word(header+28);
    s->native_replay=word(header+8)==4;unsigned replay_count=0;
    s->result->phase_total=count;
    s->appendix_offset=PLAN_HEADER+count*PLAN_RECORD;s->appendix_bytes=word(header+36);
    if(s->appendix_offset>4*1024*1024 || s->appendix_bytes>4*1024*1024-s->appendix_offset)return -1;
    /* Build a small optional name index during the validation read we already
       need. Dependencies otherwise binary-search the recipe on the stick,
       issuing about eleven seek/read pairs for every lookup. A hash narrows
       candidates only: the complete recipe name must still match exactly. */
    s->recipe_keys=malloc((size_t)count*sizeof *s->recipe_keys);
    uint32_t crc=0xffffffffu,appendix_cursor=s->appendix_offset;
    for(unsigned i=0;i<count;i++) {
        if(read_recipe(s,&r,raw))return -1;
        for(unsigned j=4;j<sizeof r.hash;j++)if(r.hash[j])return -1;
        crc=crc_add(crc,raw,sizeof raw);
        if(r.bytes>0x7fffffffu || !safe(r.name,0) || !safe(r.source,r.op==0) || strcmp(previous,r.name)>=0 ||
           !strcmp(r.name,"assets.manifest") || strstr(r.name,".setup-tmp") || !strcmp(r.name,".assets-manifest.tmp") || !strncmp(r.name,"data/",5) || !strncmp(r.name,"jgr_",4) ||
           !strncmp(r.name,"shot",4) || !strcmp(r.name,"level.txt") ||
           (r.op>3 && (r.op<10 || r.op>18) && r.op!=20 && r.op!=30 && r.op!=50 && r.op!=60 && r.op!=61 && r.op!=62 && r.op!=63 && r.op!=64 && r.op!=70 && r.op!=71 && (r.op<80 || r.op>82) && (r.op<84 || r.op>93) && (r.op<40 || r.op>47)))return -1;
        if((r.op<=1 && (r.arg[0]||r.arg[1]||r.arg[2]||r.arg[3])) ||
           (!r.op && *r.source) || (r.op && !*r.source))return -1;
        if(r.op==93 && (r.arg[0]>2 || r.arg[1] || r.arg[2] || r.arg[3] || !strcmp(r.name,r.source)))return -1;
        /* Version4 binds every replay recipe to the reviewed native profile.
         * Title remains interpreted inside that profile. Mixed recipe modes
         * would give order-dependent repair results, so reject them here. */
        if(r.op==91){
            if(!jgr_replay_session_known(r.name) || strcmp(r.source,"decompile/extracted/1ST_READ.BIN") ||
               r.arg[0]!=s->native_replay || r.arg[1] || r.arg[2] || r.arg[3])return -1;
            replay_count++;
        }
        if(r.op==92){char name[40],source[40];unsigned city=r.arg[0];if((city!=1&&city!=2&&city!=3&&city!=5)||r.arg[1]||r.arg[2]||r.arg[3])return -1;snprintf(name,sizeof name,"people%u_native.jpn",city);snprintf(source,sizeof source,"people%u_compact.jgc",city);if(strcmp(r.name,name)||strcmp(r.source,source))return -1;}
        if(r.op>=88 && r.op<=90 && (r.arg[0] || r.arg[1] || r.arg[2] || r.arg[3]))return -1;
        if(r.op==89 && (strcmp(r.name,"chars_roster.bin") || strcmp(r.source,"chars_compact.jgc")))return -1;
        if(r.op==88 && (strcmp(r.name,"grind_lights.jgs") || strcmp(r.source,"decompile/extracted/1ST_READ.BIN")))return -1;
        if(r.op==90 && (strcmp(r.name,"spray_bubble.jsd") || strcmp(r.source,"decompile/afs/PLAYER/016_player_effect_dat.bin")))return -1;
        if(r.op==20 && (!jgr_convert_table_known(r.name) || r.arg[0] || r.arg[1] || r.arg[2] || r.arg[3]))return -1;
        if(r.op>=80 && r.op<=82 && (r.arg[0] || r.arg[1] || r.arg[2] || r.arg[3]))return -1;
        if(r.op>=84 && r.op<=87 && (r.arg[1] || r.arg[2] || r.arg[3]))return -1;
        if(r.op==71 && (strcmp(r.name,"kogane_surface.bin") || strcmp(r.source,"decompile/extracted/1ST_READ.BIN") || r.arg[0] || r.arg[1] || r.arg[2] || r.arg[3]))return -1;
        if(r.op==70 && (!jgr_misc_known(r.name) || r.arg[0] || r.arg[1] || r.arg[2] || r.arg[3] || strcmp(r.source,"decompile/extracted/1ST_READ.BIN")))return -1;
        if(r.op==61 || r.op==62) {
            char name[32],source[32];unsigned stage=r.arg[0];
            if((stage!=1 && stage!=2 && stage!=3 && stage!=5 && stage!=6) || r.arg[1] || r.arg[2] || r.arg[3])return -1;
            snprintf(name,sizeof name,"stage%u.%s",stage,r.op==61 ? "jgm":"jvo");
            snprintf(source,sizeof source,"stage%u.jgs",stage);
            if(strcmp(r.name,name) || strcmp(r.source,source))return -1;
        }
        if(r.op==63 || r.op==64) {
            char name[32],source[80];unsigned stage=r.arg[0];
            if((stage!=1 && stage!=2 && stage!=3 && stage!=5 && stage!=6) || r.arg[1] || r.arg[2] || r.arg[3])return -1;
            snprintf(name,sizeof name,"stage%u.%s",stage,r.op==63 ? "jgs":"jrs");
            snprintf(source,sizeof source,"decompile/afs/STAGE%u/000_st%udat.bin",stage,stage);
            if(strcmp(r.name,name) || strcmp(r.source,source))return -1;
        }
        if(r.op==30) {
            if(r.arg[0]!=appendix_cursor || r.arg[1]>s->appendix_offset+s->appendix_bytes-appendix_cursor ||
               r.arg[2] || r.arg[3] || jgr_model_recipe_validate(s->plan,r.arg[0],r.arg[1]))return -1;
            appendix_cursor+=r.arg[1];
        }
        if(s->recipe_keys)s->recipe_keys[i]=(RecipeKey){recipe_name_hash(r.name),i};
        strcpy(previous,r.name);available+=r.op!=0;
        s->result->phase_current=i+1;
        if((!(i&31) || i+1==count) && notify(s))return -1;
    }
    if(appendix_cursor!=s->appendix_offset+s->appendix_bytes || (s->native_replay&&!replay_count))return -1;
    /* Extended metadata is hashed before any output directory is created. */
    for(unsigned left=s->appendix_bytes;left;) {
        unsigned n=left<sizeof raw?left:sizeof raw;
        if(fread(raw,1,n,s->plan)!=n)return -1;
        crc=crc_add(crc,raw,n);left-=n;
    }
    if(fgetc(s->plan)!=EOF || ferror(s->plan) || (crc^0xffffffffu)!=word(header+24) || available!=word(header+20))return -1;
    if(s->recipe_keys){qsort(s->recipe_keys,count,sizeof *s->recipe_keys,recipe_key_order);s->recipe_key_count=count;}
    s->result->available=available;
    return fseek(s->plan,PLAN_HEADER,SEEK_SET)?-1:0;
}
/* A matching file is reusable even after a cancelled conversion. Check actual
   contents using the recipe's CRC32. */
static int verified(Installer *s,const char *p,const Recipe *r,unsigned *crc)
{
    struct stat st;if(info(p,&st) || !S_ISREG(st.st_mode) || (uint64_t)st.st_size!=r->bytes)return 0;
    FILE *f=fopen(p,"rb");if(!f)return 0;
    if(setvbuf(f,NULL,_IONBF,0)){fclose(f);return 0;}
    uint32_t c=0xffffffffu;unsigned left=r->bytes;int valid=1;
    while(left) {
        unsigned n=left<COPY_BYTES?left:COPY_BYTES;
        if(fread(s->buffer,1,n,f)!=n){valid=0;break;}
        c=crc_add(c,s->buffer,n);left-=n;
        if(notify(s)){valid=0;break;}
    }
    if(fgetc(f)!=EOF || ferror(f))valid=0;
    if(fclose(f))valid=0;
    if((c^0xffffffffu)!=word(r->hash))valid=0;
    if(valid)*crc=c^0xffffffffu;
    return valid;
}
static FILE *small_output_open(const Recipe *r,unsigned char **buffer)
{
    *buffer=NULL;
#ifdef JGR_INSTALL_TESTING
    if(small_disabled)return NULL;
#endif
    if(!((r->op>=10 && r->op<=18) || r->op==20) || r->bytes>SMALL_OUTPUT_BYTES)return NULL;
    int saved=errno;
    unsigned char *data=calloc((size_t)r->bytes+1,1);
    FILE *file=data?fmemopen(data,(size_t)r->bytes+1,"w+b"):NULL;
    if(file)*buffer=data;else free(data);
    errno=saved;return file;
}
static int small_output_verified(const Recipe *r,
                                 const unsigned char *buffer,long size,unsigned *crc)
{
    if(size<0 || (unsigned long)size!=r->bytes)return 0;
    unsigned c=crc_add(0xffffffffu,buffer,r->bytes)^0xffffffffu;
    if(c!=word(r->hash))return 0;
    *crc=c;return 1;
}
static int capacity(Installer *s,uint64_t required)
{
#ifdef __EMSCRIPTEN__
    /* File System Access does not expose the selected volume's free space.
     * A -1 answer explicitly skips preflight; checked write/close calls must
     * still fail the transaction if that volume fills. Sandboxed backends
     * with a meaningful measured quota may supply their remaining bytes. */
    extern double jgr_browser_available_bytes(void);
    (void)s;double available=jgr_browser_available_bytes();
    if(available==-1.)return 0;
    if(!(available>=0.) || available>9007199254740991.)return -1;
    uint64_t free_bytes=(uint64_t)available;
#elif defined(__PSP__)
    char device[8];const char *colon=strchr(s->root,':');
    if(!colon || colon-s->root>6)return -1;
    size_t n=(size_t)(colon-s->root)+1;memcpy(device,s->root,n);device[n]=0;
    SceDevInf data;SceDevctlCmd command={&data};memset(&data,0,sizeof data);
    if(sceIoDevctl(device,SCE_PR_GETDEV,&command,sizeof command,NULL,0)<0 || data.sectorSize<=0 || data.sectorCount<=0)return -1;
    uint64_t free_bytes=(uint64_t)data.freeClusters*(unsigned)data.sectorSize*(unsigned)data.sectorCount;
#else
    struct statvfs data;if(statvfs(s->root,&data))return -1;
    uint64_t free_bytes=(uint64_t)data.f_bavail*data.f_frsize;
#endif
    return free_bytes>=required?0:1;
}
static int locate_disc(Installer *s,char *out)
{
    char directory_path[PATH_MAXIMUM];
    /* Keep the slash on device roots (umd0:/ in an external emulator test,
       ms0:/ on a root install); bare device names are not directory handles. */
    if(path(s,directory_path,"",""))return -1;
    DIR *d=opendir(directory_path);if(!d)return -1;struct dirent *e;unsigned count=0;
    while((e=readdir(d))!=NULL) {
        size_t n=strlen(e->d_name);
        if(n<5 || e->d_name[n-4]!='.' || (e->d_name[n-3]!='g'&&e->d_name[n-3]!='G') ||
           (e->d_name[n-2]!='d'&&e->d_name[n-2]!='D') || (e->d_name[n-1]!='i'&&e->d_name[n-1]!='I'))continue;
        if(path(s,out,"",e->d_name)){closedir(d);return -1;}count++;
    }
    closedir(d);return count==1?0:count?2:1;
}
/* Preflight marks only names proven absent by a complete directory index.
 * The bit is cleared on publication; unknown/index-fallback and repair entries
 * keep their ordinary verification and replacement checks. */
static int known_absent(const Installer *s,unsigned index)
{return !!(s->absent_files[index/8]&(1u<<(index%8)));}
static int publish_asset(Installer *s,unsigned index,const char *temporary,const char *target)
{
    if(!known_absent(s,index) && remove(target) && errno!=ENOENT)return -1;
    if(rename(temporary,target))return -1;
    s->absent_files[index/8]&=(unsigned char)~(1u<<(index%8));
    return 0;
}
/* The worker exclusively owns its cloned disc and temporary streams. Only
 * the main thread publishes completed files or updates installer/UI state. */
static void audio_release(Installer *s)
{
    if(s->audio_worker){
        jgr_copy_worker_destroy(s->audio_worker);s->audio_worker=NULL;
#ifdef JGR_INSTALL_TESTING
        audio_live=0;
#endif
        for(unsigned n=s->audio_published;n<s->audio_count;n++)
            remove(s->audio_destinations[n].temporary);
    }
    free(s->audio_tasks);free(s->audio_destinations);free(s->audio_pending);
    s->audio_tasks=NULL;s->audio_destinations=NULL;s->audio_pending=NULL;
    s->audio_count=s->audio_published=0;
}
static int audio_poll(Installer *s)
{
    if(!s->audio_worker)return 0;
    while(s->audio_published<s->audio_count){
        unsigned n=s->audio_published;JgrCopyCompletion done;
        if(jgr_copy_worker_result(s->audio_worker,n,&done)!=1)break;
        JgrInstallCopyStats *stats=&s->result->copy;
        if(done.status){
            strcpy(s->result->file,s->audio_destinations[n].name);
            return fail(s,done.status==JGR_COPY_CHECKSUM?JGR_INSTALL_CHECKSUM:
                done.status==JGR_COPY_CANCELLED?JGR_INSTALL_CANCELLED:JGR_INSTALL_IO,done.detail);
        }
        char target[PATH_MAXIMUM];uint32_t started=copy_clock();
        JgrCopyTask *task=&s->audio_tasks[n];
        if(path(s,target,"data/assets/",s->audio_destinations[n].name) ||
           done.bytes!=task->bytes ||
           publish_asset(s,task->tag,task->temporary,target)){
            strcpy(s->result->file,s->audio_destinations[n].name);
            return fail(s,JGR_INSTALL_IO,"Cannot publish prepared audio");
        }
        stats->bytes+=done.bytes;stats->read_us+=done.read_us;stats->write_us+=done.write_us;
        stats->hash_us+=done.hash_us;stats->chunks+=done.chunks;stats->bulk_chunks+=done.chunks;
        unsigned chunk=task->bytes<JGR_COPY_WORKER_PAYLOAD_BYTES?task->bytes:JGR_COPY_WORKER_PAYLOAD_BYTES;
        if(chunk>stats->max_chunk)stats->max_chunk=chunk;
        stats->publish_us+=(uint32_t)(copy_clock()-started);stats->files++;
        s->checksums[task->tag]=done.crc32;
        s->verified_files[task->tag/8]|=(unsigned char)(1u<<(task->tag%8));
        s->audio_pending[task->tag/8]&=(unsigned char)~(1u<<(task->tag%8));
        s->result->written++;s->result->completed_bytes+=task->bytes;
        s->result->phase_current=s->result->written+s->result->reused;
        s->result->pipeline.published++;s->audio_published++;
    }
    /* Once the batch is published there is no reason to pin the clone,
     * buffers, descriptors and stack through the remaining model jobs. The
     * destroy joins before releasing them, including the final-thread-exit
     * race immediately after it published its last completion. */
    if(s->audio_published==s->audio_count)audio_release(s);
    return 0;
}
static int audio_drain(Installer *s)
{
    if(!s->audio_worker)return 0;
    uint32_t started=copy_clock();jgr_copy_worker_pause(s->audio_worker,0);
    for(;;){
        if(notify(s) || !s->audio_worker)break;
        JgrCopyProgress progress;
        if(jgr_copy_worker_progress(s->audio_worker,&progress)){
            fail(s,JGR_INSTALL_IO,"Cannot read audio worker state");break;
        }
        if(progress.finished){(void)notify(s);break;}
#ifdef __PSP__
        sceKernelDelayThread(10000);
#else
        usleep(10000);
#endif
    }
    s->result->pipeline.drain_us+=(uint32_t)(copy_clock()-started);
    if(!s->result->status && s->audio_published!=s->audio_count)
        fail(s,JGR_INSTALL_IO,"Audio copy batch ended before completion");
    audio_release(s);return s->result->status;
}
static int audio_prepare(Installer *s)
{
#ifdef JGR_INSTALL_TESTING
    if(audio_disabled)return 0;
#endif
    long saved=ftell(s->plan);if(saved<0)return fail(s,JGR_INSTALL_IO,NULL);
    unsigned count=0;int status=0;
    if(fseek(s->plan,PLAN_HEADER,SEEK_SET))return fail(s,JGR_INSTALL_IO,NULL);
    for(unsigned i=0;i<s->result->total;i++){
        Recipe r;unsigned char raw[PLAN_RECORD];
        if(read_recipe(s,&r,raw)){status=fail(s,JGR_INSTALL_PLAN,NULL);goto done;}
        if(r.op==JGR_INSTALL_COPY && !strncmp(r.name,"adx/",4) &&
           !(s->verified_files[i/8]&(1u<<(i%8))))count++;
    }
    if(!count || count>JGR_COPY_WORKER_MAX_TASKS)goto done;
    s->audio_tasks=calloc(count,sizeof *s->audio_tasks);
    s->audio_destinations=calloc(count,sizeof *s->audio_destinations);
    s->audio_pending=calloc((s->result->total+7)/8,1);
    if(!s->audio_tasks || !s->audio_destinations || !s->audio_pending)goto fallback;
    if(fseek(s->plan,PLAN_HEADER,SEEK_SET)){status=fail(s,JGR_INSTALL_IO,NULL);goto done;}
    for(unsigned i=0;i<s->result->total;i++){
        Recipe r;unsigned char raw[PLAN_RECORD];
        if(read_recipe(s,&r,raw)){status=fail(s,JGR_INSTALL_PLAN,NULL);goto done;}
        if(r.op!=JGR_INSTALL_COPY || strncmp(r.name,"adx/",4) ||
           (s->verified_files[i/8]&(1u<<(i%8))))continue;
        unsigned n=s->audio_count;JgrCopyTask *task=&s->audio_tasks[n];
        AudioDestination *destination=&s->audio_destinations[n];char target[PATH_MAXIMUM];struct stat st;
        if(jgr_disc_source(s->disc,r.source,&task->source) || task->source.size!=r.bytes){
            strcpy(s->result->file,r.name);status=fail(s,JGR_INSTALL_DISC,"Invalid audio source extent");goto done;
        }
        if(parents(s,"data/assets/",r.name) || path(s,target,"data/assets/",r.name)){
            status=fail(s,JGR_INSTALL_IO,NULL);goto done;
        }
        int length=snprintf(destination->temporary,sizeof destination->temporary,"%s.setup-tmp",target);
        if(length<0 || (size_t)length>=sizeof destination->temporary){status=fail(s,JGR_INSTALL_IO,NULL);goto done;}
        int exists=info(destination->temporary,&st);
        if((!exists && !S_ISREG(st.st_mode)) || (exists && errno!=ENOENT)){
            status=fail(s,JGR_INSTALL_IO,"Invalid audio temporary file");goto done;
        }
        strcpy(destination->name,r.name);task->temporary=destination->temporary;
        task->bytes=r.bytes;task->tag=i;task->crc32=word(r.hash);
        s->audio_pending[i/8]|=(unsigned char)(1u<<(i%8));s->audio_count++;
    }
    char error[160];
    s->audio_worker=jgr_copy_worker_create(s->disc,s->audio_tasks,count,1,error,sizeof error);
    if(!s->audio_worker)goto fallback;
#ifdef JGR_INSTALL_TESTING
    audio_live=1;
#endif
#ifdef __PSP__
    /* Optional overlap must leave enough space for the model working set. */
    static void *(*volatile reserve_heap)(size_t)=malloc;
    void *reserve=reserve_heap(6u*1024u*1024u);
    if(!reserve)goto fallback;
    free(reserve);
#endif
    s->result->pipeline.queued=count;goto done;
fallback:
    audio_release(s);
done:
    if(fseek(s->plan,saved,SEEK_SET))status=fail(s,JGR_INSTALL_IO,NULL);
    return status;
}

/* Dependency files must be present in this plan and hash-verified before a
 * sidecar consumes them. Preserve the canonical plan cursor/checksum slots. */
static int find_recipe(Installer *s,const char *name,Recipe *found,unsigned *index)
{
    for(unsigned i=0;i<s->recipe_memo_count;i++)if(!strcmp(s->recipe_memos[i].recipe.name,name)){
        *found=s->recipe_memos[i].recipe;if(index)*index=s->recipe_memos[i].index;return 0;
    }
    long saved=ftell(s->plan);if(saved<0)return -1;
    unsigned low=0,high=s->recipe_key_count,matched=0;int status=-1,moved=0;
    if(high){
        uint32_t hash=recipe_name_hash(name);
        while(low<high){unsigned mid=low+(high-low)/2;
            if(s->recipe_keys[mid].hash<hash)low=mid+1;else high=mid;}
        for(;low<s->recipe_key_count && s->recipe_keys[low].hash==hash;low++){
            unsigned at=s->recipe_keys[low].index;Recipe r;unsigned char raw[PLAN_RECORD];moved=1;
            if(fseek(s->plan,PLAN_HEADER+at*PLAN_RECORD,SEEK_SET) || read_recipe(s,&r,raw))break;
            if(!strcmp(r.name,name)){*found=r;matched=at;status=0;break;}
        }
    }else{
        /* Allocation failure preserves the original bounded disk search. */
        high=s->result->total;
        while(low<high) {
            unsigned mid=low+(high-low)/2;Recipe r;unsigned char raw[PLAN_RECORD];moved=1;
            if(fseek(s->plan,PLAN_HEADER+mid*PLAN_RECORD,SEEK_SET) || read_recipe(s,&r,raw))break;
            int order=strcmp(r.name,name);
            if(order<0)low=mid+1;
            else if(order>0)high=mid;
            else {*found=r;matched=mid;status=0;break;}
        }
    }
    if(moved && fseek(s->plan,saved,SEEK_SET))status=-1;
    if(!status){
        if(index)*index=matched;
        RecipeMemo *memo=s->recipe_memos+s->recipe_memo_next;
        memo->recipe=*found;memo->index=matched;
        s->recipe_memo_next=(s->recipe_memo_next+1)%RECIPE_MEMOS;
        if(s->recipe_memo_count<RECIPE_MEMOS)s->recipe_memo_count++;
    }
    return status;
}
static FILE *dependency(Installer *s,const char *name)
{
    Recipe r;char target[PATH_MAXIMUM];unsigned index;struct stat st;
    if(find_recipe(s,name,&r,&index) || path(s,target,"data/assets/",name))return NULL;
    unsigned char mask=(unsigned char)(1u<<(index%8));
    if(s->audio_pending && (s->audio_pending[index/8]&mask) && audio_drain(s))return NULL;
    /* Reuse only this invocation's successful hash. Every dependency open
     * still checks the file's type and length. Repair starts with an empty
     * bitmap and hashes existing files again; same-size changes after that
     * hash have the same accepted trade-off as final verification below. */
    if(info(target,&st) || !S_ISREG(st.st_mode) || (uint64_t)st.st_size!=r.bytes) {
        s->verified_files[index/8]&=(unsigned char)~mask;return NULL;
    }
    if(!(s->verified_files[index/8]&mask)) {
        if(!verified(s,target,&r,&s->checksums[index]))return NULL;
        s->verified_files[index/8]|=mask;
    }
    FILE *file=fopen(target,"rb");
    if(!file)s->verified_files[index/8]&=(unsigned char)~mask;
    return file;
}
static int replay_progress(unsigned long long instructions,void *user){(void)instructions;return notify(user)==0;}
static FILE *table_dependency(void *user,const char *name){return dependency(user,name);}
static FILE *setup_output(const char *name)
{
    struct stat st;int exists=info(name,&st);
    if((!exists && !S_ISREG(st.st_mode)) || (exists && errno!=ENOENT))return NULL;
    return fopen(name,"w+b");
}
static int stream_output(Installer *s,FILE *input,unsigned bytes,FILE *output)
{
    if(fseek(input,0,SEEK_SET))return -1;
    while(bytes){unsigned n=bytes<COPY_BYTES?bytes:COPY_BYTES;if(fread(s->buffer,1,n,input)!=n || fwrite(s->buffer,1,n,output)!=n || notify(s))return -1;bytes-=n;}
    return 0;
}
/* JGS/JRS are one source conversion, two independently verified outputs.
 * A hash-bound sidecar avoids regenerating the pair in the current run. If
 * only JGS survived an interruption, the JRS row regenerates the pair safely. */
static int city_pair(Installer *s,const Recipe *r,FILE *out)
{
    char geometry_name[32],owner_name[32],directory[PATH_MAXIMUM],sidecar[PATH_MAXIMUM],temporary[PATH_MAXIMUM];
    Recipe geometry,owners;unsigned crc,stage=r->arg[0];
    snprintf(geometry_name,sizeof geometry_name,"stage%u.jgs",stage);
    snprintf(owner_name,sizeof owner_name,"stage%u.jrs",stage);
    if(find_recipe(s,geometry_name,&geometry,NULL) || find_recipe(s,owner_name,&owners,NULL) || path(s,directory,"data/cache","") || path(s,sidecar,"data/cache/",owner_name))return -1;
    if(r->op==64 && verified(s,sidecar,&owners,&crc)) {
        FILE *input=fopen(sidecar,"rb");if(!input)return -1;
        int bad=stream_output(s,input,owners.bytes,out),failure_errno=errno;
        if(fclose(input) && !bad){bad=-1;failure_errno=errno;}
        if(bad)errno=failure_errno;
        return bad;
    }
    int n=snprintf(temporary,sizeof temporary,"%s/%s.setup-tmp",directory,r->op==63?owner_name:geometry_name);
    if(n<0 || (size_t)n>=sizeof temporary)return -1;
    FILE *other=setup_output(temporary);if(!other)return -1;
    int bad=jgr_stage_city(s->disc,stage,directory,r->op==63?out:other,r->op==63?other:out);
    /* Cleanup can replace errno even when it succeeds (including scratch
     * cookie callbacks). Report the first failure, not the final close or
     * temporary-file removal that follows it. */
    int failure_errno=errno;
    if(bad && !s->result->detail[0]){
        JgrStageFailure failure;jgr_stage_failure(&failure);
        if(failure.step && failure.stage==stage)
            snprintf(s->result->detail,sizeof s->result->detail,
                "City stage=%u step=%u sec=%u errno=%d src=%lld dst=%lld io=%d/%d hf=%u kf=%u km=%u",
                failure.stage,failure.step,failure.section,failure.error,
                (long long)failure.source_offset,(long long)failure.output_offset,
                failure.source_error,failure.output_error,
                failure.heap_free,failure.kernel_free,failure.kernel_max_free);
    }
    if(fflush(other)){if(!bad)failure_errno=errno;bad=-1;}
    if(fclose(other)){if(!bad)failure_errno=errno;bad=-1;}
    if(!bad && !verified(s,temporary,r->op==63?&owners:&geometry,&crc)){
        bad=-1;failure_errno=errno;
        if(s->result->status!=JGR_INSTALL_CANCELLED){
            snprintf(s->result->file,sizeof s->result->file,"%s",r->op==63?owner_name:geometry_name);
            fail(s,JGR_INSTALL_CHECKSUM,"Generated city sidecar differs from this build's expected data");
        }
    }
    if(!bad && r->op==63) {
        struct stat st;int exists=info(sidecar,&st);
        if((!exists && !S_ISREG(st.st_mode)) || (exists && errno!=ENOENT) || (remove(sidecar) && errno!=ENOENT) || rename(temporary,sidecar)){bad=-1;failure_errno=errno;}
    }
    if(bad || r->op==64)remove(temporary);
    if(bad)errno=failure_errno;
    return bad;
}
static int model_convert(Installer *s,const Recipe *r,const char *directory,FILE *out)
{
    Recipe light;unsigned index=0;FILE *lighting=NULL;char temporary[PATH_MAXIMUM]={0},target[PATH_MAXIMUM];
    /* The shared cast already visits the source normals and materials. Save
     * its lighting sidecar in that pass instead of rebuilding the same cast. */
    int companion=!strcmp(r->name,"chars.jgc") && !find_recipe(s,"playerlight.bin",&light,&index) &&
        light.op==30 && !(s->verified_files[index/8]&(1u<<(index%8)));
    if(companion){
        s->verified_files[index/8]&=(unsigned char)~(1u<<(index%8));
        if(parents(s,"data/assets/",light.name) || path(s,target,"data/assets/",light.name))return -1;
        int n=snprintf(temporary,sizeof temporary,"%s.setup-tmp",target);
        if(n<0 || (size_t)n>=sizeof temporary)return -1;
        lighting=setup_output(temporary);if(!lighting)return -1;
    }
    int bad=jgr_models_convert_with_lighting(s->disc,s->plan,r->arg[0],r->arg[1],directory,out,lighting);
    if(bad)snprintf(s->result->detail,sizeof s->result->detail,"%s",jgr_models_error());
    if(lighting){
        int saved_errno=errno;
        if(fflush(lighting)){if(!bad)saved_errno=errno;bad=-1;}
        if(fclose(lighting)){if(!bad)saved_errno=errno;bad=-1;}
        if(!bad && !verified(s,temporary,&light,&s->checksums[index])){
            if(s->result->status!=JGR_INSTALL_CANCELLED)
                fail(s,JGR_INSTALL_CHECKSUM,"Generated cast lighting differs from expected data");
            bad=-1;
        }
        if(!bad && publish_asset(s,index,temporary,target)){saved_errno=errno;bad=-1;}
        if(!bad){s->verified_files[index/8]|=(unsigned char)(1u<<(index%8));
            s->result->written++;s->result->completed_bytes+=light.bytes;}
        else remove(temporary);
        errno=saved_errno;
    }
    return bad;
}
static int generate(Installer *s,const Recipe *r,FILE *out,unsigned *copied_crc)
{
    if(r->op==93){
        FILE *source=dependency(s,r->source);if(!source)return -1;
        int bad=0;
        if(r->arg[0])bad=jgr_model_compact(source,out,r->arg[0]==2);
        else {size_t n;while((n=fread(s->buffer,1,COPY_BYTES,source)))if(fwrite(s->buffer,1,n,out)!=n){bad=-1;break;}
            if(ferror(source))bad=-1;}
        if(fclose(source))bad=-1;
        return bad;
    }
    if(r->op==91){
        if(!s->replay){char directory[PATH_MAXIMUM];if(path(s,directory,"data/cache",""))return -1;
            s->replay=jgr_replay_session_create(s->disc,directory,table_dependency,s,replay_progress,s,s->result->detail,sizeof s->result->detail);
            if(!s->replay)return -1;
            if(s->native_replay){
                if(path(s,directory,"data/assets","") || jgr_replay_session_native_assets(s->replay,directory)) {
                    snprintf(s->result->detail,sizeof s->result->detail,"Native replay profile is unavailable in this build");return -1;
                }
            }
        }
        int bad=jgr_replay_session_generate(s->replay,r->name,out);
        if(bad)snprintf(s->result->detail,sizeof s->result->detail,"%s",jgr_replay_session_error(s->replay));
        return bad;
    }
    if(r->op==92){FILE *source=dependency(s,r->source);if(!source)return -1;int bad=jgr_people_native(s->disc,r->arg[0],source,out);if(fclose(source))bad=-1;return bad;}
    if(r->op==88){char directory[PATH_MAXIMUM];if(path(s,directory,"data/cache",""))return -1;return jgr_grind_lights(s->disc,directory,out);}
    if(r->op==89){
        char directory[PATH_MAXIMUM];if(path(s,directory,"data/cache",""))return -1;
        /* Keep ordinary input buffering for the roster's random-read fallback.
         * A large stdio buffer amplifies seek-driven rereads on PSP newlib. */
        FILE *chars=dependency(s,r->source),*lighting=dependency(s,"playerlight.bin");int bad=-1;
        if(chars&&lighting)bad=jgr_roster_convert(chars,lighting,directory,out);
        if(chars&&fclose(chars))bad=-1;
        if(lighting&&fclose(lighting))bad=-1;
        return bad;
    }
    if(r->op==63 || r->op==64)return city_pair(s,r,out);
    if(r->op==71)return jgr_stage_surface(s->disc,out);
    if(r->op==70)return jgr_misc_convert(s->disc,r->name,out);
    if(r->op==20){
        if(!jgr_convert_table_deferred(r->name))return jgr_convert_table(s->disc,r->name,out);
        char directory[PATH_MAXIMUM];if(path(s,directory,"data/cache",""))return -1;
        return jgr_convert_table_cached(s->disc,r->name,directory,table_dependency,s,out);
    }
    if(r->op==50)return jgr_world_convert(s->disc,r->name,out);
    if(r->op==61 || r->op==62) {
        FILE *jgs=dependency(s,r->source),*jrs=NULL;int status=-1;
        if(!jgs)return -1;
        if(r->op==61)status=jgr_stage_mips(s->disc,jgs,r->arg[0],out);
        else {
            char name[32];snprintf(name,sizeof name,"stage%u.jrs",r->arg[0]);jrs=dependency(s,name);
            if(jrs)status=jgr_stage_visibility(jgs,jrs,r->arg[0],out);
        }
        if(jrs && fclose(jrs))status=-1;
        if(fclose(jgs))status=-1;
        return status;
    }
    if(r->op==60) {
        char directory[PATH_MAXIMUM];if(path(s,directory,"data/cache",""))return -1;
        return jgr_stage_objects(s->disc,r->arg[0],directory,out);
    }
    if(r->op==30) {
        char directory[PATH_MAXIMUM];long saved=ftell(s->plan);
        if(saved<0 || path(s,directory,"data/cache",""))return -1;
        int bad=model_convert(s,r,directory,out);
        if(fseek(s->plan,saved,SEEK_SET))bad=-1;
        return bad;
    }
    if(r->op==81) {
        uint32_t crc;
        int bad=jgr_scene_dependencies_crc(s->disc,out,&crc);
        if(!bad)*copied_crc=crc;
        if(!bad && *copied_crc!=word(r->hash))
            return fail(s,JGR_INSTALL_CHECKSUM,"Generated file differs from this build's expected data");
        return bad;
    }
    JgrDiscFile input;
    if(jgr_disc_source(s->disc,r->source,&input)<0)return -1;
    if(r->op==90)return jgr_convert_bubble(s->disc,&input,out);
    if(r->op>=80 && r->op<=82)return jgr_scene_convert(s->disc,&input,r->op,out);
    if(r->op>=84 && r->op<=87)return jgr_graffiti_convert(s->disc,&input,r->op,r->arg[0],out);
    if(r->op==JGR_INSTALL_COPY) {
        if(input.size!=r->bytes)return -1;
        uint32_t crc=0xffffffffu;
        JgrInstallCopyStats *stats=&s->result->copy;
        unsigned char *buffer=s->bulk_buffer?s->bulk_buffer:s->buffer;
        unsigned capacity=s->bulk_buffer?JGR_DISC_SEQUENTIAL_BYTES:COPY_BYTES;
        for(unsigned at=0;at<input.size;) {
            unsigned n=input.size-at<capacity?input.size-at:capacity;
            uint32_t started=copy_clock();
            int bad=s->bulk_buffer?
                jgr_disc_read_sequential(s->disc,&input,at,buffer,n,JGR_DISC_SEQUENTIAL_CAPACITY):
                jgr_disc_read(s->disc,&input,at,buffer,n);
            stats->read_us+=(uint32_t)(copy_clock()-started);
            if(bad)return -1;
            started=copy_clock();size_t written=fwrite(buffer,1,n,out);
            stats->write_us+=(uint32_t)(copy_clock()-started);
            if(written!=n)return -1;
            started=copy_clock();
            crc=crc_add(crc,buffer,n);
            stats->hash_us+=(uint32_t)(copy_clock()-started);
            stats->bytes+=n;stats->chunks++;
            if(s->bulk_buffer)stats->bulk_chunks++;
            if(n>stats->max_chunk)stats->max_chunk=n;
            started=copy_clock();bad=notify(s);
            stats->callback_us+=(uint32_t)(copy_clock()-started);
            if(bad)return -1;
            at+=n;
        }
        if((crc^0xffffffffu)!=word(r->hash))
            return fail(s,JGR_INSTALL_CHECKSUM,"Source file differs from this build's expected data");
        *copied_crc=crc^0xffffffffu;
        return 0;
    }
    if(r->op==JGR_INSTALL_TEX2D)return jgr_convert_tex2d(s->disc,&input,r->arg[0],out);
    if(r->op==JGR_INSTALL_LIST2D)return jgr_convert_list2d(s->disc,&input,r->arg[0],r->arg[1],r->arg[2],out);
    if(r->op>=10 && r->op<=18)return jgr_mission_convert(s->disc,&input,r->op,r->arg[0],out);
    if(r->op>=40 && r->op<=47)return jgr_resource_convert(s->disc,&input,r->op,r->arg,out);
    return -1;
}
static int publish(Installer *s,const char *temporary)
{
    struct stat st;
    if(info(temporary,&st)==0 && !S_ISREG(st.st_mode))return -1;
    FILE *f=fopen(temporary,"wb");if(!f)return -1;
    int bad=fprintf(f,"JGR_ASSETS 1\nlayout 1\ncount %u\n",s->result->total)<0;
    if(fseek(s->plan,PLAN_HEADER,SEEK_SET))bad=1;
    for(unsigned i=0;!bad && i<s->result->total;i++) {
        Recipe r;unsigned char raw[PLAN_RECORD];
        if(read_recipe(s,&r,raw) || fprintf(f,"%u %08x %s\n",r.bytes,s->checksums[i],r.name)<0)bad=1;
    }
    if(fflush(f))bad=1;
    if(fclose(f))bad=1;
    if(bad)return -1;
    f=fopen(temporary,"rb");if(!f)return -1;uint32_t crc=0xffffffffu;size_t n;
    while((n=fread(s->buffer,1,COPY_BYTES,f)))crc=crc_add(crc,s->buffer,n);
    if(ferror(f))bad=1;
    if(fclose(f))bad=1;
    if(s->manifest_crc && (crc^0xffffffffu)!=s->manifest_crc)return -1;
    char target[PATH_MAXIMUM];if(path(s,target,"data/assets/","assets.manifest"))return -1;
    if(!bad && remove(target) && errno!=ENOENT)bad=1;
    if(!bad && rename(temporary,target))bad=1;
    return bad?-1:0;
}
int jgr_install_run(const char *root,const char *plan_path,const char *gdi,
                    int allow_incomplete,JgrInstallProgress progress,void *user,JgrInstallResult *result)
{
    if(!result)return JGR_INSTALL_IO;
    memset(&install_work,0,sizeof install_work);
    jgr_stage_stats_reset();jgr_replay_stats_reset();
    memset(result,0,sizeof *result);Installer s;memset(&s,0,sizeof s);s.result=result;s.progress=progress;s.user=user;
    if(!root || !*root || strlen(root)>=sizeof s.root || !plan_path)return fail(&s,JGR_INSTALL_IO,"Invalid application path");
    strcpy(s.root,root);while(strlen(s.root)>1 && s.root[strlen(s.root)-1]=='/')s.root[strlen(s.root)-1]=0;
    s.plan=fopen(plan_path,"rb");if(!s.plan)return fail(&s,JGR_INSTALL_PLAN,"Missing conversion instructions");
    char temporary[PATH_MAXIMUM]={0};int status=0;
    if(phase(&s,JGR_INSTALL_PHASE_PLAN,0,0)){status=result->status;goto done;}
    if(validate(&s)){status=result->status==JGR_INSTALL_CANCELLED?result->status:fail(&s,JGR_INSTALL_PLAN,"Invalid conversion instructions");goto done;}
    s.buffer=malloc(COPY_BYTES);s.checksums=calloc(result->total,sizeof *s.checksums);
    s.verified_files=calloc((result->total+7)/8,1);
    s.absent_files=calloc((result->total+7)/8,1);
    if(!s.buffer || !s.checksums || !s.verified_files || !s.absent_files){status=fail(&s,JGR_INSTALL_IO,"Not enough setup memory");goto done;}
    if(parents(&s,"data/assets/","placeholder") || parents(&s,"data/cache/","placeholder") ||
       path(&s,temporary,"data/assets/",".assets-manifest.tmp")){status=fail(&s,JGR_INSTALL_IO,"Cannot create setup directories");goto done;}
    {
        struct stat st;
        if(info(temporary,&st)==0 && !S_ISREG(st.st_mode)){status=fail(&s,JGR_INSTALL_IO,"Invalid setup temporary file");temporary[0]=0;goto done;}
    }
    /* Reclaim interrupted work before calculating available space. Otherwise
     * the old scratch occupies the same reserve needed to restart its job. */
    if(phase(&s,JGR_INSTALL_PHASE_CLEANUP,0,result->total)){status=result->status;goto done;}
    if(reclaim_scratch(&s)){status=result->status==JGR_INSTALL_CANCELLED?result->status:fail(&s,JGR_INSTALL_IO,"Unsafe or unreadable setup scratch file");goto done;}
    directory_index_clear(&s);
    if(phase(&s,JGR_INSTALL_PHASE_SCAN,0,result->total)){status=result->status;goto done;}
    unsigned needed=0;uint64_t required=4*1024*1024u,work_bytes=0,texture_cache_bytes=0;
    for(unsigned i=0;i<result->total;i++) {
        Recipe r;unsigned char raw[PLAN_RECORD];char target[PATH_MAXIMUM];
        if(read_recipe(&s,&r,raw) || path(&s,target,"data/assets/",r.name)){status=fail(&s,JGR_INSTALL_PLAN,NULL);goto done;}
        strcpy(result->file,r.name);
        int present=indexed_presence(&s,target);
        if(present<0){status=result->status==JGR_INSTALL_CANCELLED?result->status:fail(&s,JGR_INSTALL_IO,"Unsafe or unreadable asset directory");goto done;}
        if(!present)s.absent_files[i/8]|=(unsigned char)(1u<<(i%8));
        if(present && verified(&s,target,&r,&s.checksums[i])) {
            s.verified_files[i/8]|=(unsigned char)(1u<<(i%8));
            result->reused++;result->completed_bytes+=r.bytes;
        }
        else if(result->status){status=result->status;goto done;}
        else if(!r.op)result->missing++;
        else {needed++;required+=(uint64_t)r.bytes+32768u;
            if(r.op==30)texture_cache_bytes=JGR_MODEL_TEXTURE_CACHE_DISK_BYTES;
            if(r.op==91 && work_bytes<REPLAY_SCRATCH_BYTES)work_bytes=REPLAY_SCRATCH_BYTES;
            if((r.op==30 || r.op==60 || r.op==89) && (uint64_t)r.bytes*3>work_bytes)work_bytes=(uint64_t)r.bytes*3;
            if(r.op==63 || r.op==64 || (r.op==20 && (!strcmp(r.name,"source_data_stage_ped_proxy_data.jsd") || !strcmp(r.name,"source_data_ptr_kogane_tunnel_materials.jsd")))) {
                char name[32];Recipe geometry;snprintf(name,sizeof name,"stage%u.jgs",r.op==20?(!strcmp(r.name,"source_data_stage_ped_proxy_data.jsd")?1:2):r.arg[0]);
                if(find_recipe(&s,name,&geometry,NULL)){status=fail(&s,JGR_INSTALL_PLAN,"City pair is missing from conversion instructions");goto done;}
                if((uint64_t)geometry.bytes*6>work_bytes)work_bytes=(uint64_t)geometry.bytes*6;
            }}
        result->phase_current=i+1;
        if((!(i&31) || i+1==result->total) && notify(&s)){status=result->status;goto done;}
    }
    directory_index_clear(&s);
    /* The session texture cache can coexist with converter scratch. Reserve
     * its bounded maximum once, independently of the number of model jobs. */
    required+=work_bytes+texture_cache_bytes;
    result->required_bytes=required;
    if(result->missing && !allow_incomplete){status=fail(&s,JGR_INSTALL_INCOMPLETE,"This build does not yet convert every required asset");goto done;}
    if(needed) {
        if(phase(&s,JGR_INSTALL_PHASE_SPACE,0,1)){status=result->status;goto done;}
        int space=capacity(&s,required);
        if(space){status=fail(&s,space>0?JGR_INSTALL_SPACE:JGR_INSTALL_IO,"Cannot reserve space for generated assets");goto done;}
        if(phase(&s,JGR_INSTALL_PHASE_DISC,0,1)){status=result->status;goto done;}
        char selected[PATH_MAXIMUM];
        if(!gdi) {
            int found=locate_disc(&s,selected);
            if(found){status=fail(&s,JGR_INSTALL_DISC,found==2?"Keep only one supported GDI beside EBOOT.PBP":"Place the supported GDI and its tracks beside EBOOT.PBP");goto done;}
            gdi=selected;
        }
        if(jgr_disc_open(&s.disc,gdi,result->detail,sizeof result->detail)<0){status=fail(&s,JGR_INSTALL_DISC,NULL);goto done;}
        jgr_stage_mips_cache_begin(s.disc);
        jgr_stage_texture_capture(s.disc,jgr_stage_mips_capture,NULL);
        char table_cache[PATH_MAXIMUM];
        if(path(&s,table_cache,"data/cache","")){status=fail(&s,JGR_INSTALL_IO,NULL);goto done;}
        jgr_table_city_cache_begin(s.disc,table_cache,table_dependency,&s);
        if(audio_prepare(&s)){status=result->status;goto done;}
        /* Catalog/manifest order stays canonical. Generated sidecars run in
           a second pass so their JGS/JRS dependencies are published first.
           Geometry-bound tables follow, then the isolated replay phase, then
           mission casts that depend on the completed title replay. */
        for(unsigned phase=0;phase<5;phase++) {
        if(jgr_scratch_setup_phase(phase)){status=fail(&s,JGR_INSTALL_IO,"Cannot switch setup scratch budget");goto done;}
        if(phase==0){
            jgr_models_cache_begin(s.disc);
            if(s.audio_worker){
                result->phase=JGR_INSTALL_PHASE_CONVERT;
                result->phase_current=result->written+result->reused;result->phase_total=result->total;
                jgr_copy_worker_pause(s.audio_worker,0);
            }
        }
        if(fseek(s.plan,PLAN_HEADER,SEEK_SET)){status=fail(&s,JGR_INSTALL_IO,NULL);goto done;}
        for(unsigned i=0;i<result->total;i++) {
            Recipe r;unsigned char raw[PLAN_RECORD];char target[PATH_MAXIMUM];
            if(read_recipe(&s,&r,raw) || path(&s,target,"data/assets/",r.name)){status=fail(&s,JGR_INSTALL_PLAN,NULL);goto done;}
            unsigned recipe_phase=r.op==91?3:r.op==20 && !strcmp(r.name,"source_data_ptr_mission_cast.jsd")?4:r.op==20 && jgr_convert_table_deferred(r.name)?2:(r.op==61 || r.op==62 || r.op==89 || r.op==92 || r.op==93)?1:0;
            if(!r.op || recipe_phase!=phase)continue;
            if(s.audio_pending && (s.audio_pending[i/8]&(1u<<(i%8))))continue;
            strcpy(result->file,r.name);
            result->phase=JGR_INSTALL_PHASE_CONVERT;
            result->phase_current=result->written+result->reused;result->phase_total=result->total;
            if(notify(&s)){status=result->status;goto done;}
            /* This run has already hashed every byte. Later same-size media
             * changes are an accepted installer trade-off; a repair run
             * hashes existing files again, without trusting this bitmap. */
            if(s.verified_files[i/8]&(1u<<(i%8)))continue;
            if(!known_absent(&s,i) && verified(&s,target,&r,&s.checksums[i])){
                s.verified_files[i/8]|=(unsigned char)(1u<<(i%8));
                result->reused++;result->completed_bytes+=r.bytes;continue;
            }
            if(result->status){status=result->status;goto done;}
            if(r.op==60 && audio_drain(&s)){status=result->status;goto done;}
            /* Never carry a dependency's verification through regeneration. */
            s.verified_files[i/8]&=(unsigned char)~(1u<<(i%8));
            if(parents(&s,"data/assets/",r.name)){status=fail(&s,JGR_INSTALL_IO,NULL);goto done;}
            /* sceIoRename changes a basename within its source directory;
               staging beside the destination also makes publishing atomic. */
            int temporary_length=snprintf(temporary,sizeof temporary,"%s.setup-tmp",target);
            if(temporary_length<0 || (size_t)temporary_length>=sizeof temporary){status=fail(&s,JGR_INSTALL_IO,NULL);goto done;}
            struct stat st;
            if(info(temporary,&st)==0 && !S_ISREG(st.st_mode)){status=fail(&s,JGR_INSTALL_IO,"Invalid setup temporary file");goto done;}
            /* City and other converters allocate large working sets outside
             * scratch: release the optional index, file and pixels first.
             * Ordinary copies are bounded and often separate related model
             * banks, so preserve reuse across those copies. Reopening the
             * model scope is lazy and allocates nothing until it is used. */
            if(r.op!=30 && r.op!=JGR_INSTALL_COPY){
                jgr_models_cache_end();
                if(phase==0)jgr_models_cache_begin(s.disc);
            }
            if(r.op==JGR_INSTALL_COPY)copy_buffer_prepare(&s);
            else copy_buffer_release(&s);
            /* Model assembly also writes sequentially to the real output.
             * Buffer those small section/record writes so they do not become
             * thousands of Memory Stick calls. This optional buffer is only
             * 128 KiB; scratch already reserves much larger heap headroom.
             * Replay also copies its packed output sequentially. Release
             * this buffer before other jobs. Allocation
             * failure keeps ordinary stdio buffering and identical output. */
            if(r.op==JGR_INSTALL_COPY || r.op==81 || r.op==30 || r.op==89 || r.op==91 || r.op==93) {
                if(!s.output_buffer)s.output_buffer=malloc(COPY_BYTES);
            } else {free(s.output_buffer);s.output_buffer=NULL;}
            unsigned char *small_buffer=NULL;
            FILE *out=small_output_open(&r,&small_buffer);
            if(!out)out=fopen(temporary,"w+b");
            if(!out){status=fail(&s,JGR_INSTALL_IO,NULL);goto done;}
            if(s.output_buffer)(void)setvbuf(out,(char *)s.output_buffer,_IOFBF,COPY_BYTES);
            JgrCopyProgress before={0},after={0};int audio_was_running=s.audio_worker!=NULL;
            if(s.audio_worker){
                if(r.op==81){jgr_copy_worker_pause(s.audio_worker,1);jgr_copy_worker_wait_paused(s.audio_worker);}
                jgr_copy_worker_progress(s.audio_worker,&before);
            }
            s.work=work_begin(r.op);
            uint32_t work_started=s.work?copy_clock():0;
#if JGR_SETUP_CRASH
            /* In-memory fault context only. The periodic stick log may name
             * an earlier file when a converter faults between checkpoints. */
            release_report_log(r.name,(unsigned)strlen(r.name));
#endif
            errno=0;
            int bad=generate(&s,&r,out,&s.checksums[i]);
            int write_errno=errno;const char *write_step="conversion";
            if(s.work)s.work->convert_us+=(uint32_t)(copy_clock()-work_started);
            if(s.audio_worker){
                jgr_copy_worker_progress(s.audio_worker,&after);
                if(r.op==81)jgr_copy_worker_pause(s.audio_worker,0);
            }
            if(audio_was_running && r.op!=JGR_INSTALL_COPY &&
               (s.audio_worker?after.completed:result->pipeline.published)>before.completed)
                result->pipeline.overlapped_jobs++;
            uint32_t copy_finished=r.op==JGR_INSTALL_COPY?copy_clock():0;
            work_started=s.work?copy_clock():0;
            if(fflush(out)){if(!bad){write_errno=errno;write_step="flush";}bad=1;}
            long small_size=-1;int small_verified=0;
            if(small_buffer && !bad && (ferror(out) || fseek(out,0,SEEK_END) || (small_size=ftell(out))<0)) {
                write_errno=errno;write_step="size";bad=1;
            }
            if(fclose(out)){if(!bad){write_errno=errno;write_step="close";}bad=1;}
            if(small_buffer && !bad) {
                /* Check the final bytes, including every patched header,
                 * before creating a real temporary file. As with raw copies,
                 * successful exact writes/flush/close replace a disk reread. */
                if(!small_output_verified(&r,small_buffer,small_size,&s.checksums[i])) {
                    free(small_buffer);
                    status=fail(&s,JGR_INSTALL_CHECKSUM,"Generated file differs from this build's expected data");goto done;
                }
                if(notify(&s)){free(small_buffer);status=result->status;goto done;}
                FILE *file=fopen(temporary,"wb");
                if(!file){write_errno=errno;write_step="open";bad=1;}
                else {
                    if(setvbuf(file,NULL,_IONBF,0)){write_errno=errno;write_step="buffer";bad=1;}
                    if(!bad && fwrite(small_buffer,1,r.bytes,file)!=r.bytes){write_errno=errno;write_step="write";bad=1;}
                    if(fflush(file)){if(!bad){write_errno=errno;write_step="flush";}bad=1;}
                    if(fclose(file)){if(!bad){write_errno=errno;write_step="close";}bad=1;}
                }
                small_verified=!bad;
            }
            free(small_buffer);
            if(s.work)s.work->flush_us+=(uint32_t)(copy_clock()-work_started);
            if(r.op==JGR_INSTALL_COPY)result->copy.write_us+=(uint32_t)(copy_clock()-copy_finished);
            if(bad){
                const char *detail=result->detail[0]?NULL:jgr_disc_error(s.disc);
                if(detail && !*detail){
                    snprintf(result->detail,sizeof result->detail,"Asset %s failed (operation %u, errno %d)",write_step,r.op,write_errno);
                    detail=NULL;
                }
                status=fail(&s,result->status?result->status:JGR_INSTALL_IO,detail);goto done;
            }
            /* Sequential copies hash the exact buffers written. Seek-based
             * converters still require one completed-output hash pass. */
            copy_finished=r.op==JGR_INSTALL_COPY?copy_clock():0;
            work_started=s.work?copy_clock():0;
            /* COPY succeeds only after writing exactly the source extent's
             * expected size and matching its hash. Flush/close also succeeded
             * above. No extra temporary stat is needed; final verification
             * still checks each published size/type. Other outputs retain
             * their existing size or completed-file checksum validation. */
            int valid=(small_verified || r.op==JGR_INSTALL_COPY) ? 1:r.op==81 ?
                !info(temporary,&st) && S_ISREG(st.st_mode) && (uint64_t)st.st_size==r.bytes:
                verified(&s,temporary,&r,&s.checksums[i]);
            if(s.work)s.work->verify_us+=(uint32_t)(copy_clock()-work_started);
            if(!valid){status=fail(&s,result->status?result->status:JGR_INSTALL_CHECKSUM,"Generated file differs from this build's expected data");goto done;}
            work_started=s.work?copy_clock():0;
            int publish_failed=publish_asset(&s,i,temporary,target);
            if(s.work)s.work->publish_us+=(uint32_t)(copy_clock()-work_started);
            if(publish_failed){status=fail(&s,JGR_INSTALL_IO,NULL);goto done;}
            if(r.op==JGR_INSTALL_COPY){result->copy.publish_us+=(uint32_t)(copy_clock()-copy_finished);result->copy.files++;}
            s.verified_files[i/8]|=(unsigned char)(1u<<(i%8));
            result->written++;result->completed_bytes+=r.bytes;result->phase_current=result->written+result->reused;
            if(s.work){s.work->completed++;s.work->bytes+=r.bytes;s.work=NULL;}
            if(notify(&s)){status=result->status;goto done;}
        }
        free(s.output_buffer);s.output_buffer=NULL;
        copy_buffer_release(&s);
        if(phase==0){jgr_models_cache_end();if(audio_drain(&s)){status=result->status;goto done;}}
        if(phase==1){jgr_stage_texture_capture(NULL,NULL,NULL);jgr_stage_mips_cache_end();}
        if(phase==2)jgr_table_city_cache_end();
        if(phase==3){jgr_replay_session_destroy(s.replay);s.replay=NULL;}
        }
    }
    if(result->missing){status=fail(&s,JGR_INSTALL_INCOMPLETE,"Available conversions finished; required converters remain missing");goto done;}
    /* Files hashed in this process need only a final size/type check. This
       avoids another full-pack read; it intentionally does not detect a
       same-size change after hashing. Untrusted entries still get a hash. */
    if(phase(&s,JGR_INSTALL_PHASE_VERIFY,0,result->total)){status=result->status;goto done;}
    if(fseek(s.plan,PLAN_HEADER,SEEK_SET)){status=fail(&s,JGR_INSTALL_IO,NULL);goto done;}
    for(unsigned i=0;i<result->total;i++) {
        Recipe r;unsigned char raw[PLAN_RECORD];char target[PATH_MAXIMUM];
        if(read_recipe(&s,&r,raw)){status=fail(&s,JGR_INSTALL_PLAN,NULL);goto done;}
        strcpy(result->file,r.name);
        struct stat st;int valid=0;
        if(!path(&s,target,"data/assets/",r.name))valid=(s.verified_files[i/8]&(1u<<(i%8)))?
            !info(target,&st) && S_ISREG(st.st_mode) && (uint64_t)st.st_size==r.bytes:
            verified(&s,target,&r,&s.checksums[i]);
        if(!valid){status=fail(&s,result->status?result->status:JGR_INSTALL_CHECKSUM,NULL);goto done;}
        result->phase_current=i+1;if(notify(&s)){status=result->status;goto done;}
    }
    if(phase(&s,JGR_INSTALL_PHASE_PUBLISH,0,1)){status=result->status;goto done;}
    /* Reclaim conversion storage before the optional 512 KiB startup pack.
     * Its absence must never fail a verified installation: the game can read
     * the original tables and retry this cache on its next normal startup. */
    jgr_stage_texture_capture(NULL,NULL,NULL);
    jgr_stage_mips_cache_end();jgr_table_city_cache_end();jgr_models_cache_end();
    audio_release(&s);copy_buffer_release(&s);
    jgr_replay_session_destroy(s.replay);s.replay=NULL;
    free(s.output_buffer);s.output_buffer=NULL;
    /* Subset development plans do not contain the complete table registry. */
    if(s.manifest_crc==JGR_ASSET_MANIFEST_CRC32 && result->total==JGR_ASSET_FILE_COUNT) {
        (void)portable_startup_cache_prepare(s.root);
        /* Optional original startup artwork is derived only while setup
         * already owns the user's disc. Normal game boot never opens it. */
        if(s.disc)(void)portable_startup_warning_prepare(s.disc,s.root);
    }
    if(path(&s,temporary,"data/assets/",".assets-manifest.tmp") || publish(&s,temporary))status=fail(&s,JGR_INSTALL_IO,"Cannot publish the verified asset manifest");
    if(!status){result->phase=JGR_INSTALL_PHASE_DONE;result->phase_current=result->phase_total=1;if(notify(&s))status=result->status;}
done:
    if(s.work)s.work->failed++;
    jgr_stage_texture_capture(NULL,NULL,NULL);
    jgr_stage_mips_cache_end();
    jgr_table_city_cache_end();
    audio_release(&s);
    jgr_models_cache_end();
    copy_buffer_release(&s);
    if(status && *temporary)remove(temporary);
    directory_index_clear(&s);jgr_replay_session_destroy(s.replay);jgr_disc_close(s.disc);
    (void)jgr_scratch_setup_phase(0);
    free(s.recipe_keys);free(s.checksums);free(s.verified_files);free(s.absent_files);free(s.output_buffer);free(s.buffer);if(s.plan)fclose(s.plan);
    result->status=status;return status;
}
const char *jgr_install_error(int status)
{
    switch(status) {
    case JGR_INSTALL_OK:return "Game data prepared";
    case JGR_INSTALL_PLAN:return "Conversion instructions are damaged";
    case JGR_INSTALL_DISC:return "A supported game disc image is required";
    case JGR_INSTALL_CHECKSUM:return "Generated data failed verification";
    case JGR_INSTALL_SPACE:return "Not enough free Memory Stick space";
    case JGR_INSTALL_INCOMPLETE:return "This installer build is incomplete";
    case JGR_INSTALL_CANCELLED:return "Setup interrupted; completed files are retained";
    default:return "Asset preparation failed";
    }
}
