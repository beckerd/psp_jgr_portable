/* 050e18/050e48: original 16-frame Kogane water, shared lossless T8
   palette. Immutable frames need no GE cache flush or per-frame disk I/O. */
static struct {const Stage *owner;unsigned char *data;unsigned frame;float remainder;} water_surface;
static void stage_water_forget(const Stage *s)
{if(water_surface.owner==s){free(water_surface.data);memset(&water_surface,0,sizeof water_surface);}}
static int stage_water_load(Stage *s,const char *render_path)
{
    char path[256];const char *slash=strrchr(render_path,'/');size_t prefix=slash ? (size_t)(slash-render_path+1):0;
    if(prefix+sizeof "kogane_surface.bin">sizeof path)return -1;
    memcpy(path,render_path,prefix);strcpy(path+prefix,"kogane_surface.bin");
    SceUID fd=sceIoOpen(path,PSP_O_RDONLY,0777);if(fd<0)return -1;
    unsigned header[8];int valid=sceIoLseek(fd,0,PSP_SEEK_END)==66624 && sceIoLseek(fd,0,PSP_SEEK_SET)==0 &&
        sceIoRead(fd,header,sizeof header)==sizeof header && !memcmp(header,"KWS1",4) &&
        header[1]==16 && header[2]==64 && header[3]==64 && header[4]==60 && header[5]==64 && header[6]==1088 && header[7]==66624 &&
        s->h->n_tex>60 && s->tex[60].w==64 && s->tex[60].h==64;
    unsigned char *data=valid ? memalign(64,66624):NULL;unsigned got=0;
    if(data){sceIoLseek(fd,0,PSP_SEEK_SET);while(got<66624){int n=sceIoRead(fd,data+got,66624-got);if(n<=0)break;got+=(unsigned)n;}}
    sceIoClose(fd);
    if(got!=66624){free(data);return -1;}
    for(unsigned i=1088;i<66624;i++)if(data[i]>=32){free(data);return -1;}
#ifdef __PSP__
    sceKernelDcacheWritebackRange(data,66624);
#endif
    water_surface.owner=s;water_surface.data=data;water_surface.frame=0;water_surface.remainder=0;return 0;
}
void stage_water_step(Stage *s,float dt)
{
    if(water_surface.owner!=s || !isfinite(dt) || dt<=0)return;
    water_surface.remainder+=fminf(dt,.1f);
    while(water_surface.remainder+1e-7f>=1.f/30.f){
        water_surface.remainder-=1.f/30.f;water_surface.frame=(water_surface.frame+1)&15;
    }
    if(water_surface.remainder<0)water_surface.remainder=0;
}
static int stage_water_texture(const Stage *s,unsigned texture)
{return water_surface.owner==s && texture==60;}
