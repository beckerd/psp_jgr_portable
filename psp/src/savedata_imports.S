/* Weak imports allow the PSP loader to bind the save-hashing service after
   chnnlsv.prx loads. Keep these flags explicit across PSPSDK versions. */
#include <pspstub.s>
STUB_START "sceChnnlsv",0x40090000,0x00030005
STUB_FUNC 0xE7833020,sceSdSetIndex
STUB_FUNC 0xF21A1FCA,sceSdRemoveValue
STUB_FUNC 0xC4C494F8,sceSdGetLastIndex
STUB_END
