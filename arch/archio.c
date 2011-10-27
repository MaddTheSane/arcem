/* archio.c - IO and IOC emulation.  (c) David Alan Gilbert 1995 */
/* (c) David Alan Gilbert 1995 - see Readme file for copying info */
/*
 * $Id: archio.c,v 1.9.2.9 2011/10/27 19:37:14 phlamethrower Exp $
 */

#include <ctype.h>
#include <signal.h>

#include "../armdefs.h"
#include "../armemu.h"

#include "dbugsys.h"

#include "armarc.h"
#include "fdc1772.h"
#include "hdc63463.h"
#include "i2c.h"
#ifdef HOSTFS_SUPPORT
# include "hostfs.h"
#endif
#include "keyboard.h"
#include "displaydev.h"

/*#define IOC_TRACE*/

struct IOCStruct ioc;

static void UpdateTimerRegisters_Event(ARMul_State *state,CycleCount time);

/*-----------------------------------------------------------------------------*/

#ifndef SYSTEM_gp2x
static void FDCHDC_Poll(ARMul_State *state,CycleCount nowtime)
{
  EventQ_RescheduleHead(state,nowtime+250,FDCHDC_Poll); /* TODO - This probably needs to be made realtime */
  FDC_Regular(state);
  HDC_Regular(state);
}
#endif

/*-----------------------------------------------------------------------------*/
void
IO_Init(ARMul_State *state)
{
  ioc.ControlReg = 0xff;
  ioc.ControlRegInputData = 0x7f; /* Not sure about IF and IR pins */
  ioc.IRQStatus = 0x0090; /* (A) Top bit always set - plus power on reset */
  ioc.IRQMask = 0;
  ioc.FIRQStatus = 0;
  ioc.FIRQMask = 0;
  ioc.kbd.CurrentKeyVal = 0xff;
  ioc.LatchA = ioc.LatchB = 0xff;
  ioc.LatchAold = ioc.LatchBold = -1;
  ioc.TimerInputLatch[0] = 0xffff;
  ioc.TimerInputLatch[1] = 0xffff;
  ioc.TimerInputLatch[2] = 0xffff;
  ioc.TimerInputLatch[3] = 0xffff;
  ioc.Timer0CanInt = ioc.Timer1CanInt = 1;
  ioc.TimersLastUpdated = -1;
  ioc.NextTimerTrigger = ARMul_Time;
  ioc.TimerFracBit = 0; 
  ioc.IOCRate = ioc.InvIOCRate = 0x10000; /* Default values shouldn't matter so much */
  ioc.IOEBControlReg = 0;
  EventQ_Insert(state,ARMul_Time,UpdateTimerRegisters_Event);

  IO_UpdateNirq(state);
  IO_UpdateNfiq(state);

  I2C_Init(state);
  FDC_Init(state);
  HDC_Init(state);
  Kbd_Init(state);
#ifndef SYSTEM_gp2x
  EventQ_Insert(state,ARMul_Time+250,FDCHDC_Poll);
#endif
} /* IO_Init */

/*------------------------------------------------------------------------------*/
void
IO_UpdateNfiq(ARMul_State *state)
{
  register unsigned int tmp = state->Exception & ~Exception_FIQ;

  if (ioc.FIRQStatus & ioc.FIRQMask) {
    /* Cause FIQ */
    tmp |= Exception_FIQ;
  }

  state->Exception = tmp;
}

/*------------------------------------------------------------------------------*/
void
IO_UpdateNirq(ARMul_State *state)
{
  register unsigned int tmp = state->Exception & ~Exception_IRQ;

  if (ioc.IRQStatus & ioc.IRQMask) {
    /* Cause interrupt! */
    tmp |= Exception_IRQ;
  }

  state->Exception = tmp;
}

/** Calculate if Timer0 or Timer1 can cause an interrupt; if either of them
 *  suddenly become able to then call UpdateTimerRegisters to fix an event
 *  to cause the interrupt later */
