import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from semantic import cans_digest
from observations import validate
from setup_assets import publish


def cans(keys):
    return b'CAN3'+struct.pack('<II',len(keys),2)+b''.join(struct.pack('<III4f',subset,0,key,1.,2.,3.,4.) for subset,key in keys)


class ValidationTests(unittest.TestCase):
    def test_identity_renaming_preserves_aliases_and_namespaces(self):
        self.assertEqual(cans_digest(cans([(0,10),(0,10),(1,10),(1,20),(0,0)])),
                         cans_digest(cans([(0,99),(0,99),(1,7),(1,99),(0,0)])))
        self.assertNotEqual(cans_digest(cans([(0,10),(0,10)])),cans_digest(cans([(0,10),(0,20)])))
        self.assertNotEqual(cans_digest(cans([(0,0)])),cans_digest(cans([(0,10)])))
        a=bytearray(cans([(0,10)]));struct.pack_into('<f',a,24,5.)
        self.assertNotEqual(cans_digest(a),cans_digest(cans([(0,10)])))

    def test_capture_receipt_rejects_wrong_disc_and_private_state(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp);fp={'fingerprint':'test'}
            receipt=dict(schema=1,disc=fp,fresh_boot=True,private_states_used=False,capture_complete=True,
                         exit_code=0,emulator_sha256='0'*64,kind='title')
            for name in ('title_actor_camera.bin','title.csv'):(path/name).write_bytes(b'fixture')
            (path/'capture.json').write_text(json.dumps(receipt))
            self.assertEqual(validate(path,fp)[0],'title')
            with self.assertRaises(ValueError):validate(path,{'fingerprint':'other'})
            for field,value in [('private_states_used',True),('capture_complete',False),('fresh_boot',False)]:
                bad=dict(receipt);bad[field]=value;(path/'capture.json').write_text(json.dumps(bad))
                with self.subTest(field=field),self.assertRaises(ValueError):validate(path,fp)

    def test_atomic_publish_verifies_bytes_and_preserves_existing_destination(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);src=root/'source';src.mkdir();(src/'asset').write_bytes(b'fixture')
            row=dict(path='asset',bytes=7,sha256=hashlib.sha256(b'fixture').hexdigest(),crc32=f'{zlib.crc32(b"fixture"):08x}')
            bad=dict(row);bad['sha256']='0'*64
            with self.assertRaises(ValueError):publish(src,root/'bad',[bad],{})
            self.assertFalse((root/'bad').exists());self.assertFalse((root/'bad.partial').exists())
            publish(src,root/'pack',[row],{})
            self.assertEqual((root/'pack/asset').read_bytes(),b'fixture')
            self.assertTrue(json.loads((root/'pack/catalog.json').read_text())['complete'])
            with self.assertRaises(ValueError):publish(src,root/'pack',[row],{})
            self.assertEqual((root/'pack/asset').read_bytes(),b'fixture')

if __name__=='__main__':unittest.main()
