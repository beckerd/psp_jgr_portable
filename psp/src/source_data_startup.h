#ifndef JGR_SOURCE_DATA_STARTUP_H
#define JGR_SOURCE_DATA_STARTUP_H
/* Same source-data validation and pointer fixups as the individual loaders.
 * A valid local cache uses one sequential file read. When it is missing,
 * the loaders build it from the prepared assets, without disc extraction.
 * Cache write failure does not prevent an otherwise validated game boot. */
int source_data_startup_load(void);
#endif
