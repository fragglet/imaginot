
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#pragma pack(push, 1)

// Sopwith only allows games 0...7
#define NUM_GAMES 8

enum {
	PLAYER_WAITING = 0,
	PLAYER_FLYING = 1,
	PLAYER_FINISHED = 91,
};

struct bpb {
	uint16_t bytes_per_sector;
	uint8_t sectors_per_cluster;
	uint16_t reserved_sectors;
	uint8_t num_fats;
	uint16_t root_dir_count;
	uint16_t total_sectors;
	uint8_t media_descriptor;
	uint16_t sectors_per_fat;
	uint16_t sectors_per_track;
	uint16_t num_heads;
};

struct fat_dirent {
	char name[11];
	uint8_t attr;
	uint8_t res[10];
	uint16_t time;
	uint16_t date;
	uint16_t cluster;
	uint32_t size;
};

// Format of the Sopwith communications buffer (sopwith1.dta):
struct sop_multio {
	uint8_t max_players;
	uint8_t num_players;
	uint8_t last_player;
	uint8_t key[8];
	uint8_t state[4];
	uint8_t explseed;
};

static void (interrupt far *old_int13)();
static void (interrupt far *old_int21)();
static void (interrupt far *old_int25)();
static void (interrupt far *old_int26)();

static bool locked = false;

static bool hooked = false;
static int int13_count = 0;
static int int21_count = 0;
static int int25_count = 0;
static int int26_count = 0;
static int reads = 0, writes = 0;

static uint8_t data_bytes[40];

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

static bool ReadBootSector(uint8_t far *data)
{
	struct bpb far *bpb;

	bpb = (struct bpb far *) (data + 0x0b);

	bpb->bytes_per_sector = 512;
	bpb->sectors_per_cluster = 1;
	bpb->reserved_sectors = 1;  // = boot sector
	bpb->num_fats = 1;
	bpb->root_dir_count = NUM_GAMES + 1;
	bpb->total_sectors = 320;  // 160KiB disk
	bpb->media_descriptor = 0xed;
	bpb->sectors_per_fat = 1;
	bpb->sectors_per_track = 10;
	bpb->num_heads = 1;

	return true;
}

static bool ReadDirectory(void far *data)
{
	struct fat_dirent far *dirents = data;
	int i;

	_fmemcpy(dirents[0].name, "SEMAPHOR   ", 11);
	dirents[0].cluster = SECTOR_SEMAPHOR;
	dirents[0].size = 512;

	// We make 7 files for sopwith0...7.dta, but they all point
	// to the same cluster on "disk":
	for (i = 0; i < NUM_GAMES; i++)
	{
		_fmemcpy(dirents[i + 1].name, "SOPWITH0DTA", 11);
		dirents[i + 1].name[7] += i;
		dirents[i + 1].cluster = SECTOR_SOPWITH1;
		dirents[i + 1].size = 512;
	}

	return true;
}

static bool ReadSemaphore(void far *data)
{
	*((uint8_t far *) data) = locked ? 0xff : 0xfe;
	return true;
}

static bool ReadData(void far *data)
{
	_fmemcpy(data, data_bytes, 40);
	return true;
}

static bool ReadSector(void far *data, uint32_t sector, uint16_t cnt)
{
	// Only ever one sector:
	if (cnt != 1) {
		return false;
	}

	// All sectors are low:
	if (sector > 16) {
		return false;
	}

	++reads;

	_fmemset(data, 0, 512);

	switch (sector) {
	case SECTOR_BOOT:
		return ReadBootSector(data);

	case SECTOR_FAT:
		// Unused.
		return false;

	case SECTOR_DIRECTORY:
		return ReadDirectory(data);

	case SECTOR_SOPWITH1:
		return ReadData(data);

	case SECTOR_SEMAPHOR:
		return ReadSemaphore(data);

	default:
		return false;
	}
}

static bool WriteSemaphore(void far *data)
{
	switch (*((uint8_t far *) data)) {
	case 0xfe:
		locked = false;
		return true;
	case 0xff:
		locked = true;
		return true;
	default:
		return false;
	}
}

static bool WriteData(void far *data)
{
	_fmemcpy(data_bytes, data, 40);

	{ // TODO: Temporary hack.
	struct sop_multio *multio = (void *) data_bytes;

	multio->state[1] = PLAYER_FLYING;
	multio->num_players = 2;
	multio->last_player = 1;
	}
	return true;
}

