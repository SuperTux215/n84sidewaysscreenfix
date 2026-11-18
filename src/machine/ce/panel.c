/*
 * N84 - LCD panel model (see panel.h)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "panel.h"

#include <stddef.h>
#include <string.h>

panel_state_t panel;
int panel_layout = PANEL_LAYOUT_AUTO;

#define PANEL_CMD_SWRESET 0x01
#define PANEL_CMD_MADCTL  0x36

#define PANEL_PARAM_BIT   0x100   /* bit 8 of the 9-bit SPI frame: 1 = parameter, 0 = command */

static void set_madctl(uint8_t v) {
    panel.madctl = v;
    if (v & PANEL_MADCTL_MV) panel.seen_mv = 1;
    panel.sideways = panel.seen_mv && !(v & PANEL_MADCTL_MV);
}

void panel_reset(void) {
    panel.cmd = 0;
    panel.paramIdx = 0;
    panel.madctl = 0;               /* ST7789 reset value */
    panel.seen_mv = 0;
    panel.sideways = 0;
}

void panel_hw_reset(void) { panel_reset(); }

void panel_set_os_default(void) {
    panel_reset();
    set_madctl(PANEL_MADCTL_MV);
}

/*
 * The panel is not selected/deselected around each command by every program,
 * so chip select does not abort a command here: a command is simply followed
 * by its parameters until the next command arrives.
 */
void panel_spi_select(bool low) { (void)low; }

/* 9-bit frames, as CEmu */
uint8_t panel_spi_peek(uint32_t *rxData) { *rxData = 0; return 9; }

uint8_t panel_spi_transfer(uint32_t txData, uint32_t *rxData) {
    /* the SPI controller passes its whole shift register: keep the 9-bit frame */
    txData &= 0x1FF;
    panel.frames++;
    uint8_t byte = (uint8_t)txData;
    if (txData & PANEL_PARAM_BIT) {
        if (panel.cmd == PANEL_CMD_MADCTL && panel.paramIdx == 0) set_madctl(byte);
        if (panel.paramIdx != 0xFF) panel.paramIdx++;
    } else {
        panel.cmd = byte;
        panel.paramIdx = 0;
        if (byte == PANEL_CMD_SWRESET) panel_reset();
    }
    *rxData = 0;
    return 9;
}

bool panel_save(FILE *image) {
    uint8_t b[2] = { panel.madctl, panel.seen_mv };
    return fwrite(b, 1, 2, image) == 2;
}

bool panel_restore(FILE *image) {
    uint8_t b[2];
    size_t n = fread(b, 1, 2, image);
    if (n == 2) {
        panel_reset();
        panel.seen_mv = b[1];
        set_madctl(b[0]);
        panel.seen_mv |= b[1];
        panel.sideways = panel.seen_mv && !(b[0] & PANEL_MADCTL_MV);
        return true;
    }
    if (n == 1) {                    /* an earlier test build wrote only MADCTL */
        panel_reset();
        set_madctl(b[0]);
        return true;
    }
    panel_set_os_default();          /* state file from before the panel model */
    return feof(image) != 0;
}
