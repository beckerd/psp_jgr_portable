#ifndef JGR_GAME_CONTROLS_H
#define JGR_GAME_CONTROLS_H
#include <pspctrl.h>
/* Dreamcast tutorial: left trigger sprays; right trigger boosts. */
#define GAME_SPRAY PSP_CTRL_LTRIGGER
#define GAME_BOOST PSP_CTRL_RTRIGGER
static inline unsigned game_recorded_buttons(unsigned buttons)
{
    return (buttons&1 ? PSP_CTRL_CROSS:0) |
           (buttons&4 ? GAME_SPRAY:0) | (buttons&8 ? GAME_BOOST:0);
}
#endif
