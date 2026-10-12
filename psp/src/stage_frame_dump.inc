/* This path runs only after the frame-spike trigger has stopped gameplay.
   Keep the normal renderer completely untouched: no per-object timers,
   retained duplicate masks, allocations, or file operations while playing. */
static void stage_dump_floats(FILE *out,const char *kind,unsigned index,const float *v,unsigned count)
{
    fprintf(out,"%s id=%u values=",kind,index);
    for(unsigned k=0;k<count;k++)fprintf(out,"%s%.9g",k ? ",":"",v[k]);
    fputc('\n',out);
}
static int stage_dump_box_gate(const StageBox *box,const float planes[6][4])
{
    for(unsigned j=0;j<6;j++) {
        const float *p=planes[j];
        float radius=fabsf(p[0])*box->extent[0]+fabsf(p[1])*box->extent[1]+fabsf(p[2])*box->extent[2];
        if(visibility_dot(p,box->center)+p[3]+radius<-.1f)return 0;
    }
    return 1;
}
void stage_dump_frame(const Stage *s,FILE *out)
{
    if(!out)return;
    if(!s || !s->base || !s->h){fputs("SPIKE_STAGE available=0\n",out);return;}
    int captured=stage_capture.valid && stage_capture.owner==s && stage_capture.base==s->base;
    const __typeof__(source_render) *render=captured ? &stage_capture.source:&source_render;
    int live_prepared=stage_prepared.owner==s && stage_prepared.base==s->base && stage_prepared.ready;
    int prepared=captured || live_prepared;
    unsigned cone_count=captured ? stage_capture.cone_count:stage_prepared.cone_count;
    unsigned selection_valid=captured ? stage_capture.selection_valid:visibility_selection.valid;
    unsigned selection_count=captured ? stage_capture.selection_count:visibility_selection.count;
    int source=render->owner==s && render->h;
    int visibility=visibility_owner==s->base;
    const StageVisibilityHeader *vh=visibility ? (const void *)visibility_arena:NULL;
    const StageBox *boxes=vh ? (const void *)(vh+1):NULL;
    const StageOccluder *occluders=vh ? (const void *)(boxes+vh->objects):NULL;
    const unsigned short *candidates=NULL;
    float eye[3]={0};int camera_valid=prepared && visibility_camera(stage_prepared.clip,eye);
    int grid_x=-1,grid_z=-1;
    if(camera_valid && vh) {
        grid_x=(int)floorf(eye[0]*(1.f/512))-vh->grid_x;
        grid_z=(int)floorf(eye[2]*(1.f/512))-vh->grid_z;
        if((unsigned)grid_x<vh->grid_w && (unsigned)grid_z<vh->grid_h)
            candidates=(const unsigned short *)(occluders+vh->occluders)+(grid_z*vh->grid_w+grid_x)*64;
    }
    fprintf(out,"SPIKE_STAGE available=1 version=1 key=%08x objects=%u batches=%u vertices=%u indices=%u textures=%u sky_explicit=%d prepared=%d pending=%u building=%u cullmode=%d mask_capacity=%u source=%d visibility=%d\n",
        visibility_hash(2166136261u,s->base,s->h->off_vert),s->h->n_obj,s->h->n_batch,s->h->n_vert,s->h->n_idx,s->h->n_tex,
        s->sky_explicit,prepared,captured ? stage_capture.pending:stage_prepared.pending,captured ? stage_capture.building:stage_prepared.building,
        captured ? stage_capture.cullmode:stage_prepared.cullmode,STAGE_VISIBILITY_CACHE,source,visibility);
    fprintf(out,"SPIKE_STAGE_STAMP captured=%d live_prepared=%d bytes=%u source_records_captured=%d\n",
        captured,live_prepared,captured ? (unsigned)sizeof stage_capture:0,captured && source);
    fputs("SPIKE_STAGE_NOTE masks=recorded_submitted_or_live_prepared_draw eligible=potential_submission_before_local_instance_and_triangle_tests recomputed_gates=scalar_diagnostic_only cached_candidates=identities_not_current_hidden_results published_cones=actor_certified_subset\n",out);
    fprintf(out,"SPIKE_STAGE_STATE commands=%d fragments=%d mesh_clipping=%d flat=%d visibility=%d geometry_occlusion=%d plane_occlusion=%d source_sectors=%d source_lod=%d source_ranges=%d drawn_objects=%d drawn_batches=%d drawn_triangles=%d texswitch=%d fragment_bytes=%u texture_resident=%u texture_bytes=%u texture_hits=%u texture_misses=%u\n",
        command_cache_enabled,fragment_batching,mesh_clipping,stage_flat,visibility_enabled,geometry_occlusion,plane_occlusion,
        source_sectors,source_lod,source_ranges,s->drawn_obj,s->drawn_batches,s->drawn_tris,s->texswitch,fragment_index_bytes,
        texture_owner==s ? texture_count:0,texture_owner==s ? texture_bytes:0,texture_hits,texture_misses);
    if(prepared) {
        stage_dump_floats(out,"SPIKE_STAGE_CLIP",0,stage_prepared.clip,16);
        for(unsigned p=0;p<6;p++) {
            stage_dump_floats(out,"SPIKE_STAGE_VIEW_PLANE",p,stage_prepared.visible[p],4);
            stage_dump_floats(out,"SPIKE_STAGE_CLIP_PLANE",p,stage_prepared.planes[p],4);
            stage_dump_floats(out,"SPIKE_STAGE_GUARD_PLANE",p,stage_prepared.guard[p],4);
        }
    }
    if(source) {
        fprintf(out,"SPIKE_STAGE_SOURCE stage=%u key=%08x sector=%d sectors=%u records=%u near_objects=%u view_valid=%d special=%d previous_special=%d prepared_previous_special=%d background=%d water_bound=%d water_mask=%u tangent=%.9g wedge=%.9g qualified=%u qualification_valid=%d restricted=%d qualification_builds=%u mask=%08x,%08x,%08x,%08x\n",
            render->h->stage,render->h->key,render->sector,render->h->sectors,render->h->records,
            render->h->near_objects,render->view_valid,render->special,render->previous_special,
            (captured ? stage_capture.previous_special:stage_prepared.previous_special),render->background_visible,render->water_bound,render->water_mask,
            render->horizontal_tangent,render->wedge_offset,render->qualified_count,render->qualification_valid,
            render->qualification_restrict,render->qualification_builds,render->sector_mask[0],render->sector_mask[1],render->sector_mask[2],render->sector_mask[3]);
        stage_dump_floats(out,"SPIKE_STAGE_SOURCE_EYE",0,render->eye,3);
        stage_dump_floats(out,"SPIKE_STAGE_SOURCE_FORWARD",0,render->forward,3);
        stage_dump_floats(out,"SPIKE_STAGE_SOURCE_CENTERS",0,render->center,3);
        stage_dump_floats(out,"SPIKE_STAGE_SOURCE_RADII",0,render->radius,3);
        for(unsigned i=0;i<render->h->records;i++) {
            if(!(i&31u))hang_mark(HANG_CAPTURE_WRITE);
            const StageRenderRecord *r=render->record+i;
            int sector_pass=!render->qualification_restrict || (r->kind>=6 && r->kind<=8) ||
                (render->sector_mask[r->sector/32]&(1u<<(r->sector%32)))!=0;
            fprintf(out,"SPIKE_STAGE_RECORD id=%u source=%08x kind=%u sector=%u sector_pass=%d choice=%u prop_hidden=%u near=%u,%u far=%u,%u position=%.9g,%.9g,%.9g lod2=%.9g\n",
                i,r->source,r->kind,r->sector,sector_pass,captured ? stage_capture_record(i)&3u:render->choice[i],
                captured ? (stage_capture_record(i)>>2)&1u:render->prop_hidden[i],
                r->near_first,r->near_count,r->far_first,r->far_count,r->position[0],r->position[1],r->position[2],r->lod2);
        }
    }
    unsigned eligible_objects=0,eligible_batches=0,eligible_triangles=0,alpha_triangles=0,clipped_batches=0;
    for(unsigned i=0;i<s->h->n_obj;i++) {
        if(!(i&31u))hang_mark(HANG_CAPTURE_WRITE);
        const JgsObj *o=s->obj+i;
        int sky=s->sky_explicit ? (o->flags&JGS_O_SKY)!=0:o->r>=1500.f;
        int selected=stage_render_object(s,o),cached=prepared && i<STAGE_VISIBILITY_CACHE;
        if(captured && source) {
            if(render->water_bound && (i==261 || i==262))selected=(render->water_mask&(i==261 ? 1:2))!=0;
            else if(o->pad) {
                unsigned record=stage_capture_record(o->pad-1);
                selected=!(record&4u) && (record&3u)==((o->flags&JGS_O_FAR) ? 2u:1u);
            } else selected=!(o->flags&JGS_O_FAR);
        }
        int sphere=prepared ? sphere_clip_mask(stage_prepared.visible,o):-999;
        int guard=prepared ? sphere_clip_mask(stage_prepared.guard,o):-999;
        int box=prepared && boxes && i<vh->objects ? stage_dump_box_gate(boxes+i,stage_prepared.visible):-1;
        int mask=cached ? stage_prepared.objects[i]:-999;
        int eligible=prepared && (cached ? mask>=0:!sky && selected && sphere>=0 && guard>=0);
        if(eligible)eligible_objects++;
        fprintf(out,"SPIKE_STAGE_OBJECT id=%u record=%u flags=%u source_selected=%d sky=%d mask_recorded=%d mask=%d eligible=%d scalar_sphere=%d scalar_guard=%d scalar_box=%d bounds=%.9g,%.9g,%.9g,%.9g batches=%u,%u instance=%u\n",
            i,o->pad ? o->pad-1:~0u,o->flags,selected,sky,cached,mask,eligible,sphere,guard,box,
            o->cx,o->cy,o->cz,o->r,o->first_batch,o->n_batch,s->object_instance ? s->object_instance[i]:0);
        if(boxes && i<vh->objects) {
            stage_dump_floats(out,"SPIKE_STAGE_OBJECT_CENTER",i,boxes[i].center,3);
            stage_dump_floats(out,"SPIKE_STAGE_OBJECT_EXTENT",i,boxes[i].extent,3);
        }
        for(unsigned b=o->first_batch;b<o->first_batch+o->n_batch;b++) {
            const JgsBatch *batch=s->batch+b;
            int batch_cached=cached && b<STAGE_VISIBILITY_CACHE;
            int batch_mask=batch_cached ? stage_prepared.batches[b]:-999;
            /* Shared-instance draws use their local prepared object instead
               of the ordinary world-space batch mask; do not mislabel them. */
            int instance=stage_object_instance(s,i)!=NULL;
            int effective_mask=cached ? mask:eligible ? guard:-1;
            if(!instance) {
                if(batch_cached && batch_mask<0)effective_mask=-1;
                else if(prepared && s->batch_bounds && o->n_batch>1 && s->batch_bounds[b].r<o->r*.8f) {
                    if(batch_cached)effective_mask=batch_mask;
                    else if(batch_clip_mask(stage_prepared.visible,s->batch_bounds+b)<0)effective_mask=-1;
                    else if(effective_mask>0)effective_mask=batch_clip_mask(stage_prepared.guard,s->batch_bounds+b);
                }
            }
            int batch_eligible=eligible && (instance || effective_mask>=0);
            int alpha=stage_batch_alpha(s,batch),pinned=texture_owner==s && batch->tex<STAGE_TEXTURE_CACHE_SLOTS && texture_pixels[batch->tex]!=NULL;
            if(batch_eligible) {
                eligible_batches++;eligible_triangles+=batch->n_idx/3;
                if(alpha)alpha_triangles+=batch->n_idx/3;
                if(instance || effective_mask>0)clipped_batches++;
            }
            fprintf(out,"SPIKE_STAGE_BATCH object=%u id=%u mask_recorded=%d mask=%d effective_mask=%d instance_path=%d eligible=%d vertices=%u indices=%u triangles=%u tex=%u flags=%u alpha=%d pinned=%d vertex_offset=%u index_offset=%u\n",
                i,b,batch_cached,batch_mask,effective_mask,instance,batch_eligible,batch->n_vert,batch->n_idx,batch->n_idx/3,batch->tex,batch->flags,alpha,pinned,batch->vert_off,batch->idx_off);
            if(s->batch_bounds)stage_dump_floats(out,"SPIKE_STAGE_BATCH_BOUND",b,&s->batch_bounds[b].cx,4);
            if(visibility && visibility_batch_extents && b<visibility_batch_count)
                stage_dump_floats(out,"SPIKE_STAGE_BATCH_EXTENT",b,visibility_batch_extents[b],3);
        }
    }
    fprintf(out,"SPIKE_STAGE_ELIGIBLE objects=%u batches=%u triangles=%u alpha_triangles=%u clipped_or_instance_batches=%u\n",
        eligible_objects,eligible_batches,eligible_triangles,alpha_triangles,clipped_batches);
    if(vh) {
        fprintf(out,"SPIKE_STAGE_VISIBILITY magic=%.4s key=%08x objects=%u occluders=%u groups=%u grid_origin=%d,%d grid_size=%u,%u cell=%d,%d camera_valid=%d selection_valid=%u selection_count=%u actor_cones=%u\n",
            vh->magic,vh->key,vh->objects,vh->occluders,vh->groups,vh->grid_x,vh->grid_z,vh->grid_w,vh->grid_h,grid_x,grid_z,
            camera_valid,selection_valid,selection_count,prepared ? cone_count:0);
        stage_dump_floats(out,"SPIKE_STAGE_SELECTION_EYE",0,visibility_selection.eye,3);
        stage_dump_floats(out,"SPIKE_STAGE_SELECTION_FORWARD",0,visibility_selection.forward,3);
        for(unsigned i=0;i<vh->occluders;i++) {
            int slot=-1,cell_slot=-1,published=-1;
            if(selection_valid)for(unsigned j=0;j<selection_count;j++)if(visibility_selection.ids[j]==i)slot=(int)j;
            if(candidates)for(unsigned j=0;j<64;j++)if(candidates[j]==i)cell_slot=(int)j;
            if(prepared)for(unsigned j=0;j<cone_count;j++)if(stage_prepared.cone_ids[j]==i)published=(int)j;
            if(slot<0 && cell_slot<0 && published<0)continue;
            const StageOccluder *o=occluders+i;unsigned alias=o->count>>3;
            int owner_mask=prepared && o->owner<STAGE_VISIBILITY_CACHE ? stage_prepared.objects[o->owner]:-999;
            int alias_mask=prepared && alias && alias-1<STAGE_VISIBILITY_CACHE ? stage_prepared.objects[alias-1]:-999;
            fprintf(out,"SPIKE_STAGE_OCCLUDER id=%u cached_slot=%d cell_slot=%d published_slot=%d owner=%u alternate=%d owner_mask=%d alternate_mask=%d corners=%u score=%.9g center=%.9g,%.9g,%.9g radius=%.9g\n",
                i,slot,cell_slot,published,o->owner,alias ? (int)alias-1:-1,owner_mask,alias_mask,visibility_corners(o),
                prepared ? visibility_screen_score(o,stage_prepared.clip):0.f,o->center[0],o->center[1],o->center[2],o->radius);
            stage_dump_floats(out,"SPIKE_STAGE_OCCLUDER_POINTS",i,&o->point[0][0],12);
            stage_dump_floats(out,"SPIKE_STAGE_OCCLUDER_AREA",i,o->area,3);
            if(slot>=0) {
                for(unsigned j=0;j<visibility_selection.hidden_count[slot];j++)
                    fprintf(out,"SPIKE_STAGE_CACHED_OBJECT occluder=%u object=%u\n",i,visibility_selection.hidden[slot][j]);
                for(unsigned j=0;j<visibility_selection.batch_count[slot];j++)
                    fprintf(out,"SPIKE_STAGE_CACHED_BATCH occluder=%u object=%u batch=%u\n",i,visibility_selection.batch[slot][j].object,visibility_selection.batch[slot][j].batch);
            }
        }
    }
    if(prepared)for(unsigned j=0;j<cone_count;j++) {
        const StageOcclusionCone *cone=stage_prepared.cones+j;
        fprintf(out,"SPIKE_STAGE_ACTOR_CONE id=%u occluder=%u planes=%u far_w=%.9g depth_offset=%.9g\n",
            j,stage_prepared.cone_ids[j],cone->count,cone->far_w,stage_prepared.depth_offset);
        stage_dump_floats(out,"SPIKE_STAGE_ACTOR_CONE_DEPTH",j,cone->depth,4);
        for(unsigned p=0;p<cone->count;p++)stage_dump_floats(out,"SPIKE_STAGE_ACTOR_CONE_PLANE",j*5+p,cone->plane[p],4);
    }
    fputs("SPIKE_STAGE_END\n",out);
}
