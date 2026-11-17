/*
 * N84 - LCD panel (ST7789-like) model for the CEmu-derived TI-84 Plus CE machine.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * N84 draws frames straight from the LCD controller's frame buffer (CEmu's
 * non-DMA path), so the panel's frame memory is not emulated. The one panel
 * setting that changes what the user sees is MADCTL's MV bit (row/column
 * exchange), so that is tracked:
 *
 *  - The panel is natively a 240x320 portrait display. The OS sets MV, which
 *    makes the controller's 320x240 raster land on it as a landscape picture:
 *    the frame buffer is row-major, 320 pixels per row.
 *  - Programs that use the lcddrvce library can clear MV (and use a 240x320
 *    window) to get a column-major frame buffer instead: 240 pixels per
 *    column, 320 columns, with pixel (x, y) at x * 240 + y. The picture on the
 *    screen is the same way up; only the memory layout differs.
 *
 * All other commands are accepted and ignored, and reads return zero.
 */
#ifndef N84_CE_PANEL_H
#define N84_CE_PANEL_H
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define PANEL_MADCTL_MV 0x20

typedef struct panel_state {
    /* kept for the CEmu-derived code that still sets them */
    bool skipFrame, gammaDirty, accurateGamma;
    /* Saved state (see panel_save): */
    uint8_t madctl;      /* last parameter of MADCTL (36h) */
    uint8_t cmd;         /* command whose parameters are arriving */
    uint8_t paramIdx;    /* number of parameters received for `cmd` */
} panel_state_t;
extern panel_state_t panel;

void panel_reset(void);          /* power-on / reset state: MADCTL = 0 */
void panel_hw_reset(void);       /* reset line toggled through the control port */
static inline void panel_vsync(void) {}
static inline void init_panel(void) {}

/* State after a reset of the OS orientation, for save states that predate the
 * panel model (they can only have been made in the OS's landscape layout). */
void panel_set_os_default(void);

/* True when the frame buffer is column-major (MV cleared) and must be
 * transposed to get the picture. */
static inline bool panel_transposed(void) { return !(panel.madctl & PANEL_MADCTL_MV); }

bool panel_save(FILE *image);
/* Returns false on a truncated block; a missing block (end of file) restores
 * the OS default and succeeds, so older save states keep loading. */
bool panel_restore(FILE *image);

void panel_spi_select(bool low);
uint8_t panel_spi_peek(uint32_t *rxData);
uint8_t panel_spi_transfer(uint32_t txData, uint32_t *rxData);
#endif
