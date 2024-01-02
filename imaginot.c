
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#pragma pack(push, 1)

struct bpb {
	uint16_t bytes_per_sector;
	uint8_t sectors_per_cluster;
	uint16_t reserved_sectors;
	uint8_t num_fats;
	uint16_t root_dir_count;
	uint16_t total_sectors;
	uint8_t media_descriptor;
	uint16_t sectors_per_fat;
};

static void (interrupt far *old_int13)();
static void (interrupt far *old_int21)();
static void (interrupt far *old_int25)();
static void (interrupt far *old_int26)();

static bool hooked = false;
static int int13_count = 0;
static int int21_count = 0;
static int int25_count = 0;
static int int26_count = 0;
static int reads = 0;

//
// Emulated floppy disk:
// Imaginet worked by sharing a floppy drive at the raw, hardware level.
// Sopwith players exchanged data by writing to a particular sector on
// the disk. So we emulate a simple DOS drive and barebones FAT12:
//
//
enum {
	//            Sector #   Purpose
	SECTOR_BOOT,      // 0   Boot sector & BIOS parameter block (BPB)
	SECTOR_FAT,       // 1   File allocation table
	SECTOR_DIRECTORY, // 2   Directory table for root directory (B:\)
	SECTOR_SOPWITH1,  // 3   Contents of file SOPWITH1.DTA
	SECTOR_SEMAPHOR,  // 4   Contents of file SEMAPHOR
};

static uint8_t ReadBootSector(void far *data)
{
	struct bpb far *bpb;

	bpb = ((struct bpb far *) data) + 0x0b;

	bpb->bytes_per_sector = 512;
	bpb->sectors_per_cluster = 1;
	bpb->reserved_sectors = 0;
	bpb->num_fats = 1;
	bpb->root_dir_count = 2;
	bpb->total_sectors = 320;  // 160KiB disk
	bpb->media_descriptor = 0;
	bpb->sectors_per_fat = 1;

	return 0;
}

static uint8_t ReadDirectory(void far *data)
{
	return 0;
}

static uint8_t ReadSector(void far *data, uint32_t sector, uint16_t cnt)
{
	// Only ever one sector:
	if (cnt != 1) {
		return 1;
	}

	// All sectors are low:
	if (sector > 16) {
		return 1;
	}

	++reads;

	_fmemset(data, 0, 512);

	switch (sector) {
	case SECTOR_BOOT:
		return ReadBootSector(data);

	case SECTOR_FAT:
		// Unused.
		return 0;

	case SECTOR_DIRECTORY:
		return ReadDirectory(data);

	case SECTOR_SOPWITH1:
	case SECTOR_SEMAPHOR:
		return 1;  // TODO

	default:
		return 1;
	}
}

// Int 13h: BIOS interrupt call
static void interrupt far Int13(union INTPACK ip)
{
	++int13_count;

	// ah=2 -> DISK - READ SECTOR(S) INTO MEMORY
	// al=1 -> Drive B:
	if (ip.h.ah == 2 && ip.h.dl == 1) {
		// BIOS sector numbers are indexed from 1.
		uint32_t sector = ((uint32_t) ip.x.cx - 1UL)
			| (((uint32_t) ip.h.dh) << 16);
		uint8_t result = ReadSector(
			MK_FP(ip.x.es, ip.x.bx), sector, ip.h.al);
		return;
	}
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

	// al=1 -> Drive B:
	if (ip.h.al == 1) {
		uint8_t result = ReadSector(
			MK_FP(ip.x.ds, ip.x.bx), ip.x.dx, ip.x.cx);
		return;
	}

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
		printf(" reads:        %7d\n", reads);
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
