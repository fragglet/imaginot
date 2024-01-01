
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

static void (interrupt far *old_int21)();

static bool hooked = false;
static int int21_count = 0;

static void interrupt far Int21(union INTPACK ip)
{
	++int21_count;
	_chain_intr(old_int21);
}

static void RestoreInterrupts(void)
{
	if (hooked) {
		_dos_setvect(0x21, old_int21);
		hooked = false;
		printf("Normal interrupts restored.\n");
		printf(" int 21 calls: %7d\n", int21_count);
	}
}

static void HookInterrupts(void)
{
	if (!hooked) {
		old_int21 = _dos_getvect(0x21);
		_dos_setvect(0x21, Int21);
		hooked = true;
		printf("Interrupts hooked.\n");
	}
}

int main(int argc, char *argv[])
{
	HookInterrupts();
	atexit(RestoreInterrupts);
	return system("command");
}
