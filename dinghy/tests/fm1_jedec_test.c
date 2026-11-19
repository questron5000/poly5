/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-1's two flash chips are both accepted by the app's storage and the update loader (one macro, FL_JEDEC_OK,
 * hal/fm1_flash_map.h): the Puya P25Q80 of older units and the UC25HQ80 of newer ones. Anything else is refused,
 * so the loader writes nothing and the app keeps its settings in RAM. */
#include <stdio.h>
#include <stdint.h>
#include "fm1_flash_map.h"

static int check(const char *what, int ok) { printf("%-74s %s\n", what, ok ? "ok" : "FAILED"); return !ok; }

int main(void)
{
    int bad = 0;
    bad += check("older FM-1: Puya P25Q80 (0x856014) accepted", FL_JEDEC_OK(0x856014u));
    bad += check("newer FM-1: UC25HQ80 (0xB36014) accepted", FL_JEDEC_OK(0xB36014u));
    bad += check("SMK-37's part (0xEB6014) refused on the FM-1", !FL_JEDEC_OK(0xEB6014u));
    bad += check("no answer (0x000000, 0xFFFFFF) refused", !FL_JEDEC_OK(0u) && !FL_JEDEC_OK(0xFFFFFFu));
    bad += check("a 2 MiB Puya (0x856015) refused", !FL_JEDEC_OK(0x856015u));
    puts(bad ? "jedec test FAILED" : "jedec test passed");
    return bad != 0;
}
