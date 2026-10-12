"""Canonical identities for audited address-only format changes.

CAN3 runtime source words are opaque equality keys, scoped to a sub-set.
They are never dereferenced (canplay.c). Renaming keys is equivalent only
if zero/nonzero and the complete alias partition are retained; every other
byte remains part of this digest.
"""
import hashlib
import struct

def cans_digest(data):
    if len(data)<12 or data[:4]!=b'CAN3':raise ValueError('Expected CAN3')
    count,sets=struct.unpack_from('<II',data,4)
    if len(data)!=12+count*28:raise ValueError('Malformed CAN3')
    out=bytearray(data);mapping={}
    for i in range(count):
        at=12+i*28;subset,area,key=struct.unpack_from('<III',data,at)
        if subset>=sets or area>=5:raise ValueError('Invalid CAN3 namespace')
        if key:
            scope=(subset,key)
            if scope not in mapping:mapping[scope]=len(mapping)+1
            struct.pack_into('<I',out,at+8,mapping[scope])
    return hashlib.sha256(out).hexdigest()
