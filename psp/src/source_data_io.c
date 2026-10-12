#include "source_data_io.h"
#include "portable_crc32.h"
static SourceDataOpen dependency;
static void *dependency_user;
static SourceDataView memory_view;
static void *memory_user;
int source_data_io_begin(SourceDataOpen open,void *user)
{
    if(!open || dependency || memory_view)return -1;
    dependency=open;dependency_user=user;return 0;
}
void source_data_io_end(void)
{dependency=NULL;dependency_user=NULL;}
FILE *source_data_io_open(const char *name)
{
    if(!name || !*name || memory_view)return NULL;
    return dependency?dependency(dependency_user,name):fopen(name,"rb");
}
int source_data_io_view_begin(SourceDataView view,void *user)
{
    if(!view || dependency || memory_view)return -1;
    memory_view=view;memory_user=user;return 0;
}
int source_data_io_view(const char *name,const void **data,size_t *bytes)
{
    if(!name || !*name || !data || !bytes)return -1;
    *data=NULL;*bytes=0;
    return memory_view ? memory_view(memory_user,name,data,bytes):0;
}
void source_data_io_view_end(void)
{memory_view=NULL;memory_user=NULL;}
int source_data_io_busy(void){return dependency!=NULL || memory_view!=NULL;}

unsigned source_data_io_checksum(const void *data,size_t bytes)
{return jgr_crc32_add(0xffffffffu,data,bytes)^0xffffffffu;}
