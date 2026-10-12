#include "source_data_startup.h"
#if JGR_PORTABLE_ASSETS
#include "source_data_bubble.h"
#include "source_data_numeric.h"
#include "source_data_pointers.h"
#include "source_data_io.h"
#include "portable_startup_cache.h"
#include <unistd.h>
static int load_tables(void)
{
    return source_data_bubble_load_file("spray_bubble.jsd")<0 ||
           source_data_numeric_load()<0 || source_data_pointers_load()<0 ? -1:0;
}
static int apply_cache(PortableStartupCache *cache)
{
    if(source_data_io_view_begin(portable_startup_cache_view,cache))return -1;
    int result=load_tables();source_data_io_view_end();return result;
}
int source_data_startup_load(void)
{
    /* Do not borrow, end, or replace a converter-owned dependency scope. */
    if(source_data_io_busy())return -1;
    char root[512];if(!getcwd(root,sizeof root))return load_tables();
    PortableStartupCache cache={0};
    if(!portable_startup_cache_read(&cache,root)){
        int result=apply_cache(&cache);portable_startup_cache_free(&cache);
        if(!result)return 0;
    }
    /* Missing, old or malformed cache: originals remain authoritative.
     * Validate all records before publishing the newly assembled cache. */
    if(!portable_startup_cache_collect(&cache,root)){
        int result=apply_cache(&cache);
        if(!result)(void)portable_startup_cache_write(&cache,root);
        portable_startup_cache_free(&cache);return result;
    }
    /* A cache allocation failure should retain the former lower-memory
     * per-file startup behavior. All borrowed storage is already released. */
    return load_tables();
}
#else
int source_data_startup_load(void){return 0;}
#endif
