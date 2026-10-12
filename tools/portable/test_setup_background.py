from pathlib import Path
import struct
import tempfile
import unittest
import zlib

from PIL import Image
import setup_background as background


class SetupBackgroundTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / 'art.png'

    def test_psp_channel_order_stride_and_header(self):
        image = Image.new('RGB', (480, 272))
        for x, color in enumerate(((255, 0, 0), (0, 255, 0), (0, 0, 255))):
            image.putpixel((x, 0), color)
        image.putpixel((479, 271), (255, 255, 255))
        image.save(self.source)
        data, metadata = background.encode_png(self.source)
        self.assertEqual(data[:8], b'JGRBG01\0')
        self.assertEqual(struct.unpack_from('<III', data, 8), (480, 272, 261120))
        self.assertEqual(struct.unpack_from('<HHH', data, 24), (0x001f, 0x07e0, 0xf800))
        self.assertEqual(data[-2:], b'\xff\xff')
        self.assertEqual(len(data), 261144)
        self.assertEqual(struct.unpack_from('<I', data, 20)[0], zlib.crc32(data[24:]) & 0xffffffff)
        self.assertFalse(metadata['verified_game_asset'])

    def test_alpha_is_composited_over_black(self):
        image = Image.new('RGBA', (480, 272), (255, 255, 255, 0))
        image.putpixel((1, 0), (255, 0, 0, 128))
        image.save(self.source)
        data, _ = background.encode_png(self.source)
        self.assertEqual(struct.unpack_from('<HH', data, 24), (0, 16))

    def test_wrong_dimensions_and_non_png_rejected(self):
        Image.new('RGB', (479, 272)).save(self.source)
        with self.assertRaisesRegex(ValueError, '480x272'):
            background.encode_png(self.source)
        Image.new('RGB', (480, 272)).save(self.source, format='BMP')
        with self.assertRaisesRegex(ValueError, 'PNG'):
            background.encode_png(self.source)

    def test_corrupt_truncated_appended_and_wrong_header_rejected(self):
        Image.new('RGB', (480, 272)).save(self.source)
        data, _ = background.encode_png(self.source)
        invalid = (data[:-1], data + b'\0', b'BADMAGIC' + data[8:],
                   data[:8] + struct.pack('<I', 512) + data[12:],
                   data[:-1] + bytes([data[-1] ^ 1]))
        for changed in invalid:
            with self.subTest(length=len(changed)), self.assertRaises(ValueError):
                background.validate(changed)

    def test_output_is_new_and_conversion_is_deterministic(self):
        Image.new('RGB', (480, 272), 'blue').save(self.source)
        destination = self.root / 'data/setup/background.jbg'
        first = background.convert(self.source, destination)
        self.assertEqual(first['bytes'], destination.stat().st_size)
        self.assertEqual(destination.read_bytes(), background.encode_png(self.source)[0])
        with self.assertRaises(FileExistsError):
            background.convert(self.source, destination)


if __name__ == '__main__':
    unittest.main()
