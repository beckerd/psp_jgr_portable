#ifndef JGR_PORTABLE_TABLES_H
#define JGR_PORTABLE_TABLES_H
#include "portable_disc.h"
#include <stdio.h>
/* Setup-only numeric table conversion. Recipes contain source coordinates and
 * the portable output ABI, never original initializer bytes. Returns zero on
 * success. The caller verifies the resulting file against the asset catalog. */
typedef struct {uint32_t identity,offset,size,alignment;} JgrTableSection;
typedef FILE *(*JgrTableDependency)(void *user,const char *name);
/* The dependency callback returns a new read-only, catalog-verified stream.
 * It remains owned by this conversion until closed. No disc/cache is needed
 * after setup completes. */
typedef int (*JgrTableFill)(JgrDisc *,const char *,const JgrTableSection *,unsigned,
                          unsigned char *,unsigned,void *);
int jgr_convert_table_fill(JgrDisc *,const char *,FILE *,JgrTableFill,void *);
int jgr_convert_table_deferred(const char *output_name);
int jgr_convert_table_cached(JgrDisc *,const char *,const char *scratch_directory,
                             JgrTableDependency,void *,FILE *);
/* Optional one-installation RAM cache. Captures tiny derived tables while
 * their original city intermediate exists. Begin discards earlier entries;
 * end must run before closing the disc or destroying dependency context.
 * No disk cache is trusted and uncaptured/repair jobs use exact regeneration. */
void jgr_table_city_cache_begin(JgrDisc *,const char *scratch_directory,
                               JgrTableDependency,void *);
void jgr_table_city_cache_end(void);
int jgr_convert_table_known(const char *output_name);
int jgr_convert_table(JgrDisc *disc,const char *output_name,FILE *output);
#endif
