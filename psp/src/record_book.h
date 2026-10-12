#ifndef JGR_RECORD_BOOK_H
#define JGR_RECORD_BOOK_H
#include <string.h>
typedef struct {
    int phase,cursor,child,sound;float frame,fade,fade_step,remainder;
    unsigned edges;
} RecordBook;
typedef struct {
    int phase,cursor,sound;float frame,scroll,target,speed;
} RecordBrowser;
typedef struct {int phase,cursor,sound_mode,rumble,sound;float frame;} RecordOptions;
void record_options_init(RecordOptions *,const unsigned char *save);
int record_options_step(RecordOptions *,unsigned buttons,int rumble_available,unsigned char *save);
void record_options_pose_step(RecordOptions *);
void record_options_close(const RecordOptions *,unsigned char *save);
void record_book_init(RecordBook *);
/* Original digital edges: A4, B2, Start8, Up16, Down32.
   Child route: 1 save, 2 records, 3 options, 4 garage; zero waits. */
int record_book_step(RecordBook *,unsigned buttons,int child_done);
void record_browser_init(RecordBrowser *);
int record_browser_step(RecordBrowser *,unsigned buttons,int expanded);
/* Animation completion belongs to draw, not input, in both original tasks. */
void record_book_pose_step(RecordBook *);
void record_browser_pose_step(RecordBrowser *);
int record_browser_rows(int cursor);
unsigned record_browser_offset(int cursor,int row);
int record_browser_title(int cursor,int row,const unsigned char *save);
int record_browser_value(int kind,unsigned score,char *out,unsigned capacity);
#ifndef JGR_GARAGE_HOST_TEST
#include "charanim.h"
void record_book_draw(RecordBook *,Chars *);
void record_browser_draw(RecordBrowser *,Chars *,const unsigned char *save);
void record_options_draw(RecordOptions *,Chars *,int rumble_available);
#endif
#endif