static bool WriteSector(void far *data, uint32_t sector, uint16_t cnt)
{
	// Only ever one sector:
	if (cnt != 1) {
		return false;
	}

	++writes;

	switch (sector) {
	case SECTOR_SEMAPHOR:
		return WriteSemaphore(data);
	case SECTOR_SOPWITH1:
		return WriteData(data);
	default:
		return false;
	}
}

// Int 13h: BIOS interrupt call
static void interrupt far Int13(union INTPACK ip)
{
	++int13_count;

	// ah=0 -> DISK - RESET DISK SYSTEM
	// al=1 -> Drive B:
	if (ip.h.ah == 0 && ip.h.dl == 1) {
		ip.w.flags &= ~INTR_CF;
		ip.h.ah = 0;  // success
		return;
	}

	// ah=2 -> DISK - READ SECTOR(S) INTO MEMORY
	// al=1 -> Drive B:
	if (ip.h.ah == 2 && ip.h.dl == 1) {
		// BIOS sector numbers are indexed from 1.
		uint32_t sector = ((uint32_t) ip.x.cx - 1UL)
			| (((uint32_t) ip.h.dh) << 16);
		bool success = ReadSector(
			MK_FP(ip.x.es, ip.x.bx), sector, ip.h.al);
		if (success) {
			ip.w.flags &= ~INTR_CF;
			ip.h.ah = 0;  // success
			ip.h.al = 1;  // 1 sector transferred
		} else {
			ip.w.flags |= INTR_CF;
			ip.h.ah = 0x20;
			ip.h.al = 0;
		}
		return;
	}

	// ah=3 -> DISK - WRITE DISK SECTOR(S)
	// al=1 -> Drive B:
	if (ip.h.ah == 3 && ip.h.dl == 1) {
		uint32_t sector = ((uint32_t) ip.x.cx - 1UL)
			| (((uint32_t) ip.h.dh) << 16);
		bool success = WriteSector(
			MK_FP(ip.x.es, ip.x.bx), sector, ip.h.al);
		if (success) {
			ip.w.flags &= ~INTR_CF;
			ip.h.ah = 0;  // success
			ip.h.al = 1;  // 1 sector written
		} else {
			ip.w.flags |= INTR_CF;
			ip.h.ah = 0x20;
			ip.h.al = 0;
		}
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

// DOS interrupts 25h and 26h leave the FLAGS register on the stack and
// it is the job of the caller to clean it up. So here we have a special
// modified version of the Watcom interrupt return sequence that does a
// retf instead of iret.
extern void PopAndReturn();
#pragma aux PopAndReturn = \
    "sti" \
    "pop ax" \
    "pop ax" \
    "pop es" \
    "pop ds" \
    "pop di" \
    "pop si" \
    "pop bp" \
    "pop bx" \
    "pop bx" \
    "pop dx" \
    "pop cx" \
    "pop ax" \
    "retf"

// Int 25h: DOS 1+ - ABSOLUTE DISK READ
static void interrupt far Int25(union INTPACK ip)
{
	++int25_count;
	_dos_setvect(0x13, Int13);

	// al=1 -> Drive B:
	if (ip.h.al == 1) {
		bool success = ReadSector(
			MK_FP(ip.x.ds, ip.x.bx), ip.x.dx, ip.x.cx);
		if (success) {
			ip.w.flags &= ~INTR_CF;
			ip.h.ah = 0;
			ip.h.al = 0;
		} else {
			ip.w.flags |= INTR_CF;
			ip.h.ah = 0x20;
			ip.h.al = 0x0c;  // general failure
		}
		goto pop_and_return;
	}

	_chain_intr(old_int25);

pop_and_return:
	PopAndReturn();
}

// Int 26h: DOS 1+ - ABSOLUTE DISK WRITE
static void interrupt far Int26(union INTPACK ip)
{
	++int26_count;
	_dos_setvect(0x13, Int13);

	// al=1 -> Drive B:
	if (ip.h.al == 1) {
		bool success = WriteSector(
			MK_FP(ip.x.ds, ip.x.bx), ip.x.dx, ip.x.cx);
		if (success) {
			ip.w.flags &= ~INTR_CF;
			ip.h.ah = 0;
			ip.h.al = 0;
		} else {
			ip.w.flags |= INTR_CF;
			ip.h.ah = 0x20;
			ip.h.al = 0x0c;  // general failure
		}
		goto pop_and_return;
	}

	_chain_intr(old_int26);

pop_and_return:
	PopAndReturn();
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
		printf(" writes:       %7d\n", writes);
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
