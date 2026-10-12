/* Music sequencer data transcribed from 1ST_READ.BIN (2026-09-02): ADX name table 0x8c1d5bc4 (211 names) and the song table
   0x8c1dab74 (190 entries {node*, kind, 0}; node = {adx index, next*}; a chain either ends or loops back to node `loop`).
   Selector bgm_pick = FUN_8c02d61c (stage BGM start, called from FUN_8c02e204); FUN_8c02d3ec(n) plays entry n. */
#include "bgm.h"

#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_mixed_bgm_c__bgm_name_0.inc"
#else

#include "generated/source_data/bgm_c__BGM_NAME_0.inc"

#endif


#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_bgm_c__bgm_table_0.inc"
#else

#include "generated/source_data/bgm_c__BGM_TABLE_0.inc"

#endif

const int BGM_NENTRIES = 190;

/* FUN_8c02d61c: mode = JMDc +0x18 (FUN_8c041fdc), stage/slot = game-mode task 6 +0x7c/+0x80, prev = stream task +0x84 (0 on a stage/slot change,
   then 1/2 = RNG low-bit variants, 3/4 = failed/abandoned variants), rnd = FUN_8c013e94(18) (0..17), cur/w80 = wrapper task 0x297 +0x74 (current entry) / +0x78. Returns the entry (-1 = keep). */
