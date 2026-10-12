#ifndef JGR_PORTABLE_NATIVE_CACHE_H
#define JGR_PORTABLE_NATIVE_CACHE_H
#include "portable_disc.h"
#include "portable_tables.h"
/* Generated collision scratch is reusable only within one live setup session.
 * No file from an earlier launch is accepted as a cached conversion. */
typedef struct JgrNativeCache JgrNativeCache;
struct Chars;
JgrNativeCache *jgr_native_cache_create(JgrDisc *,const char *scratch_directory);
int jgr_native_cache_collision(JgrNativeCache *,JgrDisc *,const char *scratch_directory,
                               unsigned stage,char *path,size_t path_bytes);
/* Only the immutable chars.jgc pose sections are shared. A borrowed descriptor
 * is a plain view, with no motion_owner indirection; mutable spray banks remain
 * private. Identity includes the live disc and verified dependency provider. */
int jgr_native_cache_bank_borrow(JgrNativeCache *,JgrDisc *,JgrTableDependency,void *,struct Chars *);
void jgr_native_cache_bank_adopt(JgrNativeCache *,JgrDisc *,JgrTableDependency,void *,struct Chars *);
int jgr_native_cache_bank_release(JgrNativeCache *,struct Chars *);
/* Drop idle pose storage under memory pressure. Active views are never evicted. */
void jgr_native_cache_trim(JgrNativeCache *,size_t available_bytes);
/* End the session after releasing private banks/aliases. Outstanding borrowed
 * views may only be released after this call; no further loads are permitted. */
void jgr_native_cache_destroy(JgrNativeCache *);
#endif
