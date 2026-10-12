/* Disc-derived selectors are installed separately from this native lookup. */
#if JGR_PORTABLE_ASSETS
#include "source_data_scene_dialogue_data.inc"
#else
#include "generated/source_data/scene_dialogue_data.inc"
#endif
static inline int scene_dialogue_mode(int id)
{
    return (unsigned)id<sizeof scene_dialogue_modes ? scene_dialogue_modes[id]:0;
}