static void
CalcCanTimerInt(ARMul_State *state)
{
  int oldTimer0CanInt = ioc.Timer0CanInt;
  int oldTimer1CanInt = ioc.Timer1CanInt;

#if 0 /* This old code was wrong and was preventing RISC OS from booting, since RISC OS checks that the timers are working (or something) by programming one of them while the IRQ is masked out */
  /* If its not causing an interrupt at the moment, and its interrupt is
     enabled */
  ioc.Timer0CanInt = ((ioc.IRQStatus & IRQA_TM0) == 0) &&
                     ((ioc.IRQMask & IRQA_TM0) != 0);
  ioc.Timer1CanInt = ((ioc.IRQStatus & IRQA_TM1) == 0) &&
                     ((ioc.IRQMask & IRQA_TM1) != 0);
#else
  /* New code: Just look at the current IRQ status (although chances are that's wrong as well?) */
  ioc.Timer0CanInt = ((ioc.IRQStatus & IRQA_TM0) == 0);
  ioc.Timer1CanInt = ((ioc.IRQStatus & IRQA_TM1) == 0);
#endif

  /* If any have just been enabled update the triggers */
  if (((!oldTimer0CanInt) && (ioc.Timer0CanInt)) ||
      ((!oldTimer1CanInt) && (ioc.Timer1CanInt)))
    UpdateTimerRegisters(state);
} /* CalcCanTimerInt */

/** Get the value of a timer uptodate - don't actually update anything - just
 * return the current value (for the Latch command)
 */
static int
GetCurrentTimerVal(ARMul_State *state,int toget)
{
  long timeSinceLastUpdate = ARMul_Time - ioc.TimersLastUpdated;
  long scaledTimeSlip = (((unsigned long long) timeSinceLastUpdate) * ioc.IOCRate)>>16;
  long tmpL;
  int result;

  tmpL = ioc.TimerInputLatch[toget];
  if (tmpL == 0) tmpL = 1;
  result = ioc.TimerCount[toget] - (scaledTimeSlip % tmpL);
  if (result < 0) result += tmpL;

  return result;
}


