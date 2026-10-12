"""Setup replay counters: non-mutating live snapshots and cached sibling copies."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / 'psp/src'
DRIVER = r'''
#include <assert.h>
#include <stdint.h>
static uint64_t now;
static unsigned clocks;
static uint64_t test_clock(void){clocks++;return now;}
#define JGR_REPLAY_CLOCK() test_clock()
#include "portable_replay_session.c"
static FILE *unused_dependency(void *user,const char *name){(void)user;(void)name;assert(0);return NULL;}
int main(int argc,char **argv){
 assert(argc==2);JgrReplayStats a,b;
 jgr_replay_stats_reset();jgr_replay_stats(&a);assert(!clocks && !a.total_us && !a.active);
#if JGR_REPLAY_STATS
 now=100;replay_request_begin("synthetic");replay_stats.family=JGR_REPLAY_DEMO;
 now=120;jgr_replay_stats_stage(JGR_REPLAY_GENERATION);
 now=150;jgr_replay_stats(&a);jgr_replay_stats(&b);
 assert(a.total_us==50 && a.setup_us==20 && a.generation_us==30);
 assert(a.active_us==50 && a.state_us==30 && a.family_us[JGR_REPLAY_DEMO]==50);
 assert(!memcmp(&a,&b,sizeof a));
 now=170;jgr_replay_stats_stage(JGR_REPLAY_PACKING);
 now=180;replay_request_end(0,123);jgr_replay_stats(&a);
 assert(a.total_us==80 && a.generation_us==50 && a.packing_us==10 && a.output_bytes==123 && a.outputs==1 && !a.active);
 now=200;replay_request_begin("failure");replay_request_reused=1;
 now=210;replay_request_end(-1,999);jgr_replay_stats(&a);
 assert(a.total_us==90 && a.output_bytes==123 && a.failures==1 && a.reused_outputs==0);
 jgr_replay_stats_reset();jgr_replay_stats(&a);assert(!a.total_us && !a.outputs && !a.failures);
#endif
 JgrReplaySession *s=jgr_replay_session_create((JgrDisc *)(uintptr_t)1,argv[1],unused_dependency,NULL,NULL,NULL,NULL,0);assert(s);
 unsigned job=0;while(job<JOBS && (!*replay_jobs[job].outputs[1] || replay_jobs[job].selection.kind!=PORTABLE_REPLAY_GANG))job++;
 assert(job<JOBS);s->last=(int)job;
 for(unsigned slot=0;slot<2;slot++){
  FILE *cached=temporary(s,3+slot);assert(cached);assert(fwrite("replay",1,6,cached)==6);assert(!fclose(cached));
  FILE *out=tmpfile();assert(out);assert(!jgr_replay_session_generate(s,replay_jobs[job].outputs[slot],out));
  assert(ftell(out)==6);rewind(out);char data[6];assert(fread(data,1,6,out)==6 && !memcmp(data,"replay",6));fclose(out);
 }
 jgr_replay_stats(&a);
#if JGR_REPLAY_STATS
 assert(a.output_requests==2 && a.outputs==2 && a.reused_outputs==2 && !a.jobs && a.output_bytes==12);
 assert(a.family==JGR_REPLAY_OPPONENT);
#else
 assert(!clocks && !a.output_requests && !a.outputs && !a.total_us);
#endif
 jgr_replay_session_destroy(s);jgr_replay_stats(&b);assert(!memcmp(&a,&b,sizeof a));
 return 0;
}
'''

class ReplayStatsTests(unittest.TestCase):
    def test_enabled_and_disabled_counter_contract(self):
        sources = sorted(p for p in SRC.glob('portable_replay*.c') if p.name != 'portable_replay_session.c')
        sources += [SRC / (n + '.c') for n in ('portable_disc', 'portable_hardware', 'portable_tables', 'portable_convert')]
        with tempfile.TemporaryDirectory(prefix='jgr-replay-stats-') as temp:
            folder = Path(temp);driver = folder / 'check.c';driver.write_text(DRIVER)
            for enabled in (0, 1):
                with self.subTest(enabled=enabled):
                    exe = folder / ('check' + str(enabled))
                    subprocess.run(['cc', '-O2', '-std=gnu99', '-ffp-contract=off', '-Wall', '-Wextra',
                                    '-Werror', '-Wno-unused-function', '-fsanitize=address,undefined',
                                    '-DJGR_REPLAY_STATS=' + str(enabled), '-I' + str(SRC), str(driver),
                                    *map(str, sources), '-lm', '-o', str(exe)], check=True)
                    subprocess.run([str(exe), str(folder)], check=True)

if __name__ == '__main__':
    unittest.main()
