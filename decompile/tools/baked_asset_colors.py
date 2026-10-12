"""Copy audited offline colors into a shipped bank, preserving every other byte."""
import struct


def recolor_character(original, model, baked, baked_model):
    """Transfer one character's RGB without regenerating unrelated models.

    Shared banks can contain later enemy geometry fixes absent from the
    generic exporter. Match this character's skeleton, batches and local
    vertices explicitly instead of requiring identical whole-bank layouts.
    """
    def parts(bank, ci):
        if bank[:4]!=b'JGC2':raise ValueError('character colors require JGC2')
        h=struct.unpack_from('<16I',bank,4)
        if not 0<=ci<h[0]:raise ValueError('invalid character')
        first,count=struct.unpack_from('<2I',bank,h[7]+48*ci)
        skeleton=[];batches=[];vertices=[]
        for bone in range(first,first+count):
            at=h[8]+48*bone
            parent=struct.unpack_from('<h',bank,at)[0]
            skeleton.append((parent-first if parent>=0 else -1,bank[at+2:at+40]))
            start,n=struct.unpack_from('<2I',bank,at+40)
            batches.append(('bone',n))
            for k in range(start,start+n):
                v,indices,nv,ni,_,flags=struct.unpack_from('<II4H',bank,h[9]+16*k)
                batches.append((nv,ni,flags,bank[h[11]+2*indices:h[11]+2*(indices+ni)]))
                if flags&8:continue
                for i in range(v,v+nv):
                    at=h[10]+28*i
                    # Owner indices are model-local in JGC2.
                    vertices.append((at,bank[at:at+8]+bank[at+11:at+28]))
        return skeleton,batches,vertices
    a=parts(original,model);b=parts(baked,baked_model)
    if a[:2]!=b[:2] or len(a[2])!=len(b[2]):raise ValueError('character geometry changed')
    result=bytearray(original)
    for (at,geometry),(other,expected) in zip(a[2],b[2]):
        if geometry!=expected:raise ValueError('character vertex or alpha changed')
        result[at+8:at+11]=baked[other+8:other+11]
    return bytes(result)


def _face_vertices(bank, model, selected_bone):
    if bank[:4] != b'JGC2':raise ValueError('face colors require JGC2')
    h=struct.unpack_from('<16I',bank,4)
    if not 0<=model<h[0]:raise ValueError('invalid face model')
    bone,nb=struct.unpack_from('<2I',bank,h[7]+48*model)
    if selected_bone is not None and not 0<=selected_bone<nb:raise ValueError('missing face bone')
    for j in range(nb):
        if selected_bone is not None and j!=selected_bone:continue
        first,n=struct.unpack_from('<2I',bank,h[8]+48*(bone+j)+40)
        for k in range(first,first+n):
            v,_,nv,_,_,flags=struct.unpack_from('<II4H',bank,h[9]+16*k)
            if flags&8:continue
            for i in range(v,v+nv):
                at=h[10]+28*i
                yield at,struct.unpack_from('<3f',bank,at+12)


def match_face_colors(source, model, target, target_bone=None):
    """Match original PLAY facial overlays to the baked head (bone 11).

    The blink/wink/mouth meshes repeat the head's surface vertices with a
    tiny depth offset. Baking those separately in their own local frame
    gives them a different shade. Copy RGB from the corresponding head
    vertex; retain the authored expressions, texture, alpha and outlines.
    A geometry mismatch fails the export instead of coloring another part.
    """
    head=list(_face_vertices(source,model,11))
    if not head:raise ValueError('empty source face')
    result=bytearray(target);matched=0
    for at,p in _face_vertices(target,0,target_bone):
        distance,other=min((sum((x-y)**2 for x,y in zip(p,q)),offset) for offset,q in head)
        if distance>0.005**2:
            # The spray head has four different neck vertices. They retain
            # its own bake; facial expression overlays must match entirely.
            if target_bone is not None:continue
            raise ValueError('facial overlay does not match head surface')
        result[at+8:at+11]=source[other+8:other+11];matched+=1
    if not matched:raise ValueError('empty facial overlay')
    return bytes(result),matched


