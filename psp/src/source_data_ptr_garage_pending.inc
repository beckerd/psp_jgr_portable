/* Payload-free external storage and reviewed dispatch code. */
#ifndef JGR_SOURCE_DATA_PTR_GARAGE_PENDING_INC
#define JGR_SOURCE_DATA_PTR_GARAGE_PENDING_INC
/* Generated from original US garage data by garage_pending_assets.py. */
extern signed char PENDING_MODEL[15] __asm__("jgr_ptr_garage_pending_PENDING_MODEL");
typedef char jgr_ptr_garage_pending_PENDING_MODEL_abi[(sizeof(PENDING_MODEL)==15u)?1:-1];
extern unsigned PENDING_FRAMES[15] __asm__("jgr_ptr_garage_pending_PENDING_FRAMES");
typedef char jgr_ptr_garage_pending_PENDING_FRAMES_abi[(sizeof(PENDING_FRAMES)==60u)?1:-1];
extern float PENDING_POSITION[3] __asm__("jgr_ptr_garage_pending_PENDING_POSITION");
typedef char jgr_ptr_garage_pending_PENDING_POSITION_abi[(sizeof(PENDING_POSITION)==12u)?1:-1];
extern float PENDING_CAMERA[244][6] __asm__("jgr_ptr_garage_pending_PENDING_CAMERA");
typedef char jgr_ptr_garage_pending_PENDING_CAMERA_abi[(sizeof(PENDING_CAMERA)==5856u)?1:-1];
extern int PENDING_ROLL[244] __asm__("jgr_ptr_garage_pending_PENDING_ROLL");
typedef char jgr_ptr_garage_pending_PENDING_ROLL_abi[(sizeof(PENDING_ROLL)==976u)?1:-1];
extern int PENDING_FOV[244] __asm__("jgr_ptr_garage_pending_PENDING_FOV");
typedef char jgr_ptr_garage_pending_PENDING_FOV_abi[(sizeof(PENDING_FOV)==976u)?1:-1];
extern const char * PENDING_TEXT[13] __asm__("jgr_ptr_garage_pending_PENDING_TEXT");
typedef char jgr_ptr_garage_pending_PENDING_TEXT_abi[(sizeof(PENDING_TEXT)==52u)?1:-1];
extern GarageQuad PENDING_QUADS[9] __asm__("jgr_ptr_garage_pending_PENDING_QUADS");
typedef char jgr_ptr_garage_pending_PENDING_QUADS_abi[(sizeof(PENDING_QUADS)==360u)?1:-1];
extern unsigned PENDING_LISTS[5][2] __asm__("jgr_ptr_garage_pending_PENDING_LISTS");
typedef char jgr_ptr_garage_pending_PENDING_LISTS_abi[(sizeof(PENDING_LISTS)==40u)?1:-1];

#endif
