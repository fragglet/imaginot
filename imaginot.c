
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

static void (interrupt far *old_int13)();
static void (interrupt far *old_int21)();
static void (interrupt far *old_int25)();
static void (interrupt far *old_int26)();

static bool hooked = false;
static int int13_count = 0;
static int int21_count = 0;
static int int25_count = 0;
static int int26_count = 0;

// Int 13h: BIOS interrupt call
static void interrupt far Int13(union INTPACK ip)
{
	++int13_count;
	_chain_intr(old_int13);
}

// Int 21h: main DOS API.
static void interrupt far Int21(union INTPACK ip)
{
	++int21_count;
	_chain_intr(old_int21);
}

// Int 25h: DOS 1+ - ABSOLUTE DISK READ
static void interrupt far Int25(union INTPACK ip)
{
	++int25_count;
	_chain_intr(old_int25);
}

// Int 26h: DOS 1+ - ABSOLUTE DISK WRITE
static void interrupt far Int26(union INTPACK ip)
{
	++int26_count;
	_chain_intr(old_int26);
}

static void RestoreInterrupts(void)
{
	if (hooked) {
		_dos_setvect(0x13, old_int13);
		_dos_setvect(0x21, old_int21);
		_dos_setvect(0x25, old_int25);
		_dos_setvect(0x26, old_int26);
		hooked = false;
		printf("Normal interrupts restored.\n");
		printf(" int 13 calls: %7d\n", int13_count);
		printf(" int 21 calls: %7d\n", int21_count);
		printf(" int 25 calls: %7d\n", int25_count);
		printf(" int 26 calls: %7d\n", int26_count);
	}
}

static void HookInterrupts(void)
{
	if (!hooked) {
		old_int13 = _dos_getvect(0x13);
		_dos_setvect(0x13, Int13);
		old_int21 = _dos_getvect(0x21);
		_dos_setvect(0x21, Int21);
		old_int25 = _dos_getvect(0x25);
		_dos_setvect(0x25, Int25);
		old_int26 = _dos_getvect(0x26);
		_dos_setvect(0x26, Int26);
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
