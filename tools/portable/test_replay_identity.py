"""Synthetic profile, export-normalization and checksum boundary checks."""
import copy
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zlib
from install_plan import HEADER, RECORD, make_plan
from package import asset_manifest
import replay_identity as identity
from replay_recipes import recipes


def digest(data):
    return hashlib.sha256(data).hexdigest()


class ReplayIdentity(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='jgr-replay-identity-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'psp/src').mkdir(parents=True)
        metadata = self.root / 'tools/portable';metadata.mkdir(parents=True)
        self.cue=bytearray(128)
        struct.pack_into('<12I',self.cue,40,1,88,16,4,2,104,24,4,0,0,1,0)
        cue_crc=zlib.crc32(self.cue[40:88])
        struct.pack_into('<4s9I',self.cue,0,b'JGSD',2,2,128,cue_crc,zlib.crc32(self.cue[88:]),88,1,4,0)
        rows = []
        # Setup always links its exception reporter, including fixture trees
        # created before a source manifest exists for include-closure discovery.
        for name in sorted(identity.required_sources(self.root) | {'psp/src/crash.c'}):
            data = b'/* Synthetic source fixture. */\n'
            if name.endswith('/player.c'):
                data = b'#include "fixture_data.inc"\nint fixture(void){return 0;}\n'
            if name.endswith('/source_data_ptr_challenge_demo_data.c'):
                data = ('source_data_pointers_load_file("source_data_ptr_challenge_demo_data.jsd",sections,2u,128u,0x%08xu,1u);\n'%cue_crc).encode()
            (self.root / name).write_bytes(data)
            rows.append(dict(path=name,sha256=digest(data),kind='portable_implementation',review='Synthetic test implementation'))
        rows.append(dict(path='psp/src/fixture_data.inc',sha256=digest(b'synthetic'),kind='game_data',
                         output='psp/src/generated/source_data/fixture_data.inc',review='Synthetic include-remapping fixture'))
        (metadata / 'source_data_manifest.json').write_text(json.dumps(dict(schema=1,files=rows)))
        names = identity.native_outputs() | {'unchanged.bin'}
        self.catalog = dict(files=[dict(path=name,bytes=128,crc32=zlib.crc32(name.encode()),sha256=digest(name.encode())) for name in sorted(names)])
        self.sources = identity.canonical_sources(self.root,identity.required_sources(self.root))
        # Real exports have separate ledgers: tool inputs are not runtime
        # transformed sources. Keep this fixture structurally faithful.
        (metadata / 'public_sources.json').write_text(json.dumps(dict(schema=1,files=[])))
        import source_data
        rowmap={row['path']:row for row in rows}
        reviewed=[]
        for name,hashed in self.sources.items():
            transformed=source_data.transform((self.root/name).read_bytes(),rowmap[name],
                {'fixture_data.inc':'generated/source_data/fixture_data.inc'})
            reviewed.append(dict(path=name,sha256=hashed,bytes=len(transformed)))
        (metadata / 'public_review.json').write_text(json.dumps(dict(schema=1,
            reviewed_files=reviewed,findings=[],whole_tree_review_complete=True)))
        self.ledger = dict(schema=1,profile='native-v1',
            baseline_manifest_crc32='%08x'%(zlib.crc32(asset_manifest(self.catalog['files']))&0xffffffff),
            generator_sources=self.sources,generator_sha256=identity.fingerprint(self.sources),
            validation=dict(target='psp',method='ppsspp',fidelity_reviewed=True,receipt_sha256=digest(b'synthetic proof'),cue_layout=identity.cue_layout(self.root)),
            files=[dict(path=n,bytes=128,crc32=zlib.crc32((n+'native').encode()),sha256=digest((n+'native').encode())) for n in sorted(identity.native_outputs())])

    def plan(self):
        return make_plan(self.catalog,{'unchanged.bin':'decompile/extracted/TEST.BIN'},conversions=recipes(),
                         replay_profile='native-v1',native_identity=self.ledger,source_root=self.root)

    def test_native_plan_binds_every_replay_and_preserves_baseline(self):
        before=copy.deepcopy(self.catalog);data,report=self.plan();header=HEADER.unpack_from(data)
        self.assertEqual(header[1],4);self.assertEqual(report['replay_profile'],'native-v1')
        applied=identity.apply_native_catalog(self.catalog,self.ledger,self.root)
        self.assertEqual(header[6],zlib.crc32(asset_manifest(applied['files']))&0xffffffff)
        expected={r['path']:r for r in applied['files']}
        for i in range(header[3]):
            op,size,a,b,c,d,checksum,name,source=RECORD.unpack_from(data,HEADER.size+i*RECORD.size)
            name=name.split(b'\0')[0].decode();self.assertEqual(checksum[:4],expected[name]['crc32'].to_bytes(4,'little'))
            self.assertEqual((a,b,c,d),(1,0,0,0) if op==91 else (0,0,0,0))
        self.assertEqual(self.catalog,before)
        original,_=make_plan(self.catalog,{},conversions=recipes())
        self.assertEqual(HEADER.unpack_from(original)[1],3)
        self.assertNotEqual(HEADER.unpack_from(original)[6],header[6])

    def test_title_requires_reviewed_native_identity(self):
        self.assertIn('title_route.bin',identity.native_outputs())
        self.assertEqual(len(identity.native_outputs()),68)
        missing=copy.deepcopy(self.ledger)
        missing['files']=[row for row in missing['files'] if row['path']!='title_route.bin']
        with self.assertRaisesRegex(ValueError,'every native route'):
            identity.apply_native_catalog(self.catalog,missing,self.root)

    def test_missing_duplicate_unknown_identity_and_unreviewed_target_rejected(self):
        changes=[lambda d:d['files'].pop(),lambda d:d['files'].append(d['files'][0]),
                 lambda d:d['files'][0].update(path='unexpected.bin'),
                 lambda d:d['validation'].update(target='host'),
                 lambda d:d['validation'].update(fidelity_reviewed=False),
                 lambda d:d.update(baseline_manifest_crc32='00000000'),
                 lambda d:d['files'][0].update(crc32=-1),
                 lambda d:d['validation']['cue_layout'].update(crc32=0)]
        for change in changes:
            with self.subTest(change=change):
                bad=copy.deepcopy(self.ledger);change(bad)
                with self.assertRaises(ValueError):identity.apply_native_catalog(self.catalog,bad,self.root)

    def test_changed_missing_generator_and_cyclic_identity_header_rejected(self):
        bad=copy.deepcopy(self.ledger);bad['generator_sources'].pop('psp/src/player.c');bad['generator_sha256']=identity.fingerprint(bad['generator_sources'])
        with self.assertRaises(ValueError):identity.apply_native_catalog(self.catalog,bad,self.root)
        bad=copy.deepcopy(self.ledger);bad['generator_sources']['psp/src/portable_asset_identity.h']=digest(b'header');bad['generator_sha256']=identity.fingerprint(bad['generator_sources'])
        with self.assertRaises(ValueError):identity.apply_native_catalog(self.catalog,bad,self.root)
        (self.root/'psp/src/player.c').write_text('/* changed after validation */\n')
        with self.assertRaisesRegex(ValueError,'source changed'):self.plan()

    def test_clean_export_and_private_source_have_identical_fingerprint(self):
        import source_data
        document=source_data.load_manifest(self.root/'tools/portable/source_data_manifest.json')
        rows={r['path']:r for r in document['files']}
        includes={'fixture_data.inc':'generated/source_data/fixture_data.inc'}
        for name in self.sources:
            p=self.root/name;p.write_bytes(source_data.transform(p.read_bytes(),rows[name],includes))
        self.assertEqual(identity.canonical_sources(self.root,self.sources),self.sources)
        self.plan()

    def test_tool_input_ledger_cannot_admit_changed_runtime(self):
        path=self.root/'psp/src/player.c';path.write_text('/* unreviewed runtime change */\n')
        policy=self.root/'tools/portable/public_sources.json'
        policy.write_text(json.dumps(dict(schema=1,files=[dict(path='psp/src/player.c',
            sha256=digest(path.read_bytes()),review='Synthetic wrong-ledger admission')])))
        with self.assertRaisesRegex(ValueError,'source changed'):self.plan()

    def test_transitive_public_header_is_bound_to_generator(self):
        metadata=self.root/'tools/portable/source_data_manifest.json'
        document=json.loads(metadata.read_text())
        path=self.root/'psp/src/player.h';path.write_text('/* synthetic actor layout */\n')
        document['files'].append(dict(path='psp/src/player.h',sha256=digest(path.read_bytes()),
            kind='portable_implementation',review='Synthetic transitive header'))
        source=self.root/'psp/src/portable_replay_pack.h'
        source.write_text('#include "player.h"\n')
        next(r for r in document['files'] if r['path'].endswith('/portable_replay_pack.h'))['sha256']=digest(source.read_bytes())
        metadata.write_text(json.dumps(document))
        self.assertIn('psp/src/player.h',identity.required_sources(self.root))
        self.ledger['generator_sources']=identity.canonical_sources(self.root,identity.required_sources(self.root))
        self.ledger['generator_sha256']=identity.fingerprint(self.ledger['generator_sources'])
        self.plan()
        path.write_text('/* changed actor layout */\n')
        with self.assertRaisesRegex(ValueError,'source changed'):self.plan()

    def test_native_catalog_cannot_silently_select_original_converter(self):
        applied=identity.apply_native_catalog(self.catalog,self.ledger,self.root)
        with self.assertRaisesRegex(ValueError,'original replay'):make_plan(applied,{})

    def test_native_profile_requires_crc_and_all_native_recipes(self):
        recipes_missing=recipes();recipes_missing.pop(next(iter(identity.native_outputs())))
        with self.assertRaisesRegex(ValueError,'every native replay converter'):
            make_plan(self.catalog,{},recipes_missing,replay_profile='native-v1',native_identity=self.ledger,source_root=self.root)
        raw={next(iter(identity.native_outputs())):'decompile/extracted/TEST.BIN'}
        with self.assertRaisesRegex(ValueError,'every native replay converter'):
            make_plan(self.catalog,raw,recipes(),replay_profile='native-v1',native_identity=self.ledger,source_root=self.root)
        self.catalog['files'][-1].pop('crc32')
        with self.assertRaisesRegex(ValueError,'complete CRC32 catalog'):self.plan()

    def test_collector_requires_reviewed_exact_outputs_and_source_fingerprint(self):
        output=self.root/'output';output.mkdir()
        for name in identity.native_outputs():(output/name).write_bytes(self.cue if name.endswith('.jsd') else ('synthetic '+name).encode())
        receipt=dict(schema=1,target='psp',method='ppsspp',fidelity_reviewed=True,
            generator_sha256=identity.fingerprint(self.sources),cue_layout=identity.cue_layout(self.root),
            files=[identity.output_identity(output/n,n) for n in sorted(identity.native_outputs())])
        receipt_path=self.root/'review.json';receipt_path.write_text(json.dumps(receipt))
        ledger=identity.collect_native_identity(self.catalog,output,receipt_path,self.root)
        self.assertEqual(ledger['files'],receipt['files'])
        self.assertEqual(ledger['validation']['receipt_sha256'],digest(receipt_path.read_bytes()))
        identity.apply_native_catalog(self.catalog,ledger,self.root)
        for field,value in [('target','host'),('fidelity_reviewed',False),('generator_sha256','0'*64)]:
            bad=dict(receipt);bad[field]=value;receipt_path.write_text(json.dumps(bad))
            with self.assertRaises(ValueError):identity.collect_native_identity(self.catalog,output,receipt_path,self.root)
        receipt_path.write_text(json.dumps(receipt))
        (output/receipt['files'][0]['path']).write_bytes(b'changed after review')
        with self.assertRaisesRegex(ValueError,'output hashes differ'):identity.collect_native_identity(self.catalog,output,receipt_path,self.root)

    def test_cue_layout_verifies_directory_and_payload(self):
        p=self.root/'cue.jsd';p.write_bytes(self.cue)
        self.assertEqual(identity.checked_cue_layout(p),identity.cue_layout(self.root))
        for offset in (40,100):
            bad=bytearray(self.cue);bad[offset]^=1;p.write_bytes(bad)
            with self.assertRaisesRegex(ValueError,'layout or checksum'):identity.checked_cue_layout(p)


if __name__=='__main__':unittest.main()
