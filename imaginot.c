//
// Copyright(C) 2024 Simon Howard
//
// You can redistribute and/or modify this program under the terms of the
// GNU General Public License version 2 as published by the Free Software
// Foundation, or any later version. This program is distributed WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <process.h>

#include "lib/flag.h"
#include "lib/log.h"

#include "doomnet.h"
#include "fakedisk.h"
#include "mempatch.h"
#include "protocol.h"

static long doomcom_addr = 0;

static void SetDoomcomAddr(long value)
{
    doomcom_addr = value;
}

int main(int argc, char *argv[])
{
    char **args;
    doomcom_t far *doomcom;

    SetHelpText("Sopwith multiplayer adapter", "ipxsetup %s sopwith2");
    APIPointerFlag("-net", SetDoomcomAddr);
    BoolFlag("-fakeplayers", &fake_players, "Simulate four player game");
    BoolFlag("-nomempatch", &no_mem_patch,
             "Don't patch Sopwith memory to fix bugs");

    args = ParseCommandLine(argc, argv);
    if (args == NULL)
    {
        ErrorPrintUsage("");
    }
    if (doomcom_addr == 0)
    {
        ErrorPrintUsage("You should run this program via a driver, eg. "
                        "IPXSETUP.");
    }

    doomcom = NetGetHandle(doomcom_addr);
    InitProtocol(doomcom);

    HookDiskInterrupts();
    atexit(RestoreDiskInterrupts);
    return spawnv(P_WAIT, args[0], (void *) args);
}