int bgm_pick(int mode, int stage, int slot, int prev, int rnd, int cur, int w80)
{
  int song = 5; (void)w80; (void)cur;
  switch(mode) {
  case 0:
    switch(stage) {
    case 0:
      if (9 < slot) {
        return 5;
      }
      switch(slot) {
      case 0:
        if (prev != 1) {
          if ((prev == 2) || (prev == 3)) {
            song = 6;
            break;
          }
          if (prev != 4) break;
        }
        song = 5;
        break;
      case 1:
        if (prev != 1) {
          if ((prev == 2) || (prev == 3)) {
            song = 8;
            break;
          }
          if (prev != 4) break;
        }
        song = 7;
        break;
      case 2:
        if (prev != 1) {
          if ((prev == 2) || (prev == 3)) {
            song = 10;
            break;
          }
          if (prev != 4) break;
        }
        song = 9;
        break;
      case 3:
        if (prev != 1) {
          if ((prev == 2) || (prev == 3)) {
            song = 0xc;
            break;
          }
          if (prev != 4) break;
        }
        song = 0xb;
        break;
      case 4:
        if (0x11 < rnd) {
          return 0xaa;
        }
        switch(rnd) {
        case 0:
          song = 0xaa;
          break;
        case 1:
          song = 0xab;
          break;
        case 2:
          song = 0xac;
          break;
        case 3:
          song = 0xad;
          break;
        case 4:
          song = 0xae;
          break;
        case 5:
          song = 0xaf;
          break;
        case 6:
          song = 0xb0;
          break;
        case 7:
          song = 0xb1;
          break;
        case 8:
          song = 0xb2;
          break;
        case 9:
          song = 0xb3;
          break;
        case 10:
          song = 0xb4;
          break;
        case 0xb:
          song = 0xb5;
          break;
        case 0xc:
          song = 0xb6;
          break;
        case 0xd:
          song = 0xb7;
          break;
        case 0xe:
          song = 0xb8;
          break;
        case 0xf:
          song = 0xb8;
          break;
        case 0x10:
          song = 0xba;
          break;
        case 0x11:
          song = 0xbb;
          break;
        default:
          return 0xaa;
        }
        break;
      case 5:
      case 6:
        song = 5;
        break;
      case 7:
      case 8:
      case 9:
        switch(rnd) {
        case 0:
          song = 0x20;
          break;
        case 1:
          song = 0x21;
          break;
        case 2:
          song = 0x22;
          break;
        case 3:
          song = 0x23;
          break;
        case 4:
          song = 0x24;
          break;
        case 5:
          song = 0x25;
          break;
        case 6:
          song = 0x26;
          break;
        case 7:
          song = 0x27;
          break;
        case 8:
          song = 0x28;
          break;
        case 9:
          song = 0x29;
          break;
        case 10:
          song = 0x2a;
          break;
        case 0xb:
          song = 0x2b;
          break;
        case 0xc:
          song = 0x2c;
          break;
        case 0xd:
          song = 0x2d;
          break;
        case 0xe:
          song = 0x2e;
          break;
        case 0xf:
          song = 0xa3;
          break;
        case 0x10:
          song = 0xa4;
          break;
        case 0x11:
          song = 0xa5;
          break;
        default:
          song = 0x20;
        }
        break;
      default:
        return 5;
      }
      break;
    case 1:
      if (slot < 10) {
        switch(slot) {
        case 0:
          if (prev != 1) {
            if ((prev == 2) || (prev == 3)) {
              song = 0xe;
              return song;
            }
            if (prev != 4) return song;
          }
          song = 0xd;
          return song;
        case 1:
          if (prev != 1) {
            if ((prev == 2) || (prev == 3)) {
              song = 0x10;
              return song;
            }
            if (prev != 4) return song;
          }
          song = 0xf;
          return song;
        case 2:
          if (prev != 1) {
            if ((prev == 2) || (prev == 3)) {
              song = 0x12;
              return song;
            }
            if (prev != 4) return song;
          }
          song = 0x11;
          return song;
        case 3:
          if (prev != 1) {
            if ((prev == 2) || (prev == 3)) {
              song = 0x14;
              return song;
            }
            if (prev != 4) return song;
          }
          song = 0x13;
          return song;
        case 4:
          if (cur == 0x1d) {
            return -1;
          }
          if (cur == 0x1e) {
            return -1;
          }
          if (prev != 1) {
            if ((prev == 2) || (prev == 3)) {
              song = 0x1e;
              return song;
            }
            if (prev != 4) return song;
          }
          song = 0x1d;
          return song;
        case 5:
        case 6:
          break;
        case 7:
        case 8:
        case 9:
          switch(rnd) {
          case 0:
            song = 0x20;
            break;
          case 1:
            song = 0x21;
            break;
          case 2:
            song = 0x22;
            break;
          case 3:
            song = 0x23;
            break;
          case 4:
            song = 0x24;
            break;
          case 5:
            song = 0x25;
            break;
          case 6:
            song = 0x26;
            break;
          case 7:
            song = 0x27;
            break;
          case 8:
            song = 0x28;
            break;
          case 9:
            song = 0x29;
            break;
          case 10:
            song = 0x2a;
            break;
          case 0xb:
            song = 0x2b;
            break;
          case 0xc:
            song = 0x2c;
            break;
          case 0xd:
            song = 0x2d;
            break;
          case 0xe:
            song = 0x2e;
            break;
          case 0xf:
            song = 0xa3;
            break;
          case 0x10:
            song = 0xa4;
            break;
          case 0x11:
            song = 0xa5;
            break;
          default:
            song = 0x20;
          }
          return song;
        default:
          return 5;      /* SUB_8c02dca8: play entry 5 */
        }
      }
      song = 5;
      break;
    case 2:
      if (slot < 10) {
        switch(slot) {
        case 0:
          if (prev != 1) {
            if ((prev == 2) || (prev == 3)) {
              song = 0x16;
              return song;
            }
            if (prev != 4) return song;
          }
          song = 0x15;
          return song;
        case 1:
          if (prev != 1) {
            if ((prev == 2) || (prev == 3)) {
              song = 0x18;
              return song;
            }
            if (prev != 4) return song;
          }
          song = 0x17;
          return song;
        case 2:
          if (prev != 1) {
            if ((prev == 2) || (prev == 3)) {
              song = 0x1a;
              return song;
            }
            if (prev != 4) return song;
          }
          song = 0x19;
          return song;
        case 3:
          if (prev != 1) {
            if ((prev == 2) || (prev == 3)) {
              song = 0x1c;
              return song;
            }
            if (prev != 4) return song;
          }
          song = 0x1b;
          return song;
        case 4:
          if (cur == 0x1d) {
            return -1;
          }
          if (cur == 0x1e) {
            return -1;
          }
          if (prev != 1) {
            if ((prev == 2) || (prev == 3)) {
              song = 0x1e;
              return song;
            }
            if (prev != 4) return song;
          }
          song = 0x1d;
          return song;
        case 5:
        case 6:
          break;
        case 7:
        case 8:
        case 9:
          switch(rnd) {
          case 0:
            song = 0x20;
            break;
          case 1:
            song = 0x21;
            break;
          case 2:
            song = 0x22;
            break;
          case 3:
            song = 0x23;
            break;
          case 4:
            song = 0x24;
            break;
          case 5:
            song = 0x25;
            break;
          case 6:
            song = 0x26;
            break;
          case 7:
            song = 0x27;
            break;
          case 8:
            song = 0x28;
            break;
          case 9:
            song = 0x29;
            break;
          case 10:
            song = 0x2a;
            break;
          case 0xb:
            song = 0x2b;
            break;
          case 0xc:
            song = 0x2c;
            break;
          case 0xd:
            song = 0x2d;
            break;
          case 0xe:
            song = 0x2e;
            break;
          case 0xf:
            song = 0xa3;
            break;
          case 0x10:
            song = 0xa4;
            break;
          case 0x11:
            song = 0xa5;
            break;
          default:
            song = 0x20;
          }
          return song;
        default:
          return 0x15;   /* SUB_8c02de80: play entry 0x15 */
        }
      }
      song = 0x15;
      break;
    case 3:
      song = 0x1f;
      break;
    case 4:
      /* 02de9c / 02dfac: slot and sequencer state are separate fields. */
      if (slot == 0) {
        if (prev != 1) {
          if ((prev == 2) || (prev == 3)) {
            song = 0x79;
            break;
          }
          if (prev != 4) break;
        }
        song = 0x78;
      }
      else {
        if (((slot != 1) && (slot != 2)) && (slot != 3)) {
          return 0x78;
        }
        switch(rnd) {
        case 0:
          song = 0x20;
          break;
        case 1:
          song = 0x21;
          break;
        case 2:
          song = 0x22;
          break;
        case 3:
          song = 0x23;
          break;
        case 4:
          song = 0x24;
          break;
        case 5:
          song = 0x25;
          break;
        case 6:
          song = 0x26;
          break;
        case 7:
          song = 0x27;
          break;
        case 8:
          song = 0x28;
          break;
        case 9:
          song = 0x29;
          break;
        case 10:
          song = 0x2a;
          break;
        case 0xb:
          song = 0x2b;
          break;
        case 0xc:
          song = 0x2c;
          break;
        case 0xd:
          song = 0x2d;
          break;
        case 0xe:
          song = 0x2e;
          break;
        case 0xf:
          song = 0xa3;
          break;
        case 0x10:
          song = 0xa4;
          break;
        case 0x11:
          song = 0xa5;
          break;
        default:
          song = 0x20;
        }
      }
      break;
    case 5:
      /* 02de9c / 02dfac: slot and sequencer state are separate fields. */
      if (slot == 0) {
        if (prev != 1) {
          if ((prev == 2) || (prev == 3)) {
            song = 0x7b;
            break;
          }
          if (prev != 4) break;
        }
        song = 0x7a;
      }
      else {
        if (((slot != 1) && (slot != 2)) && (slot != 3)) {
          return 0x7a;
        }
        switch(rnd) {
        case 0:
          song = 0x20;
          break;
        case 1:
          song = 0x21;
          break;
        case 2:
          song = 0x22;
          break;
        case 3:
          song = 0x23;
          break;
        case 4:
          song = 0x24;
          break;
        case 5:
          song = 0x25;
          break;
        case 6:
          song = 0x26;
          break;
        case 7:
          song = 0x27;
          break;
        case 8:
          song = 0x28;
          break;
        case 9:
          song = 0x29;
          break;
        case 10:
          song = 0x2a;
          break;
        case 0xb:
          song = 0x2b;
          break;
        case 0xc:
          song = 0x2c;
          break;
        case 0xd:
          song = 0x2d;
          break;
        case 0xe:
          song = 0x2e;
          break;
        case 0xf:
          song = 0xa3;
          break;
        case 0x10:
          song = 0xa4;
          break;
        case 0x11:
          song = 0xa5;
          break;
        default:
          song = 0x20;
        }
      }
    }
    break;
  case 1:
    song = 0x32;
    break;
  case 2:
    if (w80 != 0x19) {
      return -1;
    }
    song = 0x33;
    break;
  case 3:
    if (w80 != 0x19) {
      return -1;
    }
    song = 0x34;
    break;
  case 4:
    song = 0x47;
    break;
  case 5:
    song = 0x35;
    break;
  case 6:
    if (w80 != 0x19) {
      return -1;
    }
    song = 0x36;
    break;
  case 7:
    if (w80 != 0x19) {
      return -1;
    }
    song = 0x37;
    break;
  case 8:
    song = 0x44;
    break;
  case 9:
    if (w80 != 0x19) {
      return -1;
    }
    song = 0x45;
    break;
  case 10:
    if (w80 != 0x19) {
      return -1;
    }
    song = 0x46;
    break;
  case 0xb:
    song = 0x3b;
    break;
  case 0xc:
    song = 0x41;
    break;
  case 0xd:
    if (w80 != 0x19) {
      return -1;
    }
    song = 0x42;
    break;
  case 0xe:
    if (w80 != 0x19) {
      return -1;
    }
    song = 0x43;
    break;
  case 0xf:
    song = 0x38;
    break;
  case 0x10:
    song = 0x3e;
    break;
  case 0x11:
    if (w80 != 0x19) {
      return -1;
    }
    song = 0x3f;
    break;
  case 0x12:
    if (w80 != 0x19) {
      return -1;
    }
    song = 0x40;
    break;
  case 0x13:
    song = 0x2f;
    break;
  case 0x14:
    if (w80 != 0x19) {
      return -1;
    }
    song = 0x30;
    break;
  case 0x15:
    if (w80 != 0x19) {
      return -1;
    }
    song = 0x31;
    break;
  case 0x16:
    song = 0x7c;
    break;
  case 0x17:
    if (w80 != 0x19) {
      return -1;
    }
    song = 0x7d;
    break;
  case 0x18:
    if (w80 != 0x19) {
      return -1;
    }
    song = 0x7e;
    break;
  case 0x19:
    if (w80 != 0x19) {
      return -1;
    }
    song = 0x48;
    break;
  case 0x1a:
    if (w80 != 0x19) {
      return -1;
    }
    song = 0x49;
    break;
  case 0x1b:
    song = 0x7f;
    break;
  case 0x1c:
    song = 0x80;
    break;
  case 0x1d:
    song = 0x81;
    if (w80 != 0x19) {
      return -1;
    }
    break;
  default:
    return 5;
  }
return song;
}