/*------------------------------------------------------------------------------*/
static void UpdateTimerRegisters_Internal(ARMul_State *state,CycleCount nowtime,int idx)
{
  CycleDiff timeSinceLastUpdate = nowtime - ioc.TimersLastUpdated;
  /* Take into account any lost fractions of an IOC tick */
  unsigned long long TimeSlip = (((unsigned long long) timeSinceLastUpdate) * ioc.IOCRate)+ioc.TimerFracBit;
  ioc.TimerFracBit = TimeSlip & 0xffff;
  CycleDiff scaledTimeSlip = TimeSlip>>16;
  unsigned long tmpL;

  /* In theory we should be able to use MAX_CYCLES_INTO_FUTURE as our default
     next trigger time. But some software (e.g. Lotus Turbo Challenge II) seems
     to break and get stuck in a loop waiting for an interrupt which never
     happens (presumably due a bug in ArcEm somewhere).
     So use a failsafe default next trigger time of 65536 IOC cycles from now
     (i.e. the max possible timer period) */
  CycleDiff nextTrigger = ioc.InvIOCRate; /* a.k.a. 65536 IOC cycles from now */

  /* ----------------------------------------------------------------- */
  tmpL = ioc.TimerInputLatch[0];
  if (tmpL == 0) tmpL = 1;
  if (ioc.TimerCount[0] <= scaledTimeSlip) {
    KBD.TimerIntHasHappened++;
    ioc.IRQStatus |= IRQA_TM0;
    IO_UpdateNirq(state);
    ioc.Timer0CanInt = 0; /* Because it's just caused one which hasn't cleared yet */
  }
  ioc.TimerCount[0] -= (scaledTimeSlip % tmpL);
  if (ioc.TimerCount[0] < 0) ioc.TimerCount[0] += tmpL;

  if (ioc.Timer0CanInt) {
    tmpL = (((unsigned long long) ioc.TimerCount[0]) * ioc.InvIOCRate) >> 16;
    if (tmpL < nextTrigger) nextTrigger = tmpL;
  }

  /* ----------------------------------------------------------------- */
  tmpL = ioc.TimerInputLatch[1];
  if (tmpL == 0) tmpL = 1;
  if (ioc.TimerCount[1] <= scaledTimeSlip) {
    ioc.IRQStatus |= IRQA_TM1;
    IO_UpdateNirq(state);
    ioc.Timer1CanInt = 0; /* Because its just caused one which hasn't cleared yet */
  }
  ioc.TimerCount[1] -= (scaledTimeSlip % tmpL);
  if (ioc.TimerCount[1] < 0) ioc.TimerCount[1] += tmpL;

  if (ioc.Timer1CanInt) {
    tmpL = (((unsigned long long) ioc.TimerCount[1]) * ioc.InvIOCRate) >> 16;
    if (tmpL < nextTrigger) nextTrigger = tmpL;
  }

  /* ----------------------------------------------------------------- */
  if (ioc.TimerInputLatch[2]) {
    ioc.TimerCount[2] -= (scaledTimeSlip % ioc.TimerInputLatch[2]);
    if(ioc.TimerCount[2] < 0) ioc.TimerCount[2] += ioc.TimerInputLatch[2];
  }

  /* ----------------------------------------------------------------- */
  if (ioc.TimerInputLatch[3]) {
    ioc.TimerCount[3] -= (scaledTimeSlip % ioc.TimerInputLatch[3]);
    if(ioc.TimerCount[3] < 0) ioc.TimerCount[3] += ioc.TimerInputLatch[3];
  }

  ioc.TimersLastUpdated = nowtime;

  /* Don't get stuck if we're waiting for something that's about to fire */
  if(!idx && (nextTrigger < 32768) && (nextTrigger*ioc.IOCRate < 65536))
  {
    do {
      nextTrigger = (nextTrigger<<1) | 1;
    } while(nextTrigger*ioc.IOCRate < 65536);
  }

  ioc.NextTimerTrigger = nowtime + nextTrigger;
  EventQ_Reschedule(state,nowtime + nextTrigger,UpdateTimerRegisters_Event,idx);
}

void
UpdateTimerRegisters(ARMul_State *state)
{
  UpdateTimerRegisters_Internal(state,ARMul_Time,EventQ_Find(state,UpdateTimerRegisters_Event));
}

void
UpdateTimerRegisters_Event(ARMul_State *state,CycleCount nowtime)
{
  UpdateTimerRegisters_Internal(state,nowtime,0);
}

/** Called when there has been a write to the IOC control register - this
 *  potentially affects the values on the C0-C5 control lines
 *
 *  C0 is I2C clock, C1 is I2C data
 */
static void
IOC_ControlLinesUpdate(ARMul_State *state)
{

  dbug_ioc("IOC_ControlLines: Clk=%d Data=%d\n", (ioc.ControlReg & 2) != 0,
           ioc.ControlReg & 1);
  I2C_Update(state);

} /* IOC_ControlLinesUpdate */

/** Parse an IOC address
 *
 * \param address Address (assumed to be in IO-space)
 * \param bank    Return IOC bank
 * \param speed   Return IOC speed
 * \param offset  Return IOC offset
 */
static void
ParseIOCAddr(ARMword address, ARMword *bank, ARMword *speed, ARMword *offset)
{
  *bank   = (address >> 16) & 7;
  *speed  = (address >> 19) & 3;

  *offset = address & 0xffff;

  /*fprintf(stderr, "ParseIOCAddr: address=0x%x bank=%u speed=%u offset=0x%x\n",
          address, *bank, *speed, *offset); */
} /* ParseIOCAddr */

