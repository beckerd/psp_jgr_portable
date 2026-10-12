/* Exact cel sampler with a prepared header/frame and optional key brackets.
   The original sample_njs remains the independent reference path. */
int scene_njs_cel_prepare(SceneNjsCelSample *out,const Scene *s,uint32_t motion,float frame,unsigned nodes)
{
    if(!out)return 0;
    memset(out,0,sizeof *out);
    uint32_t header[3];
    if(!s || !nodes || nodes>CHAR_RENDER_MAX_BONES || !isfinite(frame) || frame<0 ||
       !scene_read(s,motion,header,sizeof header))return 0;
    unsigned channels=header[2]==0x30007 ? 3:header[2]==0x20003 ? 2:0;
    if(!channels || !header[1] || frame>=header[1] ||
       !scene_range(s,header[0],nodes*channels*8u))return 0;
    out->scene=s;out->mdata=header[0];out->frames=header[1];out->channels=channels;
    out->mdata_bytes=scene_bytes(s,header[0],nodes*channels*8u);
    out->frame=frame;out->next=(uint32_t)frame+1;out->nodes=nodes;
    out->wrap_t=frame-(uint32_t)frame;
    return 1;
}
static uint32_t njs_cel_key_word(const Scene *s,const unsigned char *keys,uint32_t address,unsigned at)
{
    uint32_t value;
    if(keys){memcpy(&value,keys+at,4);return value;}
    return scene_word(s,address+at);
}
static int njs_cel_bracket(const Scene *s,const unsigned char *keys,uint32_t address,
                          unsigned n,unsigned next,unsigned hi,uint32_t *a,uint32_t *b)
{
    if(!hi || hi>=n)return 0;
    *a=njs_cel_key_word(s,keys,address,(hi-1)*16);
    *b=njs_cel_key_word(s,keys,address,hi*16);
    return *a<next && next<=*b;
}
int scene_njs_cel_sample(const SceneNjsCelSample *sample,unsigned bone,uint32_t hint[3],CharPose *pose)
{
    if(!sample || !sample->scene || !pose || bone>=sample->nodes)return 0;
    const Scene *s=sample->scene;unsigned channels=sample->channels;
    uint32_t row[6];unsigned offset=bone*channels*8u;
    if(sample->mdata_bytes)memcpy(row,sample->mdata_bytes+offset,channels*8u);
    else if(!scene_read(s,sample->mdata+offset,row,channels*8u))return 0;
    CharPose result=*pose;result.ev&=~0x20u;
    uint32_t hints[3]={0};
    for(unsigned ch=0;ch<channels;ch++) {
        uint32_t p=row[ch],n=row[channels+ch];
        if(!p)continue;
        if(!n || n>s->length/16 || !scene_range(s,p,n*16))return 0;
        const unsigned char *keys=s->image ? s->image+(p-s->base):scene_bytes(s,p,n*16);
        unsigned lo,hi;float t;
        if(sample->next>=sample->frames || !sample->next) {
            lo=n-1;hi=0;t=sample->wrap_t;
        } else {
            uint32_t a=0,b=0;hi=hint ? hint[ch]:0;
            int found=hint && njs_cel_bracket(s,keys,p,n,sample->next,hi,&a,&b);
            /* Steady playback commonly crosses one key. Larger jumps,
               reverse seeks and stale hints use the same bounded search
               as the reference; no unbounded cursor walking. */
            if(!found && hint && hi<n-1 && njs_cel_bracket(s,keys,p,n,sample->next,hi+1,&a,&b)) {
                hi++;found=1;
            }
            if(!found) {
                lo=0;hi=n;
                while(lo<hi) {
                    unsigned k=lo+(hi-lo)/2;
                    if(njs_cel_key_word(s,keys,p,k*16)<sample->next)lo=k+1;else hi=k;
                }
                if(!lo || lo>=n)return 0;
                hi=lo;a=njs_cel_key_word(s,keys,p,(hi-1)*16);b=njs_cel_key_word(s,keys,p,hi*16);
                if(b<=a)return 0;
            }
            lo=hi-1;t=(sample->frame-(float)a)/(float)(b-a);
        }
        for(unsigned j=0;j<3;j++) {
            uint32_t aw=njs_cel_key_word(s,keys,p,lo*16+4+j*4);
            uint32_t bw=njs_cel_key_word(s,keys,p,hi*16+4+j*4);
            if(ch==1) {
                int32_t av=(int32_t)aw,bv=(int32_t)bw;
                result.ang[j]=t==0 ? av:t==1 ? bv:(int32_t)((float)(int32_t)((uint32_t)bv-(uint32_t)av)*t+(float)av);
            } else {
                float av,bv;memcpy(&av,&aw,4);memcpy(&bv,&bw,4);
                float v=source_fmac(t,bv-av,av);if(!isfinite(v))return 0;
                if(ch==0)result.pos[j]=v;else result.scl[j]=v;
            }
        }
        result.ev&=~(1u<<ch);hints[ch]=hi;
    }
    *pose=result;
    if(hint)memcpy(hint,hints,sizeof hints);
    return 1;
}
