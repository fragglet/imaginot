
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>

#include "fakedisk.h"
#include "lib/flag.h"
#include "lib/log.h"

static long doomcom_addr = 0;

void SetDoomcomAddr(long value)
{
	doomcom_addr = value;
}

int main(int argc, char *argv[])
{
	char **args;

	SetHelpText("Sopwith multiplayer adapter", "ipxsetup %s sopwith2");
	APIPointerFlag("-net", SetDoomcomAddr);

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

	HookDiskInterrupts();
	atexit(RestoreDiskInterrupts);
	return spawnv(P_WAIT, args[0], (void *) args);
}
