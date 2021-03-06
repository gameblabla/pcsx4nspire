/***************************************************************************
 *   Copyright (C) 2010 by Blade_Arma                                      *
 *   Copyright (C) 2007 Ryan Schultz, PCSX-df Team, PCSX team              *
 *   schultz.ryan@gmail.com, http://rschultz.ath.cx/code.php               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Steet, Fifth Floor, Boston, MA 02111-1307 USA.            *
 ***************************************************************************/

#ifndef __PSXCOUNTERS_H__
#define __PSXCOUNTERS_H__

#include "psxcommon.h"
#include "r3000a.h"
#include "psxmem.h"
#include "plugins.h"

extern u32 hSyncCount, frame_counter;
extern u32 spu_upd_interval;

typedef struct Rcnt
{
    u16 mode, target;
    u32 rate, irq, counterState, irqState;
    u32 cycle, cycleStart;
} Rcnt;

extern void psxRcntInit(void);
extern void psxRcntUpdate(void);

extern void psxRcntWcount(u32 index, u32 value);
extern void psxRcntWmode(u32 index, u32 value);
extern void psxRcntWtarget(u32 index, u32 value);

extern u32 psxRcntRcount(u32 index);
extern u32 psxRcntRmode(u32 index);
extern u32 psxRcntRtarget(u32 index);

extern int psxRcntFreeze(void* f, FreezeMode mode);

extern unsigned psxGetSpuSync(void);

// senquack - Called before psxRegs.cycle is adjusted back to zero
void psxRcntAdjustTimestamps(const uint32_t prev_cycle_val);

#endif /* __PSXCOUNTERS_H__ */
