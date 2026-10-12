/* stage_prepared_forget invalidates live actor queries after a prop hit,
   but retains the arrays. Borrow those arrays until the next hard reset;
   save only metadata that next-frame logic may change. */
static struct {
    const Stage *owner;const unsigned char *base;
    unsigned valid,ready,pending,building,cone_count,selection_valid,selection_count;
    int cullmode,previous_special;
    __typeof__(source_render) source;
    unsigned char records[(4096u*3u+7u)/8u]; /* two choice bits + hidden bit */
} stage_capture;
unsigned stage_capture_bytes(void){return sizeof stage_capture;}
static void stage_capture_forget(void)
{if(stage_capture.valid)stage_capture.valid=0;}
unsigned stage_capture_submit(const Stage *s)
{
    stage_capture_forget();
    if(!s || !s->base || stage_prepared.owner!=s || stage_prepared.base!=s->base ||
       !stage_prepared.ready || stage_prepared.pending || stage_prepared.building)return 0;
    stage_capture.owner=s;stage_capture.base=s->base;
    stage_capture.ready=stage_prepared.ready;stage_capture.pending=stage_prepared.pending;
    stage_capture.building=stage_prepared.building;stage_capture.cone_count=stage_prepared.cone_count;
    stage_capture.cullmode=stage_prepared.cullmode;stage_capture.previous_special=stage_prepared.previous_special;
    stage_capture.selection_valid=visibility_selection.valid;stage_capture.selection_count=visibility_selection.count;
    stage_capture.source=source_render;
    memset(stage_capture.records,0,sizeof stage_capture.records);
    if(source_render.owner==s)for(unsigned i=0;i<source_render.h->records;i++) {
        unsigned value=(source_render.choice[i]&3u)|((source_render.prop_hidden[i]!=0)<<2);
        unsigned at=i*3u,byte=at>>3,shift=at&7u;
        stage_capture.records[byte]|=(unsigned char)(value<<shift);
        if(shift>5)stage_capture.records[byte+1]|=(unsigned char)(value>>(8-shift));
    }
    stage_capture.valid=1;return sizeof stage_capture;
}
static unsigned stage_capture_record(unsigned i)
{
    unsigned at=i*3u,byte=at>>3,shift=at&7u,value=stage_capture.records[byte];
    if(shift>5)value|=(unsigned)stage_capture.records[byte+1]<<8;
    return (value>>shift)&7u;
}
