//
// Copyright(C) 2024 Simon Howard
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

#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "lib/ints.h"

#include "doomnet.h"
#include "fakedisk.h"
#include "mempatch.h"
#include "protocol.h"

#pragma pack(push, 1)

// We use a hard-coded random seed for the game; it doesn't really
// matter since it makes little difference to the game.
#define EXPL_RANDOM_SEED  0x84

// Sopwith only allows games 0...7
#define NUM_GAMES 8

enum state
{
    // Providing the data for init1mul(), init2mul()
    STATE_INIT,

    // We are waiting for Sopwith to read the data sector again to get
    // other players' commands. In this state we pretend that players
    // 0...consoleplayer-1 have all written their commands. If
    // consoleplayer == 0, this read might not happen.
    STATE_WAITING_READ,

    // We are waiting for Sopwith to write its next command.
    STATE_WAITING_WRITE,
};

enum
{
    PLAYER_WAITING = 0,
    PLAYER_FLYING = 1,
    PLAYER_FINISHED = 91,
};

struct bpb
{
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

struct fat_dirent
{
    char name[11];
    uint8_t attr;
    uint8_t res[10];
    uint16_t time;
    uint16_t date;
    uint16_t cluster;
    uint32_t size;
};

// Format of the Sopwith communications buffer (sopwith1.dta):
struct sop_multio
{
    uint8_t max_players;
    uint8_t num_players;
    uint8_t last_player;
    uint16_t key[4];
    uint8_t state[4];
    uint8_t explseed;
};

static void (interrupt far *old_int13)();
static void (interrupt far *old_int25)();
static void (interrupt far *old_int26)();

static bool locked = false;

static bool hooked = false;
static int int13_count = 0;
static int int25_count = 0;
static int int26_count = 0;
static int reads = 0, writes = 0;
static enum state state = STATE_INIT;

static struct sop_multio multio;

//
// Emulated floppy disk:
// Imaginet worked by sharing a floppy drive at the raw, hardware level.
// Sopwith players exchanged data by writing to a particular sector on
// the disk. So we emulate a simple DOS drive and barebones FAT12:
//
//
enum
{
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
    int i;

    switch (state)
    {
    case STATE_INIT:
        // Sopwith is reading to assign itself a player number.
        // To do this, we pretend that a number of players have already
        // registered themselves so that it will get the right number.
        multio.max_players = num_players;
        for (i = 0; i < num_players; i++)
        {
            multio.state[i] =
                i < consoleplayer ? PLAYER_FLYING : PLAYER_WAITING;
        }
        // This is slightly counterintuitive, because we'd expect to set
        // .num_players to consoleplayer. But the num_players field isn't
        // actually used by Sopwith for much other than identifying which
        // is the "first player" that prompts for the number of players
        // and initializes the shared data file. In our case we already
        // know how many players there are and there is no real data file
        // to be initialized. So we just set to 1 so that the game uses
        // the values we have already set.
        multio.num_players = 1;

        // To keep the game synchronized between peers, it is essential
        // that they all use the same random seed. This is the other
        // reason we set .num_players = 1 above; if num_players=0, the
        // game will generate a new random seed that doesn't match the
        // other peers.
        multio.explseed = EXPL_RANDOM_SEED;
        break;

    case STATE_WAITING_READ:
        // We signal that it's "your turn".
        multio.last_player = (consoleplayer + num_players - 1) % num_players;
        state = STATE_WAITING_WRITE;
        break;

    case STATE_WAITING_WRITE:
        break;
    }

