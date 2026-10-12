#ifndef JGR_PORTABLE_NATIVE_TABLES_H
#define JGR_PORTABLE_NATIVE_TABLES_H
#include "portable_tables.h"
/* Setup-only initialization of the native replay's external table storage.
 * The caller must stop generation on failure. Individual table loaders check
 * every section before publishing it; a retry reloads the complete subset. */
int jgr_native_tables_load(JgrTableDependency dependency,void *user,
                           char *error,size_t error_size);
#endif