/* FUN_8c02e290 / 02e71c. Verified against original SH-4 instructions,
   including the licensed-song branches at 02e648/654/660. */
int bgm_clear_pick(int mode,int cur,int category)
{
    static const unsigned char by_mode[29]={95,96,97,116,98,99,100,113,114,115,
        106,110,111,112,103,107,108,109,92,93,94,130,131,132,117,118,133,134,135};
    int song=-1;
    if(mode==0) {
        if(category==2 || category==3) {
            if(cur==31)return -1;
            if(cur>=120 && cur<=123)song=189;
            else switch(cur) {
                case 5:case 6:case 9:case 10:case 13:case 14:
                case 17:case 18:case 23:case 24:case 27:case 28:song=76;break;
                default:song=75;break;
            }
        } else if(category==4) {
            if(cur>=32 && cur<=46)song=77+cur-32;
            else if(cur>=170 && cur<=184)song=77+cur-170;
            else if(cur>=163 && cur<=165)song=166+cur-163;
            else if(cur>=185 && cur<=187)song=166+cur-185;
        }
    } else if(mode>=1 && mode<=29)song=by_mode[mode-1];
    return cur!=song ? song:-1;
}

#ifndef BGM_HOST
/* runtime: the stream task 0x298 (FUN_8c0e81b0) walks the entry's chain; kind 0 = chain (mode 2 streaming, next file at the end),
   1 = whole list once, 2/4 = single ADX (embedded loops honored); 3/5 use a memory-backed loader and are absent from the US table. FUN_8c02d3ec(n) restarts the stream. */
