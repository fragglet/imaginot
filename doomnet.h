//
// Copyright(C) 1993 id Software, Inc.
// Copyright(C) 2019-2024 Simon Howard
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

#include <stdint.h>

#define CMD_SEND    1
#define CMD_GET     2

#define DOOMCOM_ID      0x12345678l

typedef struct {
    long id;
    short intnum;               // DOOM executes an int to send commands

    // communication between DOOM and the driver
    short command;              // CMD_SEND or CMD_GET
    short remotenode;           // dest for send, set by get (-1 = no packet)
    short datalength;           // bytes in doomdata to be sent / bytes read

    // info common to all nodes
    short numnodes;             // console is allways node 0
    short ticdup;               // 1 = no duplication, 2-5 = dup for slow nets
    short extratics;            // 1 = send a backup tic in every packet
    short deathmatch;           // 1 = deathmatch
    short savegame;             // -1 = new game, 0-5 = load savegame
    short episode;              // 1-3
    short map;                  // 1-9
    short skill;                // 1-5

    // info specific to this node
    short consoleplayer;        // 0-3 = player number
    short numplayers;           // 1-4
    short angleoffset;          // 1 = left, 0 = center, -1 = right
    short drone;                // 1 = drone

    // packet data to be sent
    uint8_t data[512];
} doomcom_t;

#define BACKUPTICS 12

#define NCMD_EXIT       0x80000000l
#define NCMD_RETRANSMIT 0x40000000l
#define NCMD_SETUP      0x20000000l
#define NCMD_KILL       0x10000000l     // kill game
#define NCMD_CHECKSUM   0x0fffffffl

doomcom_t far *NetGetHandle(long l);
void NetSendPacket(doomcom_t far *doomcom);
int NetGetPacket(doomcom_t far *doomcom);