/*-----------------------------------------------------------------------------*/
static ARMword
GetWord_IOCReg(ARMul_State *state, int Register)
{
  ARMword Result;
  int Timer;

  switch (Register) {
    case 0: /* Control reg */
      Result = ioc.ControlRegInputData & ioc.ControlReg;
      dbug_ioc("IOCRead: ControlReg=0x%x\n", Result);
      break;

    case 1: /* Serial Rx data */
      Result = ioc.SerialRxData;
      ioc.IRQStatus &= ~IRQB_SRX; /* Clear receive reg full */
      dbug_ioc("IOCRead: SerialRxData=0x%x\n", Result);
      IO_UpdateNirq(state);
      break;

    case 4: /* IRQ Status A */
      Result = ioc.IRQStatus & 0xff;
      dbug_ioc("IOCRead: IRQStatusA=0x%x\n", Result);
      break;

    case 5: /* IRQ Request A */
      Result = (ioc.IRQStatus & ioc.IRQMask) & 0xff;
      dbug_ioc("IOCRead: IRQRequestA=0x%x\n", Result);
      break;

    case 6: /* IRQ Mask A */
      Result = ioc.IRQMask & 0xff;
      dbug_ioc("IOCRead: IRQMaskA=0x%x\n", Result);
      break;

    case 8: /* IRQ Status B */
      Result = ioc.IRQStatus >> 8;
      dbug_ioc("IOCRead: IRQStatusB=0x%x\n", Result);
      break;

    case 9: /* IRQ Request B */
      Result = (ioc.IRQStatus & ioc.IRQMask) >> 8;
      dbug_ioc("IOCRead: IRQRequestB=0x%x\n", Result);
      break;

    case 0xa: /* IRQ Mask B */
      Result = ioc.IRQMask >> 8;
      dbug_ioc("IOCRead: IRQMaskB=0x%x\n", Result);
      break;

    case 0xc: /* FIRQ Status */
      Result = ioc.FIRQStatus;
      dbug_ioc("IOCRead: FIRQStatus=0x%x\n", Result);
      break;

    case 0xd: /* FIRQ Request */
      Result = ioc.FIRQStatus & ioc.FIRQMask;
      dbug_ioc("IOCRead: FIRQRequest=0x%x\n", Result);
      break;

    case 0xe: /* FIRQ mask */
      Result = ioc.FIRQMask;
      fprintf(stderr,"IOCRead: FIRQMask=0x%x\n", Result);
      break;

    case 0x10: /* T0 count low */
    case 0x14: /* T1 count low */
    case 0x18: /* T2 count low */
    case 0x1c: /* T3 count low */
      Timer = (Register & 0xf) / 4;
      Result = ioc.TimerOutputLatch[Timer] & 0xff;
      /*dbug_ioc("IOCRead: Timer %d low counter read=0x%x\n", Timer, Result);
      dbug_ioc("SPECIAL: R0=0x%x R1=0x%x R14=0x%x\n", state->Reg[0],
              state->Reg[1], state->Reg[14]); */
      break;

    case 0x11: /* T0 count high */
    case 0x15: /* T1 count high */
    case 0x19: /* T2 count high */
    case 0x1a: /* T3 count high */
      Timer = (Register & 0xf) / 4;
      Result = (ioc.TimerOutputLatch[Timer] / 256) & 0xff;
      dbug_ioc("IOCRead: Timer %d high counter read=0x%x\n", Timer, Result);
      break;

    default:
      Result = 0;
      fprintf(stderr, "IOCRead: Read of unknown IOC register %d\n", Register);
      break;
  }

  return Result;
} /* GetWord_IOCReg */

