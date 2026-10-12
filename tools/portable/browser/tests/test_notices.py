"""Complete license preservation and safe visible notice markup."""
import hashlib
import html
from html.parser import HTMLParser
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import notices


class NoticeParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.tags = []
        self.pre = False
        self.text = ''

    def handle_starttag(self, tag, attrs):
        self.tags.append((tag, dict(attrs)))
        if tag == 'pre':
            self.pre = True

    def handle_endtag(self, tag):
        if tag == 'pre':
            self.pre = False

    def handle_data(self, text):
        if self.pre:
            self.text += text


class NoticeTests(unittest.TestCase):
    def release(self):
        digest = 'b' * 64
        return dict(archive_sha256=digest, archive_filename='jgr-portable-source-' + digest + '.zip')

    def test_all_canonical_texts_preserved_with_input_hashes(self):
        root = Path(__file__).resolve().parents[4]
        files = {name: (root / name).read_bytes() for name in notices.NOTICE_PATHS}
        text, rows = notices.collect(files)
        parser = NoticeParser()
        parser.feed(notices.render(text, self.release()))
        self.assertEqual(parser.text, text)
        for row in rows:
            original = files[row['path']]
            self.assertIn(original.decode('utf-8'), parser.text)
            self.assertEqual(row['sha256'], hashlib.sha256(original).hexdigest())
            self.assertEqual(row['bytes'], len(original))
        pre = next(attrs for tag, attrs in parser.tags if tag == 'pre')
        self.assertEqual(pre['tabindex'], '0')
        self.assertEqual(pre['aria-label'], 'Project and third-party license notices')
        self.assertEqual(len(rows), 7)
        self.assertTrue({'LICENSE', 'THIRD_PARTY_NOTICES.md'} <= set(files))

    def test_markup_like_notice_text_stays_text(self):
        text = '</pre><script>bad & "quoted"</script>\n<hello>'
        rendered = notices.render(text, self.release())
        parser = NoticeParser(); parser.feed(rendered)
        self.assertEqual(parser.text, text)
        self.assertNotIn('script', [tag for tag, _ in parser.tags])
        self.assertIn(html.escape(text), rendered)

    def test_separate_release_source_link_preserves_notices_and_checksum(self):
        release = self.release()
        base = 'https://github.com/beckerd/psp_jgr_portable/releases/download/1.0'
        for url in (base, base + '/'):
            rendered = notices.render('Full notices', release, url)
            parser = NoticeParser(); parser.feed(rendered)
            link = next(attrs for tag, attrs in parser.tags if tag == 'a')
            self.assertEqual(link['href'], base + '/' + release['archive_filename'])
            self.assertEqual(parser.text, 'Full notices')
            self.assertIn(release['archive_sha256'], rendered)
        parser = NoticeParser(); parser.feed(notices.render('Full notices', release))
        link = next(attrs for tag, attrs in parser.tags if tag == 'a')
        self.assertEqual(link['href'], release['archive_filename'])

    def test_invalid_source_download_locations_rejected(self):
        for url in ('', 'javascript:alert(1)', 'http://example.com', '/source',
                    'https://user:secret@example.com', 'https://example.com/?q=1',
                    'https://example.com/#source', 'https://example.com/with space'):
            with self.subTest(url=url), self.assertRaises(ValueError):
                notices.render('notice', self.release(), url)

    def test_missing_empty_invalid_and_malicious_inputs_fail(self):
        files = {name: b'License\n' for name in notices.NOTICE_PATHS}
        for bad in (None, b'', b'\xff', b'contains\0nul'):
            changed = dict(files)
            if bad is None:
                del changed[notices.NOTICE_PATHS[0]]
            else:
                changed[notices.NOTICE_PATHS[0]] = bad
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                notices.collect(changed)
        release = self.release(); release['archive_filename'] = '../bad".zip'
        with self.assertRaises(ValueError):
            notices.render('notice', release)

    def test_missing_or_duplicate_markers_fail_before_replacement(self):
        replacements = {'<!-- A -->': 'first', notices.MARKER: 'second'}
        for template in ('<!-- A -->', notices.MARKER * 2 + '<!-- A -->'):
            with self.assertRaisesRegex(ValueError, 'marker missing or repeated'):
                notices.replace_markers(template, replacements)
        self.assertEqual(notices.replace_markers('<!-- A -->' + notices.MARKER, replacements), 'firstsecond')


if __name__ == '__main__':
    unittest.main()