def bake_expression_colors(source, model, target, lighting, shadow_lift=0):
    """Bake the companion in its PLAY head frame, then join shared surfaces.

    Mew/Yo-Yo/Cube use authored 0.01/0.015-unit overlay offsets, larger than
    Gum's. Mew and Cube also have new mouth/interior vertices. Those
    retain their source normals and receive the same half-strength palette
    as PLAY; copying an arbitrary distant head vertex would flatten them.
    This only changes RGB. Neither source textures nor expression keys move.
    """
    if not 0 <= shadow_lift <= 1:raise ValueError('invalid expression shadow lift')
    from ninjalib import LIGHT_DIR,mmul,node_matrix,mtrans,xform,xform_n
    from baked_light import shade

    def matrices(bank,ci):
        h=struct.unpack_from('<16I',bank,4)
        first,count=struct.unpack_from('<2I',bank,h[7]+48*ci)
        out={-1:mtrans(0,0,0)}
        for j in range(count):
            parent,ev,*v=struct.unpack_from('<hH3f3i3fII',bank,h[8]+48*(first+j))
            out[j]=mmul(out[parent-first if parent>=0 else -1],node_matrix(ev,v[:3],v[3:6],v[6:9]))
        return out

    head=list(_face_vertices(source,model,11))
    vertices=list(_face_vertices(target,0,None))
    if not head or not vertices:raise ValueError('empty facial overlay')
    h=struct.unpack_from('<16I',target,4)
    if lighting[:4]!=b'PLT1' or len(lighting)!=20+h[3]*20:
        raise ValueError('invalid facial lighting stream')
    digest=2166136261
    for i,value in enumerate(target[h[10]:h[10]+h[3]*28]):
        if not 8<=i%28<12:digest=((digest^value)*16777619)&0xffffffff
    if struct.unpack_from('<4I',lighting,4)!=(h[3],1,digest,20):
        raise ValueError('facial lighting does not match geometry')
    head_matrix=matrices(source,model)[11];local=matrices(target,0)
    result=bytearray(target);matched=0;baked=0;max_offset=0.
    for at,pos in vertices:
        index=(at-h[10])//28;owner=struct.unpack_from('<H',target,at+24)[0]
        pos=xform(local[owner],pos)
        distance,other=min((sum((x-y)**2 for x,y in zip(pos,q)),offset) for offset,q in head)
        if distance<=0.016**2:
            result[at+8:at+11]=source[other+8:other+11]
            matched+=1;max_offset=max(max_offset,distance**.5)
            continue
        normal=struct.unpack_from('<3f',lighting,20+index*20)
        material,flags=struct.unpack_from('<2I',lighting,32+index*20)
        if flags&0x100:
            # The shipped PLAY bake is half legacy smooth lighting, half
            # original city cel lighting. Use the same owner-local normal
            # for the former and the attached head frame for the latter.
            normal=xform_n(local[owner],normal)
            k=.55+.45*max(0.,sum(a*b for a,b in zip(normal,LIGHT_DIR)))
            argb=(material&0xff00ff00)|((material&255)<<16)|((material>>16)&255)
            cel=shade('shibuya',argb,xform_n(head_matrix,normal),True)
            rgb=[int((int(((material>>s)&255)*k)+((cel>>s)&255))*.5+.5) for s in (0,8,16)]
            # Shared surfaces already inherit the softened head. Only new
            # mouth/interior vertices need the same material-relative lift.
            result[at+8:at+11]=bytes(int(c+(((material>>s)&255)-c)*shadow_lift+.5)
                                      for c,s in zip(rgb,(0,8,16)))
            baked+=1
    if matched*2<len(vertices):raise ValueError('expression does not match the selected head')
    return bytes(result),dict(matched_head_vertices=matched,baked_interior_vertices=baked,
                             maximum_shared_surface_offset=max_offset)


def recolor(original, baked, models=None, strength=1.0):
    if not 0 <= strength <= 1:
        raise ValueError('color strength must be between zero and one')
    original, baked = bytes(original), bytes(baked)
    if original[:4] != baked[:4] or original[:4] not in (b'JGS1', b'JGC2'):
        raise ValueError('incompatible color banks')
    stage = original[:4] == b'JGS1'
    count = 11 if stage else 16
    a = struct.unpack_from('<%dI' % count, original, 4)
    b = struct.unpack_from('<%dI' % count, baked, 4)
    stride = 24 if stage else 28
    va, vb, nv = (a[8], b[8], a[3]) if stage else (a[10], b[10], a[3])
    if a[:5 if stage else 7] != b[:5 if stage else 7]:
        raise ValueError('geometry counts changed')
    # Export options may change later texture/key layouts; none are copied.
    # The object/bone/batch descriptors must identify identical vertices.
    first = a[5] if stage else a[7]
    other = b[5] if stage else b[7]
    if original[first:va] != baked[other:vb]:
        raise ValueError('geometry descriptors changed')
    selected = set(range(nv)) if models is None else set()
    if models is not None:
        if stage: raise ValueError('model selector requires a JGC bank')
        for model in models:
            if not 0 <= model < a[0]: raise ValueError('invalid model')
            bone, nb = struct.unpack_from('<2I', original, a[7]+48*model)
            for j in range(bone, bone+nb):
                batch, n = struct.unpack_from('<2I', original, a[8]+48*j+40)
                for k in range(batch, batch+n):
                    v, _, nvert, _, _, flags = struct.unpack_from('<II4H', original, a[9]+16*k)
                    # Preserve ignore-light outlines exactly.
                    if not flags & 8: selected.update(range(v, v+nvert))
    result = bytearray(original); changed = 0
    for i in range(nv):
        old = original[va+i*stride:va+(i+1)*stride]
        new = baked[vb+i*stride:vb+(i+1)*stride]
        if old[:8]+old[12:] != new[:8]+new[12:]:
            raise ValueError('geometry changed at vertex %d' % i)
        if i in selected:
            if old[11] != new[11]: raise ValueError('alpha changed')
            rgb = bytes(int(a+(b-a)*strength+0.5) for a,b in zip(old[8:11],new[8:11]))
            result[va+i*stride+8:va+i*stride+11] = rgb
            changed += old[8:11] != rgb
    return bytes(result), changed
