#ifndef JGR_PORTABLE_NATIVE_OBJECTS_H
#define JGR_PORTABLE_NATIVE_OBJECTS_H
#include "portable_tables.h"
#include "player.h"
#include "collide.h"
typedef struct JgrNativeParked JgrNativeParked;
/* Setup-only intact parked scenery for source-driven replay generation.
 * Stage 1 is currently supported. Parameters/placement come directly from
 * the verified user disc; geometry comes from a verified dependency stream.
 * Returns 1 on success, 0 on failure. Load before any collision query. */
int jgr_native_parked_load(JgrNativeParked **owner,Collision *,JgrDisc *,
    JgrTableDependency,void *dependency_user,unsigned stage,unsigned set,unsigned subset);
/* bind installs before_tick and surface_contact. A world with other callbacks
 * composes its before_tick with tick(), retaining the installed contact hook. */
void jgr_native_parked_bind(JgrNativeParked *,Player *);
int jgr_native_parked_tick(JgrNativeParked *);
void jgr_native_parked_free(JgrNativeParked **);
#endif
