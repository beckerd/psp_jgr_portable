"""Optional traffic I/O failures must not detach the world's update tail."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function(source, name):
    """Compile the actual main.c adapters, not a second implementation."""
    match = re.search(r'^static (?:void|int|unsigned) ' + name + r'\([^\n]*\)\s*\{', source, re.M)
    if not match:
        raise AssertionError('Missing real function: ' + name)
    start = source.index('{', match.start())
    # Preserve offsets while ignoring braces inside strings and comments.
    clean = re.sub(r'"(?:\\.|[^"\\])*"|/\*.*?\*/|//[^\n]*',
                   lambda m: ' ' * len(m[0]), source[start:], flags=re.S)
    depth = 0
    for index, char in enumerate(clean):
        depth += (char == '{') - (char == '}')
        if depth == 0:
            return source[match.start():start + index + 1]
    raise AssertionError('Unterminated real function: ' + name)


PREAMBLE = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {void *h;} Chars;
typedef struct {unsigned stage,count;int error,defer_bodies;void *context;void (*released)(void *,unsigned,unsigned);} PathWorld;
typedef struct {PathWorld world;struct {int valid;} player_contacts;} PathMission;
typedef struct {
    void (*after_tick)(void *);void *after_tick_context;
    int (*car_pull_query)(void *);void *car_pull_context;
    unsigned car_pull;
} Player;
typedef struct {
    int id,hferr,campaign_error,col,ms;Player pl;
    PathMission *traffic;unsigned char *traffic_data,*traffic_grip_data;
    Chars traffic_models;int traffic_models_attempted;
} Level;
enum {OK,MISSING,SHORT,BAD_BIND,BAD_STAGE,MISSING_GRIP,BAD_GRIP,NO_WORLD};
static int fault,model_error,reads,model_reads,detach_count,spawn_count,audio_binds;
static void callback(void *context){assert(context);++*(int *)context;}
static unsigned char *read_file(const char *path,int *length)
{
    int grip=strstr(path,"_grips")!=NULL;reads++;
    if(fault==MISSING || (grip && fault==MISSING_GRIP))return NULL;
    *length=!grip && fault==SHORT ? 16:64;
    unsigned char *p=malloc((unsigned)*length);assert(p);memset(p,0,(unsigned)*length);return p;
}
static void *world_calloc(size_t n,size_t size)
{return fault==NO_WORLD ? NULL:calloc(n,size);}
static int pathworld_bind(PathWorld *w,const void *data,int length,int *col)
{assert(data && length>=32 && col);w->stage=fault==BAD_STAGE ? 2:1;return fault==BAD_BIND ? -3:0;}
static int pathworld_bind_grips(PathWorld *w,const void *data,int length)
{assert(w);return !data || length<32 || fault==BAD_GRIP ? -4:0;}
static int pathworld_grip_query(void *world){assert(world);return 1;}
static void pathaudio_bind(PathWorld *w){assert(w);audio_binds++;}
static void pathmission_detach(PathMission *p){assert(p);detach_count++;}
static void chars_free(Chars *c){c->h=NULL;}
static int chars_load(Chars *c,const char *path)
{assert(strstr(path,"traffic1.jgc"));model_reads++;c->h=model_error ? NULL:c;return model_error;}
static void mission_path_released(int *mission,unsigned event,unsigned handle)
{assert(mission);(void)event;(void)handle;}
static unsigned pathworld_spawn(PathWorld *w,unsigned event,const unsigned args[7])
{assert(w && event==55 && args[0]==123);w->count++;spawn_count++;return 7;}
#define runtime_log(...) ((void)0)
#define calloc world_calloc
'''

CHECKS = r'''
#undef calloc
static void reset(void)
{fault=model_error=reads=model_reads=detach_count=spawn_count=audio_binds=0;}
static void test_failed_optional_load_preserves_world_tail(void)
{
    for(int fail=MISSING;fail<=NO_WORLD;fail++) {
        reset();int ticks=0;Level level={.id=1};
        level.pl.after_tick=callback;level.pl.after_tick_context=&ticks;fault=fail;
        assert(traffic_load(&level)!=0);
        assert(!level.traffic && !level.traffic_data && !level.traffic_grip_data);
        assert(level.pl.after_tick==callback && level.pl.after_tick_context==&ticks);
        assert(!level.campaign_error && !audio_binds && !level.traffic_models_attempted);
        level.pl.after_tick(level.pl.after_tick_context);assert(ticks==1);
        /* A later authored vehicle retains the normal lazy retry path. */
        fault=OK;unsigned args[7]={123};
        assert(traffic_spawn(&level,55,args)==7 && level.traffic && spawn_count==1);
        assert(level.traffic->world.count==1 && level.traffic->world.defer_bodies);
        assert(level.traffic->world.context==&level && level.traffic->world.released==traffic_released);
        assert(level.pl.car_pull_query==pathworld_grip_query && level.pl.car_pull_context==&level.traffic->world);
        assert(level.pl.after_tick==callback && level.pl.after_tick_context==&ticks && audio_binds==1);
        int previous=reads;assert(!traffic_load(&level) && reads==previous);
        traffic_free(&level);
        assert(!level.traffic && !level.pl.after_tick && !level.pl.after_tick_context);
    }
}
static void test_early_rejection_and_actual_spawn_error(void)
{
    reset();int ticks=0;Level level={.id=4};level.pl.after_tick=callback;level.pl.after_tick_context=&ticks;
    assert(traffic_load(&level)==-1 && !reads && level.pl.after_tick==callback);
    level.id=1;level.hferr=1;assert(traffic_load(&level)==-1 && !reads);
    level.hferr=0;fault=MISSING;unsigned args[7]={123};
    assert(!traffic_spawn(&level,55,args) && level.campaign_error==4 && !spawn_count);
    assert(level.pl.after_tick==callback && level.pl.after_tick_context==&ticks);
    traffic_free(&level);
    reset();level=(Level){.id=1};fault=NO_WORLD;
    assert(traffic_load(&level)==-1 && !level.pl.after_tick && !level.pl.after_tick_context);
}
static void test_model_preload_failure_and_teardown(void)
{
    reset();int ticks=0;Level level={.id=1};level.pl.after_tick=callback;level.pl.after_tick_context=&ticks;
    assert(!traffic_load(&level) && level.traffic->world.count==0);
    model_error=-2;
    assert(traffic_prepare_models(&level,1)==-2 && model_reads==1);
    assert(!level.traffic_models_attempted && !level.traffic_models.h && !level.traffic->world.error);
    assert(level.pl.after_tick==callback && level.pl.after_tick_context==&ticks);
    model_error=0;assert(!traffic_prepare_models(&level,1) && model_reads==2);
    assert(level.traffic_models_attempted && level.traffic_models.h);
    assert(!traffic_prepare_models(&level,1) && model_reads==2);
    traffic_release(&level,1);assert(level.traffic_models.h && level.traffic_models_attempted);
    assert(!level.traffic && !level.pl.after_tick && !level.pl.car_pull_query);
    traffic_free(&level);assert(!level.traffic_models.h && !level.traffic_models_attempted);
    assert(!traffic_load(&level));level.traffic->world.count=1;model_error=-2;
    assert(traffic_prepare_models(&level,0)==-2 && level.traffic_models_attempted && level.traffic->world.error==-8);
    int previous=model_reads;model_error=0;
    assert(traffic_prepare_models(&level,1)==-1 && model_reads==previous);
    assert(level.traffic_models_attempted && level.traffic->world.error==-8);traffic_free(&level);
}
int main(void)
{
    test_failed_optional_load_preserves_world_tail();
    test_early_rejection_and_actual_spawn_error();test_model_preload_failure_and_teardown();
    puts("traffic preload failure/retry/callback checks passed");return 0;
}
'''


class TrafficPreloadTests(unittest.TestCase):
    def test_actual_loaders_preserve_callbacks_and_retry(self):
        source = (ROOT / 'psp/src/main.c').read_text()
        functions = '\n'.join(function(source, name) for name in (
            'traffic_release', 'traffic_free', 'traffic_released',
            'traffic_load', 'traffic_spawn', 'traffic_prepare_models'))
        with tempfile.TemporaryDirectory(prefix='jgr-traffic-preload-') as tmp:
            code = Path(tmp) / 'traffic.c'; exe = Path(tmp) / 'traffic'
            code.write_text(PREAMBLE + functions + CHECKS)
            build = subprocess.run(['cc', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                                    str(code), '-o', str(exe)], check=False,
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            result = subprocess.run([str(exe)], check=False, capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('failure/retry/callback checks passed', result.stdout)


if __name__ == '__main__':
    unittest.main()
