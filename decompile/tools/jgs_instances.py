"""JGI1: optional local prop geometry, with unchanged world object bounds.

The extension follows the aligned batch table, before off_vert. IDs in the
uint16 object map are one based; zero keeps the ordinary world-space path.
Each 64-byte record stores row-major affine3x4 and a local bounding sphere.
"""
import math
import struct

JGS_O_INSTANCE=4

def align16(n):return (n+15)&~15

def read_instances(data):
    _,objects,batches,_,_=struct.unpack_from('<5I',data,4)
    _,oo,ob,ov,_,_=struct.unpack_from('<6I',data,24)
    at=align16(ob+batches*16)
    result=[None]*objects
    if at+4>ov or data[at:at+4]!=b'JGI1':
        if any(struct.unpack_from('<I',data,oo+i*32+24)[0]&JGS_O_INSTANCE for i in range(objects)):
            raise ValueError('instance object without JGI1 metadata')
        return result
    magic,count,instances,reserved=struct.unpack_from('<4s3I',data,at)
    if count!=objects or not instances or instances>65535 or reserved:
        raise ValueError('invalid JGI1 header')
    records=align16(at+16+objects*2)
    if records+instances*64>ov:raise ValueError('truncated JGI1 records')
    ids=struct.unpack_from('<%dH'%objects,data,at+16)
    values=[]
    for i in range(instances):
        row=struct.unpack_from('<16f',data,records+i*64)
        if not all(math.isfinite(x) for x in row) or row[15]<0:
            raise ValueError('nonfinite JGI1 transform or invalid local sphere')
        values.append(((*row[:12],0.,0.,0.,1.),row[12:16]))
    for i,slot in enumerate(ids):
        flag=struct.unpack_from('<I',data,oo+i*32+24)[0]&JGS_O_INSTANCE
        if slot>instances or bool(slot)!=bool(flag):raise ValueError('JGI1 object map mismatch')
        if slot:result[i]=values[slot-1]
    return result

def encode_instances(instances):
    ids=[];records=[];unique={}
    for item in instances:
        if item is None:ids.append(0);continue
        matrix,sphere=item
        packed=struct.pack('<16f',*matrix[:12],*sphere)
        if packed not in unique:
            unique[packed]=len(records)+1;records.append(packed)
        ids.append(unique[packed])
    if not records:return b''
    if len(records)>65535:raise ValueError('too many JGI1 instances')
    data=bytearray(struct.pack('<4s3I',b'JGI1',len(ids),len(records),0))
    data+=struct.pack('<%dH'%len(ids),*ids)
    data+=b'\0'*(align16(len(data))-len(data))
    return bytes(data)+b''.join(records)

def transform_points(points,matrix):
    """Apply a JGI1 affine matrix to a NumPy array with trailing dimension3."""
    import numpy as np
    m=np.asarray(matrix,dtype='float64').reshape(4,4)
    return np.asarray(points,dtype='float64')@m[:3,:3].T+m[:3,3]

def expand_world_vertices(data,objects,batches,vertices):
    """Tool-only world view; return batches/vertices for existing sidecar bakers.

    Shared input vertices cannot have one global world position. Give each
    instanced batch an expanded world slice while retaining all batch IDs.
    The JGS itself and its source fingerprints remain untouched.
    """
    import numpy as np
    instances=read_instances(data)
    if not any(instances):return batches,vertices
    result=list(batches);parts=[vertices];count=len(vertices);seen=set()
    for oi,item in enumerate(instances):
        if item is None:continue
        matrix,_=item;first,n=objects[oi][4:6]
        for bi in range(first,first+n):
            if bi in seen:raise ValueError('instance objects share a batch ID')
            seen.add(bi);vo,io,nv,ni,tex,flags=batches[bi]
            points=transform_points(vertices[vo:vo+nv],matrix)
            result[bi]=(count,io,nv,ni,tex,flags);parts.append(points);count+=nv
    return result,np.concatenate(parts)