/*-----------------------------------------------------------------------------*/
static ARMword
PutVal_IOCReg(ARMul_State *state, int Register, ARMword data, int bNw)
{
  int Timer;

  switch (Register) {
    case 0: /* Control reg */
      ioc.ControlReg = (data & 0x3f) | 0xc0; /* Needs more work */
      IOC_ControlLinesUpdate(state);
      dbug_ioc("IOC Write: Control reg val=0x%x\n", data);
      break;

    case 1: /* Serial Tx Data */
      ioc.SerialTxData = data; /* Should tell the keyboard about this */
      ioc.IRQStatus &= ~IRQB_STX; /* Clear KART Tx empty */
      dbug_ioc("IOC Write: Serial Tx Reg Val=0x%x\n", data);
      IO_UpdateNirq(state);
      break;

    case 5: /* IRQ Clear */
      dbug_ioc("IOC Write: Clear Ints Val=0x%x\n", data);
      /* Clear appropriate interrupts */
      data &= 0x7c;
      ioc.IRQStatus &= ~data;
      /* If we have cleared a timer interrupt then it may cause another */
      if (data & 0x60)
        CalcCanTimerInt(state);
      IO_UpdateNirq(state);
      break;

    case 6: /* IRQ Mask A */
      ioc.IRQMask &= 0xff00;
      ioc.IRQMask |= (data & 0xff);
      CalcCanTimerInt(state);
      dbug_ioc("IOC Write: IRQ Mask A Val=0x%x\n", data);
      IO_UpdateNirq(state);
      break;

    case 0xa: /* IRQ mask B */
      ioc.IRQMask &= 0xff;
      ioc.IRQMask |= (data & 0xff) << 8;
      dbug_ioc("IOC Write: IRQ Mask B Val=0x%x\n", data);
      IO_UpdateNirq(state);
      break;

    case 0xe: /* FIRQ Mask */
      ioc.FIRQMask = data;
      IO_UpdateNfiq(state);
      dbug_ioc("IOC Write: FIRQ Mask Val=0x%x\n", data);
      break;

    case 0x10: /* T0 latch low */
    case 0x14: /* T1 latch low */
    case 0x18: /* T2 latch low */
    case 0x1c: /* T3 latch low */
      Timer = (Register & 0xf) / 4;
      UpdateTimerRegisters(state);
      ioc.TimerInputLatch[Timer] &= 0xff00;
      ioc.TimerInputLatch[Timer] |= data;
      UpdateTimerRegisters(state);
      dbug_ioc("IOC Write: Timer %d latch write low Val=0x%x InpLatch=0x%x\n",
              Timer, data, ioc.TimerInputLatch[Timer]);
      break;

    case 0x11: /* T0 latch High */
    case 0x15: /* T1 latch High */
    case 0x19: /* T2 latch High */
    case 0x1d: /* T3 latch High */
      Timer = (Register & 0xf) / 4;
      UpdateTimerRegisters(state);
      ioc.TimerInputLatch[Timer] &= 0xff;
      ioc.TimerInputLatch[Timer] |= data << 8;
      UpdateTimerRegisters(state);
      dbug_ioc("IOC Write: Timer %d latch write high Val=0x%x InpLatch=0x%x\n",
              Timer, data, ioc.TimerInputLatch[Timer]);
      break;

    case 0x12: /* T0 Go */
    case 0x16: /* T1 Go */
    case 0x1a: /* T2 Go */
    case 0x1e: /* T3 Go */
      Timer = (Register & 0xf) / 4;
      UpdateTimerRegisters(state);
      ioc.TimerCount[Timer] = ioc.TimerInputLatch[Timer];
      UpdateTimerRegisters(state);
      dbug_ioc("IOC Write: Timer %d Go! Counter=0x%x\n",
              Timer, ioc.TimerCount[Timer]);
      break;

    case 0x13: /* T0 Latch command */
    case 0x17: /* T1 Latch command */
    case 0x1b: /* T2 Latch command */
    case 0x1f: /* T3 Latch command */
      Timer = (Register & 0xf) / 4;
      ioc.TimerOutputLatch[Timer] = GetCurrentTimerVal(state,Timer) & 0xffff;
      /*dbug_ioc("(T%dLc)", Timer); */
      /*dbug_ioc("IOC Write: Timer %d Latch command Output Latch=0x%x\n",
        Timer, ioc.TimerOutputLatch[Timer]); */
      break;

    default:
      fprintf(stderr,"IOC Write: Bad IOC register write reg=%d data=0x%x\n",
              Register, data);
      break;
  } /* Register */

  return 0;
} /* PutVal_IOCReg */

