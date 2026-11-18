/*
 * N84 - LCD panel (ST7789-like) model for the CEmu-derived TI-84 Plus CE machine.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * N84 draws frames straight from the LCD controller's frame buffer (CEmu's
 * non-DMA path), so the panel's frame memory is not emulated. The one panel
 * setting that changes what the user sees is MADCTL's MV bit (row/column
 * exchange), so that is tracked:
 *
 *  - The OS leaves MADCTL at 0x08 (MV clear): the frame buffer is row-major,
 *    320 pixels per row.
 *  - Programs that use the lcddrvce library can set MV (MADCTL ^ MV = 0x28)
 *    to get a column-major frame buffer instead: 240 pixels per column, 320
 *    columns, with pixel (x, y) at x * 240 + y. The picture on the screen is
 *    the same way up; only the memory layout differs.
 *
 * (Measured on a calculator running Buckshot Roulette: the home screen reads
 * MADCTL 0x08, the game 0x28.)
 *
 * All other commands are accepted and ignored, and reads return zero. The
 * screen menu can override the detection by hand.
 */
#ifndef N84_CE_PANEL_H
#define N84_CE_PANEL_H
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define PANEL_MADCTL_MV 0x20
#define PANEL_MADCTL_OS 0x08   /* what the OS writes: BGR, MV clear */

/* How the frame buffer is read. AUTO follows what the program does to the panel
 * (see panel_transposed); the other two are the user's override from the menu. */
enum { PANEL_LAYOUT_AUTO, PANEL_LAYOUT_NORMAL, PANEL_LAYOUT_SIDEWAYS, PANEL_LAYOUT_COUNT };
extern int panel_layout;

typedef struct panel_state {
    /* kept for the CEmu-derived code that still sets them */
    bool skipFrame, gammaDirty, accurateGamma;
    /* Saved state (see panel_save): */
    uint8_t madctl;      /* last parameter of MADCTL (36h) */
    /* Not saved: */
    uint8_t cmd;         /* command whose parameters are arriving */
    uint8_t paramIdx;    /* number of parameters received for `cmd` */
    uint8_t sideways;    /* MV is set: column-major frame buffer */
    uint32_t frames;     /* 9-bit frames received (diagnostics, shown in the menu) */
} panel_state_t;
extern panel_state_t panel;

void panel_reset(void);          /* power-on / reset state */
void panel_hw_reset(void);       /* reset line toggled through the control port */
static inline void panel_vsync(void) {}
static inline void init_panel(void) {}

/* The state of a panel the OS has set up (MADCTL 0x08, MV clear). Also what
 * save states that predate the panel model get. */
void panel_set_os_default(void);

/* True when the frame buffer is column-major and must be transposed: the
 * program set MV, or the user forced it from the menu. */
static inline bool panel_transposed(void) {
    return panel_layout == PANEL_LAYOUT_AUTO ? panel.sideways != 0 : panel_layout == PANEL_LAYOUT_SIDEWAYS;
}

bool panel_save(FILE *image);
/* Returns false on a truncated block; a missing block (end of file) restores
 * the OS default and succeeds, so older save states keep loading. */
bool panel_restore(FILE *image);

void panel_spi_select(bool low);
uint8_t panel_spi_peek(uint32_t *rxData);
uint8_t panel_spi_transfer(uint32_t txData, uint32_t *rxData);
#endif
