//
// Copyright(C) 1993 id Software, Inc.
// Copyright(C) 2019-2023 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>
#include <assert.h>
#include <dos.h>

#include "lib/flag.h"
#include "lib/log.h"
#include "doomnet.h"

// NetGetHandle takes the given long value read from the command line
// and returns a doomcom_t pointer, performing appropriate checks.
doomcom_t far *NetGetHandle(long l)
{
    doomcom_t far *result = NULL;
    unsigned int seg;

    assert(l != 0);
    seg = (int) ((l >> 4) & 0xf000L);
    result = (void far *) MK_FP(seg, l & 0xffffL);
    assert(result->id == DOOMCOM_ID);

    return result;
}

void NetSendPacket(doomcom_t far *doomcom)
{
    union REGS regs;
    doomcom->command = CMD_SEND;
    int86(doomcom->intnum, &regs, &regs);
}

int NetGetPacket(doomcom_t far *doomcom)
{
    union REGS regs;
    doomcom->command = CMD_GET;
    int86(doomcom->intnum, &regs, &regs);
    return doomcom->remotenode != -1;
}


