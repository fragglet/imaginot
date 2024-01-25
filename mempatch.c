
#include <stdbool.h>
#include <stdlib.h>
#include <i86.h>

#include "mempatch.h"
#include "doomnet.h"
#include "protocol.h"

#define arrlen(x) (sizeof(x) / sizeof(*(x)))

int no_mem_patch = 0;

// This patch moves the oxen out of the warzone. This is important because
// the positions of the oxen are right next to the player 3 and 4 start
// positions, which makes taking off very difficult.

// From initoxen() in swinit.c in the Sopwith source code:
// > static   iox[] = { 1376, 1608 };
// > static   ioy[] = { 80,   91   };
static uint8_t move_oxen_old[] = {
    0x60, 0x05, 0x48, 0x06, 0x50, 0x00, 0x5b, 0x00,
};
// We move them to:
//  (1080, 42) - flat ground left of the cyan base
//  (1608, 42) - flat ground right of the magenta base:
static uint8_t move_oxen_new[] = {
    0x38, 0x04, 0xb2, 0x07, 0x2a, 0x00, 0x2a, 0x00,
};


// This patch increases the time before a plane repawns, from 10 tics to 16
// tics. The reason is that with the default of 10 tics, when a plane crashes
// in its start position, the debris can hit the respawned plane and cause it
// to crash again. This is particularly unfair in multiplayer where you can
// be attacked by another player while sitting on the runway.

// > 0000:222c 8b 76 fe    MOV SI,word ptr [BP + local_4]
// > 0000:222f 89 44 06    MOV word ptr [SI + 0x6],AX
// > 0000:2232 b8 0a 00    MOV AX,0xa
//                \_ 10 = MAXCRCOUNT, we change to 16
// > 0000:2235 8b 76 fe    MOV SI,word ptr [BP + local_4]
// > 0000:2238 89 44 1a    MOV word ptr [SI + 0x1a],AX
// > 0000:223b 8b e5       MOV SP,BP
// > 0000:223d 5d          POP BP
// > 0000:223e c3          RET

static uint8_t respawn_time_old[] = {
    0x8b, 0x76, 0xfe, 0x89, 0x44, 0x06, 0xb8, 0x0a, 0x00, 0x8b,
    0x76, 0xfe, 0x89, 0x44, 0x1a, 0x8b, 0xe5, //^^
};
static uint8_t respawn_time_new[] = {
    0x8b, 0x76, 0xfe, 0x89, 0x44, 0x06, 0xb8, 0x10, 0x00, 0x8b,
    0x76, 0xfe, 0x89, 0x44, 0x1a, 0x8b, 0xe5, //^^
};

static void ApplyPatch(uint8_t far *buf, size_t buf_len, uint8_t *old_data,
                       uint8_t *new_data, size_t sz)
{
    unsigned int i, j, match_len = 0;

    for (i = 0; i < buf_len; i++)
    {
        if (buf[i] != old_data[match_len])
        {
            i -= match_len;
            match_len = 0;
        }
        else if (match_len + 1 >= sz)
        {
            i -= match_len;
            break;
        }
        else
        {
            ++match_len;
        }
    }

    if (i >= buf_len)
    {
        // No match.
        return;
    }

    for (j = 0; j < sz; j++)
    {
        buf[i] = new_data[j];
        ++i;
    }
}

void ApplyPatches(uint16_t segment)
{
    uint8_t far *buf = MK_FP(segment, 0);

    if (no_mem_patch)
    {
        return;
    }

    // We only need to move the oxen if there are more than two players;
    // otherwise there is nothing they are getting in the way of.
    if (num_players > 2)
    {
        ApplyPatch(buf, 0xffff, move_oxen_old, move_oxen_new,
                   arrlen(move_oxen_old));
    }

    ApplyPatch(buf, 0xffff, respawn_time_old, respawn_time_new,
               arrlen(respawn_time_old));
}

