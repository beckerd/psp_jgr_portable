#ifndef JGR_PORTABLE_MODELS_PACK_H
#define JGR_PORTABLE_MODELS_PACK_H
#include <stdio.h>
#include <stddef.h>
/* Repack the port's 68-model cast, sharing near/far poses and exact key bytes.
 * Scratch streams may be rewound/overwritten; input must be a separate stream. */
int jgr_models_pack_cast(FILE *input,FILE *output,FILE *scratch[10]);
/* Optional immutable view of the complete input avoids a second RAM copy. */
int jgr_models_pack_cast_view(FILE *input,FILE *output,FILE *scratch[10],
                             const void *input_data,size_t input_bytes);
#endif