/** Read a word of data from IO address space
 *
 * \param state   Emulator state
 * \param address Address to read from (must be in IO address space)
 * \return Data read
 */
ARMword
GetWord_IO(ARMul_State *state, ARMword address)
{
  /* Test CS (Chip-Select) bit */
  if (address & (1 << 21)) {
    /* IOC address-space */

    ARMword bank, speed, offset;

    /* Sanitise the address a bit */
    address -= MEMORY_0x3000000_CON_IO;

    ParseIOCAddr(address, &bank, &speed, &offset);

    switch (bank) {
      case 0:
        offset /= 4;
        offset &= 0x1f;
        return GetWord_IOCReg(state, offset);

      case 1:
        return FDC_Read(state, offset);

      case 2:
        dbug_ioc("Read from Econet register (addr=0x%x speed=0x%x offset=0x%x)\n",
                 address, speed, offset);
        break;

      case 3:
        dbug_ioc("Read from Serial port register (addr=0x%x speed=0x%x offset=0x%x)\n",
                 address, speed, offset);
        break;

      case 4:
        dbug_ioc("Read from Internal expansion card (addr=0x%x speed=0x%x offset=0x%x)\n",
                 address, speed, offset);
        break;

      case 5:
        /*dbug_ioc("HDC read: address=0x%x speed=%d\n",address,speed); */
        return HDC_Read(state, offset);

      case 6:
        dbug_ioc("Read from Bank 6 (addr=0x%x speed=0x%x offset=0x%x)\n",
                 address, speed, offset);
        break;

      case 7:
        dbug_ioc("Read from External expansion card (addr=0x%x speed=0x%x offset=0x%x)\n",
                 address, speed, offset);
        break;
    } /* Bank switch */
  } else {
    /* IO-address space unused on Arc hardware */
    /*EnableTrace();*/
    fprintf(stderr, "Read from non-IOC IO space (addr=0x%08x pc=0x%08x\n",
            address, state->pc);

    return 0;
  }

  return 0xffffff; /* was 0 */
} /* GetWord_IO */

/** Write a word of data to IO address space
 *
 * \param state       Emulator state
 * \param address     Address to write to (must be in IO address space)
 * \param data        Data to store
 * \param byteNotword Non-zero if this is a byte store, not a word store
 */
