
#include <stdlib.h>
#include <i86.h>

#include "mempatch.h"

#define arrlen(x) (sizeof(x) / sizeof(*(x)))

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

    ApplyPatch(buf, 0xffff, move_oxen_old, move_oxen_new,
               arrlen(move_oxen_old));
}

