"""Dialogue metadata conversion with fabricated records, no game content."""
from pathlib import Path
import re
import sys
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'decompile/tools'))
from dialogue_assets import mode_source


class DialogueDataTests(unittest.TestCase):
    def row(self,ident,mode):
        return dict(id=ident,kind='PLAY dialogue',references=[dict(secondary=mode)])

    def test_sparse_bounded_data_table(self):
        text=mode_source([self.row(1,4),self.row(6,7)],count=8)
        values=[int(v) for v in re.search(r'\{([^}]+)\}',text)[1].replace('\n','').split(',') if v.strip()]
        self.assertEqual(values,[0,4,0,0,0,0,7,0])
        self.assertNotIn('switch',text)

    def test_bad_and_conflicting_records_fail_closed(self):
        for rows in ([self.row(-1,4)],[self.row(8,4)],[self.row(1,0)],
                     [self.row(1,4),self.row(1,4)],
                     [dict(id=1,kind='PLAY dialogue',references=[dict(secondary=4),dict(secondary=5)])]):
            with self.subTest(rows=rows):
                with self.assertRaises(ValueError):mode_source(rows,count=8)

    def test_non_dialogue_does_not_populate_table(self):
        row=self.row(2,4);row['kind']='bespoke cinematic'
        text=mode_source([row],count=4)
        self.assertIn('0,0,0,0,',text)


if __name__=='__main__':unittest.main()
