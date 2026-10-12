#ifndef JGR_SOURCE_DATA_IO_H
#define JGR_SOURCE_DATA_IO_H
#include <stdio.h>
typedef FILE *(*SourceDataOpen)(void *user,const char *name);
typedef int (*SourceDataView)(void *user,const char *name,const void **data,size_t *bytes);
/* Startup-only, single-threaded scope. Setup resolves completed, verified
 * dependencies before the installed asset manifest exists. A failed custom
 * lookup never falls back to an unverified file in the current directory. */
int source_data_io_begin(SourceDataOpen open,void *user);
void source_data_io_end(void);
FILE *source_data_io_open(const char *name);
/* Startup cache scope is exclusive with the converter's dependency scope.
 * A view is borrowed until scope end: 1=found, -1=missing/error, 0=no scope.
 * Callers must still run the exact original format/CRC/layout validation. */
int source_data_io_view_begin(SourceDataView view,void *user);
int source_data_io_view(const char *name,const void **data,size_t *bytes);
void source_data_io_view_end(void);
int source_data_io_busy(void);
/* Share one fast CRC table across all three startup format validators. */
unsigned source_data_io_checksum(const void *data,size_t bytes);
#endif
