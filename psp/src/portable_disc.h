#ifndef JGR_PORTABLE_DISC_H
#define JGR_PORTABLE_DISC_H

#include <stddef.h>
#include <stdint.h>

/* Bounded access to the supported original GDI. No disc contents are retained
 * after close. A handle owns eight pages of eight physical sectors each
 * (147 KiB for a raw track), so interleaved geometry reads remain cached.
 * Immutable archive indexes are validated once while resident (16 KiB total
 * allocation cap, 16 slots); a small successful-path cache avoids ISO rescans.
 * Oversized/allocation-failed indexes retain complete streaming validation. */
typedef struct JgrDisc JgrDisc;
typedef struct {
    uint64_t offset; /* cooked bytes from the beginning of the data track */
    uint32_t size;
} JgrDiscFile;
/* A caller-owned view of bytes already read/validated from this handle.
 * Initialize to zero; the caller keeps the immutable buffer alive until end.
 * Views nest in LIFO order and match exact source extents, never other discs.
 * Clones do not inherit views. Closing a handle invalidates its live scopes. */
typedef struct JgrDiscView {
    JgrDisc *disc;JgrDiscFile file;const unsigned char *data;
    struct JgrDiscView *previous;
} JgrDiscView;
int jgr_disc_view_begin(JgrDisc *,JgrDiscView *,const JgrDiscFile *,const void *,size_t);
int jgr_disc_view_end(JgrDiscView *);

/* Open validates every ISO directory/extent and the supported executable's
 * SHA-256 and boot revision before returning a usable handle. */
int jgr_disc_open(JgrDisc **out, const char *gdi, char *error, size_t error_size);
/* Setup worker clone: reopens the same validated track with an independent
 * cursor/cache. It checks opened-file identity/size/timestamps, without
 * repeating ISO/executable verification. Source media must remain immutable. */
int jgr_disc_clone(JgrDisc **out,const JgrDisc *source,char *error,size_t error_size);
void jgr_disc_close(JgrDisc *disc);
const char *jgr_disc_error(const JgrDisc *disc);
int jgr_disc_find(JgrDisc *disc, const char *iso_path, JgrDiscFile *out);
int jgr_disc_afs(JgrDisc *disc, const JgrDiscFile *archive,
                 unsigned index, JgrDiscFile *out);
/* As above, with the original bounded archive name (32 bytes plus NUL).
 * An archive without a name table returns an empty name. */
int jgr_disc_afs_named(JgrDisc *disc, const JgrDiscFile *archive,
                       unsigned index, JgrDiscFile *out, char original[33]);
/* Accepts the source names used by raw_assets.json:
 * decompile/extracted/PATH or decompile/afs/ARCHIVE/NNN_original-name. */
int jgr_disc_source(JgrDisc *disc, const char *source, JgrDiscFile *out);
int jgr_disc_read(JgrDisc *disc, const JgrDiscFile *file, uint32_t offset,
                  void *destination, size_t size);
/* Sequential COPY jobs can borrow one larger buffer, then hash/write its
 * first `size` bytes. Up to 1 MiB of cooked payload is read in one physical
 * operation. Raw MODE1 headers are all checked before payloads are compacted
 * in place. The extra sector covers an arbitrary first-byte alignment.
 * No memory is allocated here; capacity must cover `size`. Insufficient raw
 * workspace or larger requests use the ordinary cached reader instead.
 * The buffer may be changed on error and must never be published then. */
#define JGR_DISC_SEQUENTIAL_BYTES (1024u * 1024u)
#define JGR_DISC_SEQUENTIAL_CAPACITY ((JGR_DISC_SEQUENTIAL_BYTES / 2048u + 1u) * 2352u)
int jgr_disc_read_sequential(JgrDisc *disc, const JgrDiscFile *file, uint32_t offset,
                             void *buffer, size_t size, size_t capacity);
/* Installer replay BIOS access. LBAs are absolute GDI sector numbers (convert
 * a BIOS FAD by subtracting 150 first). Returns cooked 2048-byte payloads,
 * including the selected data track's boot/IP.BIN sectors. No audio, raw
 * sector headers or subchannels are exposed. Each request is at most 32
 * sectors / 64 KiB, and destination_capacity must cover the complete request. */
int jgr_disc_sector_bounds(const JgrDisc *disc, uint32_t *first_lba, uint32_t *count);
int jgr_disc_sectors(JgrDisc *disc, uint32_t lba, unsigned count,
                     void *destination, size_t destination_capacity);
typedef int (*JgrDiscVisit)(const char *path, const JgrDiscFile *file, void *user);
/* Callback returns zero to continue; nonzero aborts with an error. */
int jgr_disc_visit(JgrDisc *disc, JgrDiscVisit visit, void *user);

/* Shared streaming SHA-256. final consumes the context; output is 32 bytes.
 * hex requires 65 bytes and appends a NUL. No dynamic memory is used. */
typedef struct {
    uint32_t state[8];
    uint64_t bytes;
    unsigned used;
    unsigned char block[64];
} JgrSha256;
void jgr_sha256_init(JgrSha256 *context);
void jgr_sha256_update(JgrSha256 *context, const void *data, size_t size);
void jgr_sha256_final(JgrSha256 *context, unsigned char digest[32]);
void jgr_sha256_hex(const unsigned char digest[32], char hex[65]);

#ifdef JGR_DISC_TESTING
/* Synthetic fixtures cannot have the supported executable fingerprint. This
 * entry point is absent from production builds; all structural checks remain. */
int jgr_disc_open_fixture(JgrDisc **out, const char *gdi, char *error, size_t error_size);
/* Structural/work counters for synthetic cache tests; absent in production. */
typedef struct {
    uint32_t afs_validations, afs_rows, afs_hits, find_hits;
    uint32_t afs_cache_bytes, afs_cache_limit, afs_cache_entries, find_cache_entries;
} JgrDiscCacheStats;
void jgr_disc_cache_stats(const JgrDisc *disc, JgrDiscCacheStats *stats);
typedef struct {
    uint32_t physical_reads, sequential_reads;
    uint64_t physical_bytes;
} JgrDiscReadStats;
void jgr_disc_read_stats(const JgrDisc *disc, JgrDiscReadStats *stats);
typedef struct {uint64_t reads,bytes;} JgrDiscViewStats;
void jgr_disc_view_stats(const JgrDisc *disc,JgrDiscViewStats *stats);
#endif
#endif
