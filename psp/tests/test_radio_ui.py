"""Radio navigation and animation timing with a synthetic motion length."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CHECK = r'''
#include "radio_ui.h"
#include <assert.h>
#include <stdio.h>

static void idle(RadioUi *r)
{
    for(int i=0;i<100 && r->phase!=RADIO_IDLE;i++)radio_ui_tick(r);
    assert(r->phase==RADIO_IDLE && r->frame==0 && r->fade==0);
}

int main(void)
{
    RadioUi r;radio_ui_init(&r,12);
    assert(r.track==0 && r.frame==0 && r.fade==25);
    assert(radio_ui_input(&r,0,1,0)==-1);
    for(int i=0;i<24;i++)assert(radio_ui_tick(&r)==-1);
    assert(radio_ui_tick(&r)==0x89 && r.phase==RADIO_FORWARD);
    for(int i=1;i<12;i++){radio_ui_tick(&r);assert(r.frame==i && r.phase==RADIO_FORWARD);}
    radio_ui_tick(&r);assert(r.phase==RADIO_IDLE && r.frame==0);
    for(int i=0;i<100;i++)assert(radio_ui_tick(&r)==-1 && r.frame==0);

    /* Previous from the first song wraps and turns the dial backwards. */
    assert(radio_ui_input(&r,1,0,0)==0xa2 && r.track==25 && r.frame==11);
    for(int i=10;i>=0;i--){radio_ui_tick(&r);assert(r.frame==i && r.phase==RADIO_REVERSE);}
    radio_ui_tick(&r);assert(r.phase==RADIO_IDLE);
    assert(radio_ui_input(&r,0,1,0)==0x89 && r.track==0);
    assert(radio_ui_input(&r,0,1,0)==-1 && r.track==0);
    idle(&r);
    /* Visit every track, including forward wrap. */
    for(int i=1;i<=26;i++){
        assert(radio_ui_input(&r,0,1,0)==0x89+i%26);
        assert(r.track==i%26);idle(&r);
    }
    assert(radio_ui_input(&r,1,1,0)==0xa2);idle(&r);
    assert(radio_ui_input(&r,1,1,1)==1 && r.phase==RADIO_FADE_OUT);
    for(int i=0;i<24;i++){radio_ui_tick(&r);assert(r.phase==RADIO_FADE_OUT);}
    radio_ui_tick(&r);assert(r.phase==RADIO_DONE && r.fade==25);
    assert(radio_ui_input(&r,0,1,0)==-1);

    /* Re-entry resets selection; timing agrees at 30 and 60 render FPS. */
    radio_ui_init(&r,12);RadioUi other;radio_ui_init(&other,12);
    int starts=0,other_starts=0;
    for(int i=0;i<35;i++){
        starts+=radio_ui_update(&r,1.f/30.f)>=0;
        other_starts+=radio_ui_update(&other,1.f/60.f)>=0;
        other_starts+=radio_ui_update(&other,1.f/60.f)>=0;
        assert(r.phase==other.phase && r.frame==other.frame && r.fade==other.fade);
    }
    assert(starts==1 && other_starts==1 && r.track==0);
    radio_ui_init(&r,0);idle(&r);assert(r.nframes==1);
    puts("Radio input, track wrapping, single-cycle motion, fades and clock: PASS");
}
'''


class RadioUiTest(unittest.TestCase):
    def test_navigation_and_timing(self):
        with tempfile.TemporaryDirectory(prefix='jgr-radio-') as folder:
            source, binary = Path(folder) / 'radio.c', Path(folder) / 'radio'
            source.write_text(CHECK)
            subprocess.run(['cc', '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'psp/src'), str(source), '-o', str(binary)], check=True)
            subprocess.run([binary], check=True)


if __name__ == '__main__':
    unittest.main()
