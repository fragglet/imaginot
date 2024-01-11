
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fakedisk.h"

int main(int argc, char *argv[])
{
	HookDiskInterrupts();
	atexit(RestoreDiskInterrupts);
	return system("command");
}
