
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>

#include "doomnet.h"
#include "protocol.h"

#define MAX_DELAY 16
#define DELAY  4  /* currently hard-coded */

struct window {
    uint32_t start;
    uint8_t len;
    uint16_t cmds[MAX_DELAY];
};

struct node {
    struct window send_window;
    struct window recv_window;
    int player;
};

static doomcom_t far *doomcom;
static struct packet far *pkt;  // points into doomcom->data
static struct node nodes[MAX_PLAYERS];
static uint32_t maketic, gametic;
static int num_nodes = 2;

void InitProtocol(doomcom_t far *dc)
{
    int i;

    doomcom = dc;
    pkt = (struct packet far *) doomcom->data;

    assert(doomcom->numnodes > 1 && doomcom->numnodes < MAX_PLAYERS
        && doomcom->numplayers > 1 && doomcom->numplayers < MAX_PLAYERS
        && doomcom->consoleplayer >= 0
        && doomcom->consoleplayer < doomcom->numplayers);

    num_nodes = doomcom->numnodes;
    nodes[0].player = doomcom->consoleplayer;

    for (i = 0; i < MAX_PLAYERS; i++)
    {
        nodes[i].player = -1;
        nodes[i].send_window.start = 0;

        // We start the game with a number of tics already in
        // the pipeline.
        nodes[i].recv_window.len = DELAY - 1;
    }

    maketic = DELAY - 1;
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
    int i, new_len;

    if (maketic - gametic >= MAX_DELAY)
    {
        return false;
    }

    for (i = 0; i < num_nodes; i++)
    {
        new_len = maketic - nodes[i].send_window.start;
        if (new_len > nodes[i].send_window.len)
        {
            nodes[i].send_window.cmds[new_len] = cmd;
            ++nodes[i].send_window.len;
        }
        // TODO: Send packet
    }

    if (!NewTicReady())
    {
        return false;
    }

    // We have received the next command from every other node, so we
    // are now ready for the game to run another tic.

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


