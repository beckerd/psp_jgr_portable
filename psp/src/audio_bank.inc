typedef struct {unsigned char header[64],*samples;OsbProgram program;unsigned bytes,owned;} OsbSample;
static void osb_parts_free(OsbSample *p,unsigned count)
{if(p){for(unsigned i=0;i<count;i++)if(p[i].owned)free(p[i].samples);free(p);}}
/* Immutable original SOSP headers and compressed waves. No decoder or
   mixer changes; a large bank can occupy several small free heap spans. */
static int osb_read_mask(FILE *f,const unsigned r[2],unsigned first,unsigned count,uint64_t mask,OsbSample **out)
{
    OsbSample *parts=NULL;int error=-3;*out=NULL;
    if(!count || count>64 || first>64-count || fseek(f,0,SEEK_END))return error;
    long file_size=ftell(f);if(file_size<16)return error;
    unsigned header[4],offsets[64];
    if(r[1]<16 || r[1]>0x200000 || r[0]>(unsigned long)file_size || r[1]>(unsigned long)file_size-r[0])goto done;
    if(fseek(f,r[0],SEEK_SET) || fread(header,16,1,f)!=1 || memcmp(header,"SOSB",4) ||
       header[3]<first+count || header[3]>64 || header[3]>(r[1]-16)/4 || fread(offsets,4,header[3],f)!=header[3])goto done;
    parts=calloc(count,sizeof *parts);if(!parts){error=-4;goto done;}
    for(unsigned i=0;i<count;i++) {
        if(!(mask&(UINT64_C(1)<<i)))continue;
        OsbSample *p=parts+i;unsigned off=offsets[first+i];
        if(off>r[1] || r[1]-off<64 || fseek(f,r[0]+off,SEEK_SET) || fread(p->header,64,1,f)!=1)goto done;
        unsigned short control,low,length;memcpy(&control,p->header+4,2);memcpy(&low,p->header+6,2);memcpy(&length,p->header+10,2);
        unsigned format=(control>>7)&3,start=((control&127u)<<16)|low;
        if(format!=0 && format!=2)goto done;
        p->bytes=format==2 ? (length+1u)/2:length*2u;
        if(!length || start>r[1] || p->bytes>r[1]-start)goto done;
        for(unsigned j=0;j<i;j++)if(parts[j].program.offset==start && parts[j].bytes==p->bytes){p->samples=parts[j].samples;break;}
        if(!p->samples) {
            p->samples=malloc(p->bytes);if(!p->samples){error=-4;goto done;}p->owned=1;
            if(fseek(f,r[0]+start,SEEK_SET) || fread(p->samples,1,p->bytes,f)!=p->bytes)goto done;
        }
        if(osb_program_parts(p->header,64,p->samples,p->bytes,&p->program))goto done;
    }
    *out=parts;parts=NULL;error=0;
done:osb_parts_free(parts,count);return error;
}
static int osb_read_range(FILE *f,const unsigned r[2],unsigned first,unsigned count,OsbSample **out)
{return osb_read_mask(f,r,first,count,UINT64_MAX,out);}
static int osb_read_parts(FILE *f,const unsigned r[2],unsigned count,OsbSample **out)
{if(r[0]<144){*out=NULL;return -3;}return osb_read_range(f,r,0,count,out);}
static int osb_read_bank(FILE *f,const unsigned r[2],unsigned count,unsigned char **image,OsbSample **parts)
{
    *image=NULL;*parts=NULL;
    if(r[0]<144 || r[1]<16 || r[1]>0x200000)return -3;
    unsigned char *data=malloc(r[1]);
    if(!data)return osb_read_parts(f,r,count,parts);
    if(fseek(f,r[0],SEEK_SET) || fread(data,1,r[1],f)!=r[1]){free(data);return -3;}
    for(unsigned i=0;i<count;i++){OsbProgram p;if(osb_program(data,r[1],i,&p)){free(data);return -3;}}
    *image=data;return 0;
}
