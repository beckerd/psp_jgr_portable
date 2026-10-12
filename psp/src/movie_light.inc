/* Ordinary Ninja world lighting is independent of translation and camera.
   Prove that each normal's owner and ancestors have fixed rotation/scale;
   tint that private movie batch once, allowing its unchanged local vertices
   to use existing GE bone packets. Cel actors and reflection UVs stay live.
   No new resident allocation or texture/animation reduction. */
static int movie_fixed_linear(const Scene *s,uint32_t motion,unsigned bone)
{
    if(!motion)return 1;
    unsigned kind=scene_word(s,motion+8),channels=kind==0x30007 ? 3:kind==0x20003 ? 2:0;
    if(!channels)return 0;
    uint32_t entry=scene_word(s,motion)+bone*channels*8;
    for(unsigned ch=1;ch<channels;ch++) {
        uint32_t p=scene_word(s,entry+ch*4),n=scene_word(s,entry+channels*4+ch*4);
        if(!p)continue;
        if(!n || n>s->length/16 || !scene_range(s,p,n*16))return 0;
        /* Constant keyed zero rotation/unit scale also interpolates exactly.
           Other keyed constants can change in the last bit through Ninja's
           weighted interpolation, so conservatively leave those dynamic. */
        for(unsigned k=0;k<n;k++)for(unsigned axis=0;axis<3;axis++)
            if(scene_word(s,p+k*16+4+axis*4)!=(ch==1 ? 0u:0x3f800000u))return 0;
    }
    return 1;
}
static unsigned movie_prepare_lighting(const Movie *movie,Chars *bank)
{
#ifdef JGR_MOVIE_LIGHT_REFERENCE
    return 0;
#else
    const Scene *s=&movie->source;unsigned model=0,total=0;
    uint32_t segments=scene_word(s,0x8c800004);
    for(int segment=0;segment<movie->count;segment++) {
        uint32_t desc=scene_word(s,segments+segment*4),actors=scene_word(s,desc+8);
        uint32_t lp=scene_word(s,desc+4);
        CharLight light={0};int valid_light=scene_read(s,scene_word(s,lp),&light,24);
        for(unsigned i=0;i<256;i++,model++) {
            uint32_t a=scene_word(s,actors+i*4);if(!a)break;
            if(model>=bank->h->n_chars)return total;
            unsigned flags=scene_word(s,a+12);if(!valid_light || flags&2 || flags==8)continue;
            const JgcChar *ch=bank->chars+model;unsigned char stable[CHAR_RENDER_MAX_BONES];
            if(ch->nbones>CHAR_RENDER_MAX_BONES)continue;
            uint32_t motion=scene_word(s,a+4);int valid=1;
            for(unsigned j=0;j<ch->nbones;j++) {
                chars_sample_pose(bank,ch,-1,0,j,movie_pose+j);
                if(motion && !scene_sample_njs_nodes(s,motion,0,j,0,ch->nbones,movie_pose+j)){valid=0;break;}
                int parent=bank->bones[ch->bone0+j].parent;
                stable[j]=movie_fixed_linear(s,motion,j) && (parent<0 || stable[parent-ch->bone0]);
            }
            if(valid)total+=chars_prepare_movie_light(bank,model,movie_pose,stable,&light);
        }
    }
    return total;
#endif
}
