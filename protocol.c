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

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <time.h>

#include "crc32.h"
#include "doomnet.h"
#include "protocol.h"

#define MAX_DELAY 16
#define DELAY  4  /* currently hard-coded */

#define MIN_PACKET_LEN 14

#pragma pack(push, 1)
struct packet {
    uint32_t checksum;
    uint32_t start;
    uint32_t ack;
    uint8_t player;
    uint8_t num_cmds;
    uint16_t cmds[MAX_DELAY];
};
#pragma pack(pop)

struct window {
    uint32_t start;
    uint8_t len;
    uint16_t cmds[MAX_DELAY];
};

struct node {
    struct window send_window;
    struct window recv_window;
    int player;
    time_t last_send_time;
};

static doomcom_t far *doomcom;
static struct packet far *pkt;  // points into doomcom->data
static struct node nodes[MAX_PLAYERS];
static uint32_t maketic, gametic;
static int num_nodes = 2;
int fake_players = 0;
int consoleplayer, num_players;

void InitProtocol(doomcom_t far *dc)
{
    int i;

    doomcom = dc;
    pkt = (struct packet far *) doomcom->data;

    assert(doomcom->numnodes > 1 && doomcom->numnodes <= MAX_PLAYERS
        && doomcom->numplayers > 1 && doomcom->numplayers <= MAX_PLAYERS
        && doomcom->consoleplayer >= 0
        && doomcom->consoleplayer < doomcom->numplayers);

    maketic = DELAY;

    for (i = 0; i < MAX_PLAYERS; i++)
    {
        nodes[i].player = -1;
        nodes[i].send_window.start = 0;
        nodes[i].send_window.len = DELAY - 1;
        nodes[i].recv_window.len = 0;
    }

    num_nodes = doomcom->numnodes;
    nodes[0].player = doomcom->consoleplayer;
    consoleplayer = doomcom->consoleplayer;
    num_players = fake_players ? MAX_PLAYERS : doomcom->numplayers;
}

static uint32_t Checksum(void)
{
    return Crc32(doomcom->data + 4, doomcom->datalength - 4)
         & NCMD_CHECKSUM;
}

static void SendPacket(struct node *dest)
{
    pkt->start = dest->send_window.start;
    pkt->ack = dest->recv_window.start + dest->recv_window.len;
    pkt->player = nodes[0].player;
    pkt->num_cmds = dest->send_window.len;
    _fmemmove(pkt->cmds, dest->send_window.cmds,
              dest->send_window.len * sizeof(uint16_t));

    doomcom->command = CMD_SEND;
    doomcom->remotenode = dest - nodes;
    doomcom->datalength = sizeof(struct packet)
                        - (MAX_DELAY - pkt->num_cmds) * sizeof(uint16_t);
    pkt->checksum = Checksum();

    NetSendPacket(doomcom);
    dest->last_send_time = time(NULL);
}

// Returns true if there is at least one tic working in the receive
// window from every player.
static bool NewTicReady(void)
{
    bool result = true;
    int i;

    for (i = 0; i < num_nodes; i++)
    {
        result = result && nodes[i].recv_window.len > 0;
    }

    return result;
}

bool SwapCommand(uint16_t cmd, uint16_t cmds[MAX_PLAYERS])
{
    struct window *send_window;
    int i, new_len;

    if (maketic - gametic >= MAX_DELAY)
    {
        return false;
    }

    for (i = 0; i < num_nodes; i++)
    {
        send_window = i == 0 ? &nodes[0].recv_window : &nodes[i].send_window;
        new_len = maketic - send_window->start;
        if (new_len > send_window->len)
        {
            send_window->cmds[new_len - 1] = cmd;
            send_window->len = new_len;
            if (i != 0)
            {
                SendPacket(&nodes[i]);
            }
        }
    }

    while (!NewTicReady())
    {
        ReceivePackets();
        // TODO: We should eventually time out.
    }

    // We have received the next command from every other node, so we
    // are now ready for the game to run another tic.

    for (i = 0; i < MAX_PLAYERS; i++)
    {
        cmds[i] = 0;
    }

    for (i = 0; i < num_nodes; i++)
    {
        int p = nodes[i].player;
        if (p == -1)
        {
            continue;
        }

        // We consume the first command from the receive window.
        cmds[p] = nodes[i].recv_window.cmds[0];
        ++nodes[i].recv_window.start;
        --nodes[i].recv_window.len;
        memmove(nodes[i].recv_window.cmds,
                nodes[i].recv_window.cmds + 1,
                sizeof(uint16_t) * nodes[i].recv_window.len);
    }

    ++maketic;
    ++gametic;

    return true;
}

// Process the packet in 'pkt' as being received from the given node.
static void ReceivePacket(struct node *n)
{
    uint32_t start_index, i;

    if (doomcom->datalength < MIN_PACKET_LEN
     || pkt->checksum != Checksum()
     || pkt->player >= MAX_PLAYERS || pkt->num_cmds > MAX_DELAY)
    {
        return;
    }

    // We can use the ack field from the peer to discard commands at the
    // start of the window that are confirmed to have been received.
    if (pkt->ack > n->send_window.start
     && pkt->ack <= n->send_window.start + n->send_window.len)
    {
        int cnt = pkt->ack - n->send_window.start;
        memmove(n->send_window.cmds, n->send_window.cmds + cnt,
                (n->send_window.len - cnt) * sizeof(uint16_t));
        n->send_window.start += cnt;
        n->send_window.len -= cnt;
    }

    // Old packet?
    if (pkt->start < n->recv_window.start)
    {
        return;
    }

    // Offset of new cmds into recv_window:
    start_index = pkt->start - n->recv_window.start;

    for (i = 0; i < pkt->num_cmds; i++)
    {
        if (start_index + i >= MAX_DELAY)
        {
            break;
        }

        n->recv_window.cmds[start_index + i] = pkt->cmds[i];
        n->recv_window.len = max(n->recv_window.len,
                                 start_index + i + 1);
    }

    n->player = pkt->player;
}

static void CheckResends(void)
{
    time_t now = time(NULL);
    int i;

    for (i = 1; i < num_nodes; i++)
    {
        if (now - nodes[i].last_send_time > 0)
        {
            SendPacket(&nodes[i]);
        }
    }
}

void ReceivePackets(void)
{
    CheckResends();

    while (NetGetPacket(doomcom))
    {
        assert(doomcom->remotenode < num_nodes);

        ReceivePacket(&nodes[doomcom->remotenode]);
    }
}

