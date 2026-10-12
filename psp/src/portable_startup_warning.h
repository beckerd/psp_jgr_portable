#ifndef JGR_PORTABLE_STARTUP_WARNING_H
#define JGR_PORTABLE_STARTUP_WARNING_H
#include "portable_disc.h"

/* Optional local artwork generated from the user's disc, never a distributed
 * payload or a prerequisite for loading a complete game installation. The
 * original 4:3 canvas fits at 360x270, centered on a black 480x272 frame. */
#define JGR_STARTUP_WARNING_FILE "data/setup/startup-warning.jbg"
enum { JGR_STARTUP_WARNING_WIDTH=480, JGR_STARTUP_WARNING_HEIGHT=272,
       JGR_STARTUP_WARNING_PIXELS=480*272*2, JGR_STARTUP_WARNING_HEADER=24 };
/* Returns one for a structurally valid, checksum-checked image, zero otherwise.
 * Does not consult the disc, installation manifest, or completion receipt. */
int portable_startup_warning_valid(const char *root);
/* Returns zero if already valid or successfully prepared, minus one otherwise.
 * A valid existing image needs no disc. Callers must treat failure as optional;
 * ordinary game launch must never open a disc to call this function. */
int portable_startup_warning_prepare(JgrDisc *disc,const char *root);
#endif