    _fmemcpy(data, &multio, sizeof(struct sop_multio));
    return true;
}

static bool ReadSector(void far *data, uint32_t sector, uint16_t cnt)
{
    // Only ever one sector:
    if (cnt != 1)
    {
        return false;
    }

    // All sectors are low:
    if (sector > 16)
    {
        return false;
    }

    ++reads;

    _fmemset(data, 0, 512);

    switch (sector)
    {
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
    switch (*((uint8_t far *) data))
    {
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
    static uint16_t cmd_buf[MAX_PLAYERS];
    int i;

    _fmemcpy(&multio, data, sizeof(struct sop_multio));

    switch (state)
    {
    case STATE_INIT:
        // The first write by Sopwith is to increment the player count
        // and set its state to FLYING, which it does to share with
        // peers that it is here and ready. Once this happens, we can
        // simulate all the other players being ready too.
        for (i = 0; i < num_players; i++)
        {
            multio.state[i] = PLAYER_FLYING;
        }
        multio.num_players = num_players;
        multio.last_player = num_players - 1;
        state = STATE_WAITING_READ;
        break;

    case STATE_WAITING_READ:
        // The read step doesn't happen here if consoleplayer == 0, so
        // we just fall straight through to the write stage.

    case STATE_WAITING_WRITE:
        // The commands from other players might not have all arrived
        // yet, in which case we kick the ball back to Sopwith by
        // simulating a failed write; it will try again after a delay.
        if (!SwapCommand(multio.key[consoleplayer], cmd_buf))
        {
            return false;
        }
        for (i = 0; i < num_players; i++)
        {
            multio.key[i] = cmd_buf[i];
        }
        multio.last_player = num_players - 1;
        state = STATE_WAITING_READ;
        break;
    }

    // As a slightly underhanded move, we also do a copy back into the
    // buffer that we read from. This allows Sopwith to skip an extra
    // read call during multput() - ie. updated() will return straight
    // away and not perform any delay.
    _fmemcpy(data, &multio, sizeof(struct sop_multio));

    return true;
}

static bool WriteSector(void far *data, uint32_t sector, uint16_t cnt)
{
    // Only ever one sector:
    if (cnt != 1)
    {
        return false;
    }

    ++writes;

    switch (sector)
    {
    case SECTOR_SEMAPHOR:
        return WriteSemaphore(data);
    case SECTOR_SOPWITH1:
        return WriteData(data);
    default:
        return false;
    }
}

// Int 13h: BIOS interrupt call
static bool RealInt13(union INTPACK far *ip)
{
    ReceivePackets();
    ++int13_count;

    // ah=0 -> DISK - RESET DISK SYSTEM
    // al=1 -> Drive B:
    if (ip->h.ah == 0 && ip->h.dl == 1)
    {
        ip->w.flags &= ~INTR_CF;
        ip->h.ah = 0;  // success
        return true;
    }

    // ah=2 -> DISK - READ SECTOR(S) INTO MEMORY
    // al=1 -> Drive B:
    if (ip->h.ah == 2 && ip->h.dl == 1)
    {
        // BIOS sector numbers are indexed from 1.
        uint32_t sector = ((uint32_t) ip->x.cx - 1UL)
            | (((uint32_t) ip->h.dh) << 16);
        bool success = ReadSector(
            MK_FP(ip->x.es, ip->x.bx), sector, ip->h.al);
        if (success)
        {
            ip->w.flags &= ~INTR_CF;
            ip->h.ah = 0;  // success
            ip->h.al = 1;  // 1 sector transferred
        }
        else
        {
            ip->w.flags |= INTR_CF;
            ip->h.ah = 0x20;
            ip->h.al = 0;
        }
        return true;
    }

    // ah=3 -> DISK - WRITE DISK SECTOR(S)
    // al=1 -> Drive B:
    if (ip->h.ah == 3 && ip->h.dl == 1)
    {
        uint32_t sector = ((uint32_t) ip->x.cx - 1UL)
            | (((uint32_t) ip->h.dh) << 16);
        bool success = WriteSector(
            MK_FP(ip->x.es, ip->x.bx), sector, ip->h.al);
        if (success)
        {
            ip->w.flags &= ~INTR_CF;
            ip->h.ah = 0;  // success
            ip->h.al = 1;  // 1 sector written
        }
        else
        {
            ip->w.flags |= INTR_CF;
            ip->h.ah = 0x20;
            ip->h.al = 0;
        }
        return true;
    }

    // Chain to old handler.
    return false;
}

// Int 13h: BIOS interrupt call
static void interrupt far Int13(union INTPACK ip)
{
    static union INTPACK far *ip_copy;
    static bool chain;
    ip_copy = &ip;

    SWITCH_ISR_STACK;
    chain = !RealInt13(ip_copy);
    RESTORE_ISR_STACK;

    if (chain)
    {
        _chain_intr(old_int13);
    }
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

    // The first time this interrupt is invoked, we call ApplyPatches()
    // to make some small changes to Sopwith's memory segment.
    {
        static bool applied_patches = false;
        if (!applied_patches)
        {
            ApplyPatches(ip.x.cs);
            applied_patches = true;
        }
    }

    // al=1 -> Drive B:
    if (ip.h.al == 1)
    {
        bool success = ReadSector(
            MK_FP(ip.x.ds, ip.x.bx), ip.x.dx, ip.x.cx);
        if (success)
        {
            ip.w.flags &= ~INTR_CF;
            ip.h.ah = 0;
            ip.h.al = 0;
        }
        else
        {
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
    if (ip.h.al == 1)
    {
        bool success = WriteSector(
            MK_FP(ip.x.ds, ip.x.bx), ip.x.dx, ip.x.cx);
        if (success)
        {
            ip.w.flags &= ~INTR_CF;
            ip.h.ah = 0;
            ip.h.al = 0;
        }
        else
        {
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

void RestoreDiskInterrupts(void)
{
    if (hooked)
    {
        _dos_setvect(0x13, old_int13);
        _dos_setvect(0x25, old_int25);
        _dos_setvect(0x26, old_int26);
        hooked = false;
        printf("Normal interrupts restored.\n");
        printf(" int 13 calls: %7d\n", int13_count);
        printf(" int 25 calls: %7d\n", int25_count);
        printf(" int 26 calls: %7d\n", int26_count);
        printf(" reads:        %7d\n", reads);
        printf(" writes:       %7d\n", writes);
    }
}

void HookDiskInterrupts(void)
{
    if (!hooked)
    {
        old_int13 = _dos_getvect(0x13);
        _dos_setvect(0x13, Int13);
        old_int25 = _dos_getvect(0x25);
        _dos_setvect(0x25, Int25);
        old_int26 = _dos_getvect(0x26);
        _dos_setvect(0x26, Int26);
        hooked = true;
        printf("Interrupts hooked.\n");
    }
}
