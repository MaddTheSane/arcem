/* Display and keyboard interface for the Arc emulator */
/* (c) David Alan Gilbert 1995-1999 - see Readme file for copying info */

#ifndef DISPKBD_HEADER
#define DISPKBD_HEADER

#include "../armdefs.h"
#include "../armopts.h"


#define KBDBUFFLEN 128

typedef struct {
  int KeyColToSend,KeyRowToSend,KeyUpNDown;
} KbdEntry;

typedef enum {
  KbdState_JustStarted,
  KbdState_SentHardReset,
  KbdState_SentAck1,
  KbdState_SentAck2,
  KbdState_SentKeyByte1, /* and waiting for ack */
  KbdState_SentKeyByte2, /* and waiting for ack */
  KbdState_SentMouseByte1,
  KbdState_SentMouseByte2,
  KbdState_Idle
} KbdStates;

typedef struct {
  struct {
    int MustRedraw;  /* Set to 1 by emulator if something major changes */
    int MustResetPalette; /* Set to 1 by emulator if palette is changed */
    int PollCount;
    int AutoRefresh;
    unsigned int UpdateFlags[(512*1024)/256]; /* Matches the ones in
                                     the memory model - if they are different
                                     redraw */
    int miny,maxy;  /* min and max values which need refereshing on the X display */
    int DoingMouseFollow; /* If true following the mouse */
  } Control;

  struct {
    unsigned int Palette[16];
    unsigned int BorderCol:14;
    unsigned int CursorPalette[3];
    unsigned int Horiz_Cycle;
    unsigned int Horiz_SyncWidth;
    unsigned int Horiz_BorderStart;
    unsigned int Horiz_DisplayStart; /* In 2 pixel units */
    unsigned int Horiz_DisplayEnd;   /* In 2 pixel units */
    unsigned int Horiz_BorderEnd;
    unsigned int Horiz_CursorStart;
    unsigned int Horiz_Interlace;
    unsigned int Vert_Cycle;
    unsigned int Vert_SyncWidth;
    unsigned int Vert_BorderStart;
    unsigned int Vert_DisplayStart;
    unsigned int Vert_DisplayEnd;
    unsigned int Vert_BorderEnd;
    unsigned int Vert_CursorStart;
    unsigned int Vert_CursorEnd;
    unsigned int SoundFreq;
    unsigned int ControlReg;
    unsigned int StereoImageReg[8];
  } Vidc;

  struct {
    KbdStates KbdState;
    int MouseXCount,MouseYCount;
    int KeyColToSend,KeyRowToSend,KeyUpNDown;
    int Leds;
    int MouseXToSend,MouseYToSend; /* Double buffering - update the others while
                                      sending this */
    int MouseTransEnable,KeyScanEnable; /* When 1 allowed to transmit */
    int HostCommand; /* Normally 0 else the command code */
    KbdEntry Buffer[KBDBUFFLEN];
    int BuffOcc;
    int TimerIntHasHappened;
  } Kbd;
} DisplayInfo;

#define DISPLAYINFO (PRIVD->Display)
#define VIDC (DISPLAYINFO.Vidc)
/* Use this in gdb: ((PrivateDataType *)state->MemDataPtr)->Display->HostDisplay */
#define HOSTDISPLAY (DISPLAYINFO.HostDisplay)
#define DISPLAYCONTROL (DISPLAYINFO.Control)
#define KBD (DISPLAYINFO.Kbd)

#define VideoRelUpdateAndForce(FLAG,WRITETO,FROM) {\
                                               if ((WRITETO)!=(FROM)) { \
                                                 (WRITETO)=(FROM);\
                                                 FLAG=1;\
                                               };\
                                             };
/*-----------------------------------------------------------------------------*/

unsigned int DisplayKbd_XPoll(void *data);
void DisplayKbd_Init(ARMul_State *state);
void VIDC_PutVal(ARMul_State *state,ARMword address, ARMword data,int bNw);


#endif