#include <stdio.h>
#include <stdlib.h>
#include "stream.h"
#include "audio.h"
#include "audio_mix.h"
static int cur_entry = -1, cur_node = -1, prev_state = 0, last_stage = -1, last_slot = -1; static char bgm_path[64];
static const char *bgm_next_file(void)
{
    if (cur_entry < 0) return 0;
    const BgmEntry *b = &BGM_TABLE[cur_entry]; int n = cur_node + 1;
    if (n >= b->n) { if (b->kind == 3 || b->kind == 5) n = cur_node; else if (b->loop < 0 || b->kind == 2 || b->kind == 4) return 0; else n = b->loop; }
    cur_node = n; snprintf(bgm_path, sizeof bgm_path, "adx/%s.ADX", BGM_NAME[b->seq[n]]); return bgm_path;
}
void bgm_play(int entry)
{
    if (entry < 0 || entry >= BGM_NENTRIES) return;
    stream_begin_change(); /* old decoder cannot advance the new sequence */
    audio_music_entry(entry);
    cur_entry = entry; cur_node = 0; snprintf(bgm_path, sizeof bgm_path, "adx/%s.ADX", BGM_NAME[BGM_TABLE[entry].seq[0]]);
    printf("bgm: entry %d -> %s\n", entry, bgm_path);
    stream_set_chain(BGM_TABLE[entry].kind==2 || BGM_TABLE[entry].kind==4 ? STREAM_FILE:STREAM_CHAIN, bgm_next_file);
    stream_set_file(bgm_path);
}
int bgm_current(void) { return cur_entry; }
/* FUN_8c02e204 -> FUN_8c02d61c: stage BGM at mission start */
void bgm_stage(int mode, int stage, int slot)
{
    if (stage != last_stage || slot != last_slot) prev_state = 0;
    last_stage = stage; last_slot = slot;
    /* 0b22bc -> RNG, then low bit: unrelated to stream_active(). */
    int choice=rand()%30;
    if (prev_state == 0) prev_state = 1+(choice&1);
    /* Wrapper +78 is the CURRENT stream category (02eabc), including
       category 25 after OVR_SHRT. Recruitment retry selectors use it. */
    int e = bgm_pick(mode, stage, slot, prev_state, rand() % 18, cur_entry, source_audio_bgm_class(cur_entry));
    if (e >= 0) bgm_play(e);
}
/* FUN_8c02e290: clear jingle + resume; FUN_8c02e738: failure jingle OVR_SHRT (entry 0x77), stream state 3 then 4 */
void bgm_clear(int mode) { prev_state=0; int e = bgm_clear_pick(mode, cur_entry, source_audio_bgm_class(cur_entry)); if (e >= 0) bgm_play(e); }
void bgm_fail(void)
{
    if(cur_entry != 0x77)bgm_play(0x77);
    prev_state = prev_state == 1 ? 3 : prev_state == 2 ? 4 : 0;
}
void bgm_leave(void)
{
    if(source_audio_bgm_class(cur_entry)!=25)
        prev_state = prev_state == 1 ? 3 : prev_state == 2 ? 4 : 0;
}
#endif