void
PutValIO(ARMul_State *state, ARMword address, ARMword data, int byteNotword)
{
  /* Test CS (Chip-Select) bit */
  if (address & (1 << 21)) {
    /* IOC address-space */

    ARMword bank, speed, offset;

    /* Sanitise the address a bit */
    address -= MEMORY_0x3000000_CON_IO;

    ParseIOCAddr(address, &bank, &speed, &offset);

    if (!byteNotword) {
      /* The data bus is wierdly connected to the IOC - in the case of word
      writes, bits 16-23 of the data bus end up on the IOC data bus */
      data >>= 16;
      data &= 0xffff;
    } else {
      /* Just 8 bits actually go to the IOC */
      data &= 0xff;
    }

    switch (bank) {
      case 0:
        offset /= 4;
        offset &= 0x1f;
        PutVal_IOCReg(state, offset, data & 0xff, byteNotword);
        break;

      case 1:
        FDC_Write(state, offset, data & 0xff, byteNotword);
        break;

      case 2:
        dbug_ioc("Write to Econet register (addr=0x%x speed=0x%x offset=0x%x data=0x%x %c\n",
                 address, speed, offset, data,
                 isprint(data & 0xff) ? data : ' ');
        break;

      case 3:
        dbug_ioc("Write to Serial port register (addr=0x%x speed=0x%x offset=0x%x data=0x%x\n",
                 address, speed, offset, data);
        break;

      case 4:
        dbug_ioc("Write to Internal expansion card (addr=0x%x speed=0x%x offset=0x%x data=0x%x\n",
                 address, speed, offset, data);
        break;

      case 5:
        /* It's either Latch A/B, printer DATA  or the HDC */
        switch (offset & 0x7c) {
          case 0x00:
          case 0x04:
          case 0x08:
          case 0x0c:
            /*fprintf(stderr,"HDC write: address=0x%x speed=%u\n", address, speed); */
            HDC_Write(state, offset, data);

          case 0x10:
          case 0x14: /* Is it right for this to be 8 bytes wide? */
            dbug_ioc("Write to Printer data latch offset=0x%x data=0x%x\n",
                     offset, data);
            break;

          case 0x18:
          case 0x1c: /* Is it right for this to be 8 bytes wide? */
            dbug_ioc("Write to Latch B offset=0x%x data=0x%x\n",
                     offset, data);
            ioc.LatchB = data & 0xff;
            FDC_LatchBChange(state);
            ioc.LatchBold = data & 0xff;
            break;

          case 0x40:
          case 0x44: /* Is it right for this to be 8 bytes wide? */
            dbug_ioc("Write to Latch A offset=0x%x data=0x%x\n",
                     offset, data);
            ioc.LatchA = data & 0xff;
            FDC_LatchAChange(state);
            ioc.LatchAold = data & 0xff;
            break;

          case 0x48:
            dbug_ioc("Write to IOEB CR offset=0x%x data=0x%x\n",
                     offset, data);
            data &= 0xf;
            if(ioc.IOEBControlReg != data)
            {
              ioc.IOEBControlReg = data;
              (DisplayDev_Current->IOEBCRWrite)(state,data);
            }
            break;

          default:
            dbug_ioc("Writing to non-defined bank 5 address offset=0x%x data=0x%x\n",
                     offset, data);
            break;
        }
        break;

      case 6:
        dbug_ioc("Write to Bank 6 (addr=0x%x speed=0x%x offset=0x%x data=0x%x\n",
                 address, speed, offset, data);
        break;

      case 7:
        dbug_ioc("Write to External expansion card (addr=0x%x speed=0x%x offset=0x%x data=0x%x\n",
                 address, speed, offset, data);
        break;
    } /* Bank switch */
  } else {
    /* IO-address space unused on Arc hardware */
    /* Use it as general-purpose memory-mapped IO space */
    ARMword application = (address >> 12) & 0x1ff;
    ARMword operation   = (address & 0xfff);

    switch (application) {
    case 0x000: /* ArcEm internal */
      break;
#ifdef HOSTFS_SUPPORT
    case 0x001: /* HostFS */
      hostfs(state, operation);
      break;
#endif
    default:
      fprintf(stderr, "Write to non-IOC IO space (addr=0x%x app=0x%03x op=0x%03x data=0x%08x\n",
              address, application, operation, data);
    }
  }
} /* PutValIO */

/* ------------------- access routines for other sections of code ------------- */

/** Returns a byte in the keyboard serialiser tx register - or -1 if there isn't
 *  one. Clears the tx register full flag
 */
int
IOC_ReadKbdTx(ARMul_State *state)
{
  if ((ioc.IRQStatus & IRQB_STX) == 0) {
    /*fprintf(stderr, "ArmArc_ReadKbdTx: Value=0x%x\n", ioc.SerialTxData); */
    /* There is a byte present (Kart TX not empty) */
    /* Mark as empty and then return the value */
    ioc.IRQStatus |= IRQB_STX;
    IO_UpdateNirq(state);
    return ioc.SerialTxData;
  } else return -1;
} /* IOC_ReadKbdTx */

/** Places a byte in the keyboard serialiser rx register; returns -1 if its
 *  still full.
 */
int
IOC_WriteKbdRx(ARMul_State *state, unsigned char value)
{
  /*fprintf(stderr, "ArmArc_WriteKbdRx: value=0x%x\n", value); */
  if (ioc.IRQStatus & IRQB_SRX) {
    /* Still full */
    return -1;
  } else {
    /* We write only if it was empty */
    ioc.SerialRxData = value;

    ioc.IRQStatus |= IRQB_SRX; /* Now full */
    IO_UpdateNirq(state);
  }

  return 0;
} /* IOC_WriteKbdRx */
