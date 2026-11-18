/*
 * N84 - TI-84 Plus CE panel orientation test (no ROM needed).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Checks that the panel model follows MADCTL's MV bit, that the frame is read
 * as a column-major buffer (screen pixel (x, y) = pixel x * 240 + y) while MV
 * is clear, and that the setting survives save states, including states
 * written before the panel model existed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "ce.h"
#include "lcd.h"
#include "mem.h"
#include "port.h"
#include "panel.h"
#include "spi.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { \
    printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

/* A frame as the SPI controller hands it to the panel: the 9-bit frame in the
 * low bits, whatever was shifted before it above. */
static void send(bool param, uint8_t v) {
    uint32_t rx;
    uint8_t bits = panel_spi_transfer(0xFFFFFE00u | (param ? 0x100u : 0u) | v, &rx);
    CHECK(bits == 9, "panel frames are 9 bits, got %u", bits);
}
static void send_cmd(uint8_t cmd, const uint8_t *p, int n) {
    send(false, cmd);
    for (int i = 0; i < n; i++) send(true, p[i]);
}
static void madctl(uint8_t v) { send_cmd(0x36, &v, 1); }

static void test_panel_commands(void) {
    panel_reset();
    CHECK(!panel_transposed(), "after reset the layout is the OS's");
    madctl(0x48);
    CHECK(!panel_transposed(), "MV clear before MV was ever seen set is NOT taken as sideways (misread safety)");
    madctl(0x68);
    CHECK(!panel_transposed(), "MADCTL 0x68 sets MV");
    madctl(0x48);
    CHECK(panel_transposed(), "MADCTL 0x48 clears MV after it was set: sideways");
    madctl(0xE8);
    CHECK(!panel_transposed(), "MADCTL 0xE8 sets MV again");

    /* other commands and their parameters leave MADCTL alone */
    const uint8_t caset[4] = { 0, 0, 0x01, 0x3F };
    send_cmd(0x2A, caset, 4);
    const uint8_t raset[4] = { 0, 0, 0x00, 0xEF };
    send_cmd(0x2B, raset, 4);
    send_cmd(0x2C, NULL, 0);
    CHECK(!panel_transposed(), "CASET/RASET/RAMWR do not touch MV");
    CHECK(panel.frames > 0, "frames are counted");

    /* only the first MADCTL parameter counts */
    send(false, 0x36); send(true, 0x20); send(true, 0x00);
    CHECK(!panel_transposed(), "extra parameters are ignored");
    /* a parameter with no command before it does not change anything */
    panel.cmd = 0; panel.paramIdx = 0;
    send(true, 0x00);
    CHECK(!panel_transposed(), "stray parameter");

    madctl(0x00);
    CHECK(panel_transposed(), "now sideways");
    send_cmd(0x01, NULL, 0);
    CHECK(!panel_transposed() && panel.madctl == 0 && !panel.seen_mv, "SWRESET goes back to the OS layout");

    madctl(0x20); madctl(0x00);
    CHECK(panel_transposed(), "sideways again");
    panel_hw_reset();
    CHECK(!panel_transposed(), "hardware reset goes back to the OS layout");

    /* the menu override wins over detection */
    panel_reset();
    panel_layout = PANEL_LAYOUT_SIDEWAYS;
    CHECK(panel_transposed(), "forced sideways");
    madctl(0x20);
    CHECK(panel_transposed(), "forced sideways ignores MADCTL");
    panel_layout = PANEL_LAYOUT_NORMAL;
    madctl(0x00);
    CHECK(!panel_transposed(), "forced normal ignores MADCTL");
    panel_layout = PANEL_LAYOUT_AUTO;
}

/* Same thing through the real SPI controller model and the scheduler. */
static void spi_out(uint16_t reg, uint8_t v) { port_write_byte((uint16_t)(0xD000 + reg), v); }
static void spi_frame(bool param, uint8_t v) {
    spi_out(0x19, param ? 1 : 0);          /* the entry is committed by the low byte */
    spi_out(0x18, v);
}
static void test_through_spi(void) {
    panel_reset();
    spi_out(0x06, 0x08);                    /* CR1: 9-bit frames (bits 16-20 = 8), fastest clock */
    spi_out(0x08, 0x01);                    /* CR2: enable */
    spi_out(0x09, 0x01);                    /*      TX enable (bit 8) */
    spi_frame(false, 0x36);
    spi_frame(true, 0x60);                  /* MV set */
    ce_run(1);
    CHECK(!panel_transposed() && panel.seen_mv && panel.madctl == 0x60,
          "MADCTL 0x60 through the SPI controller: madctl=%02X seen=%u", panel.madctl, panel.seen_mv);
    spi_frame(false, 0x36);
    spi_frame(true, 0x00);                  /* MV clear */
    spi_frame(false, 0x2C);
    ce_run(1);
    CHECK(panel_transposed(), "MADCTL 0x00 through the SPI controller: madctl=%02X", panel.madctl);
    spi_out(0x08, 0x00);
}

/* ---- rendering --------------------------------------------------------- */

#define FB_ADDR   0xD40000u
#define FB_OFFSET (FB_ADDR - 0xD00000u)

static void lcd_setup(uint32_t mode, bool enabled) {
    /* LcdEn | mode | BGR (no red/blue swap) | LcdPwr */
    lcd.control = (enabled ? 1u : 0u) | mode << 1 | 1u << 8 | (enabled ? 1u << 11 : 0u);
    lcd.upbase = FB_ADDR;
    lcd_update();
}

static uint16_t *render(unsigned y0, unsigned y1, bool *res) {
    uint16_t *out = malloc(CE_LCD_W * CE_LCD_H * sizeof *out);
    memset(out, 0xA5, CE_LCD_W * CE_LCD_H * sizeof *out);
    bool r = ce_lcd_render_rows(out, y0, y1);
    if (res) *res = r;
    return out;
}

static void test_render_8bpp(void) {
    uint8_t *fb = mem.ram.block + FB_OFFSET;
    for (unsigned i = 0; i < 256; i++) lcd.palettes[0][i] = (uint16_t)(i * 251 + 17);
    for (unsigned i = 0; i < CE_LCD_W * CE_LCD_H; i++) fb[i] = (uint8_t)(i * 7 + i / 251);
    lcd_setup(3, true);

    /* MV set: the OS layout, 320 pixels per row */
    panel_set_os_default();
    uint16_t *a = render(0, CE_LCD_H, NULL);
    unsigned bad = 0;
    for (unsigned y = 0; y < CE_LCD_H; y++)
        for (unsigned x = 0; x < CE_LCD_W; x++)
            bad += a[y * CE_LCD_W + x] != lcd.palettes[0][fb[y * CE_LCD_W + x]];
    CHECK(bad == 0, "8bpp raster layout: %u wrong pixels", bad);

    /* MV clear: column-major, pixel (x, y) at x * 240 + y */
    panel_set_os_default();
    madctl(0x48);
    CHECK(panel_transposed(), "setup: sideways");
    uint16_t *b = render(0, CE_LCD_H, NULL);
    bad = 0;
    for (unsigned y = 0; y < CE_LCD_H; y++)
        for (unsigned x = 0; x < CE_LCD_W; x++)
            bad += b[y * CE_LCD_W + x] != lcd.palettes[0][fb[x * CE_LCD_H + y]];
    CHECK(bad == 0, "8bpp column-major layout: %u wrong pixels", bad);
    CHECK(memcmp(a, b, CE_LCD_W * CE_LCD_H * 2) != 0, "the two layouts must differ for this test pattern");

    /* partial row ranges (the frontend renders the speed box rows separately) */
    uint16_t *c = render(37, 91, NULL);
    CHECK(!memcmp(c + 37 * CE_LCD_W, b + 37 * CE_LCD_W, (91 - 37) * CE_LCD_W * 2), "row range matches full render");
    CHECK(c[36 * CE_LCD_W + 5] == 0xA5A5 && c[91 * CE_LCD_W] == 0xA5A5, "rows outside the range are untouched");
    CHECK(render(5, 5, NULL) != NULL, "empty range is fine");

    /* a short frame buffer (not enough bytes) takes the generic path and must not read out of bounds */
    lcd.data_end = (uint32_t *)((uint8_t *)lcd.data + 4096);
    uint16_t *d = render(0, CE_LCD_H, NULL);
    CHECK(d[0] == lcd.palettes[0][fb[0]], "short buffer: first pixel");
    lcd_update();

    /* LCD off: black, and false */
    lcd_setup(3, false);
    bool res = true;
    uint16_t *e = render(0, CE_LCD_H, &res);
    CHECK(!res && e[0] == 0 && e[CE_LCD_W * CE_LCD_H - 1] == 0, "LCD disabled renders black and returns false");
    free(a); free(b); free(c); free(d); free(e);
}

static void test_render_16bpp(void) {
    uint16_t *fb = (uint16_t *)(mem.ram.block + FB_OFFSET);
    for (unsigned i = 0; i < CE_LCD_W * CE_LCD_H; i++) fb[i] = (uint16_t)(i * 31 + i / 97);
    lcd_setup(6, true);                     /* 16 bpp 5:6:5, BGR: no swap */

    panel_set_os_default();
    uint16_t *a = render(0, CE_LCD_H, NULL);
    CHECK(!memcmp(a, fb, CE_LCD_W * CE_LCD_H * 2), "16bpp raster layout");

    panel_set_os_default();
    madctl(0x48);
    uint16_t *b = render(0, CE_LCD_H, NULL);
    unsigned bad = 0;
    for (unsigned y = 0; y < CE_LCD_H; y++)
        for (unsigned x = 0; x < CE_LCD_W; x++)
            bad += b[y * CE_LCD_W + x] != fb[x * CE_LCD_H + y];
    CHECK(bad == 0, "16bpp column-major layout: %u wrong pixels", bad);

    uint16_t *c = render(100, 140, NULL);
    CHECK(!memcmp(c + 100 * CE_LCD_W, b + 100 * CE_LCD_W, 40 * CE_LCD_W * 2), "16bpp row range");
    /* the manual override gives the same picture without any panel commands */
    panel_reset();
    panel_layout = PANEL_LAYOUT_SIDEWAYS;
    uint16_t *d = render(0, CE_LCD_H, NULL);
    CHECK(!memcmp(d, b, CE_LCD_W * CE_LCD_H * 2), "forced sideways matches detected sideways");
    panel_layout = PANEL_LAYOUT_AUTO;
    free(a); free(b); free(c); free(d);
}

/* ---- save states ------------------------------------------------------- */

static void test_state(void) {
    /* sideways state survives */
    FILE *f = tmpfile();
    panel_set_os_default(); madctl(0x48);
    CHECK(panel_transposed(), "setup: sideways");
    CHECK(ce_save_state(f), "save state");
    long full = ftell(f);
    panel_set_os_default();
    CHECK(!panel_transposed(), "setup: normal");
    rewind(f);
    CHECK(ce_load_state(f), "load state");
    CHECK(panel_transposed() && panel.madctl == 0x48 && panel.seen_mv, "sideways restored (madctl 0x%02X)", panel.madctl);

    /* normal state survives */
    FILE *g = tmpfile();
    panel_set_os_default(); madctl(0x68);
    CHECK(ce_save_state(g), "save");
    panel_set_os_default(); madctl(0x48);
    rewind(g);
    CHECK(ce_load_state(g) && !panel_transposed() && panel.madctl == 0x68, "normal restored");

    /* a state from before the panel model: the same bytes without the final two */
    rewind(f);
    uint8_t *buf = malloc((size_t)full);
    CHECK(fread(buf, 1, (size_t)full, f) == (size_t)full, "read back");
    FILE *old = tmpfile();
    fwrite(buf, 1, (size_t)full - 2, old);
    rewind(old);
    panel_reset(); madctl(0x48);
    CHECK(ce_load_state(old), "old-format state still loads");
    CHECK(!panel_transposed() && (panel.madctl & PANEL_MADCTL_MV), "old-format state gets the OS (landscape) layout");

    /* an earlier test build wrote one byte (0 = MV never set): must come back as the OS layout */
    FILE *one = tmpfile();
    fwrite(buf, 1, (size_t)full - 1, one);
    rewind(one);
    CHECK(ce_load_state(one), "one-byte state loads");
    CHECK(!panel_transposed(), "one-byte state with MV clear is not taken as sideways");
    free(buf);
    fclose(f); fclose(g); fclose(old); fclose(one);
}

int main(void) {
    size_t size = SIZE_FLASH_MIN;
    uint8_t *rom = malloc(size);
    memset(rom, 0xFF, size);
    if (!ce_init(rom, size)) { printf("ce_init failed\n"); return 2; }

    test_panel_commands();
    test_through_spi();
    test_render_8bpp();
    test_render_16bpp();
    test_state();

    ce_free();
    free(rom);
    printf(failures ? "panel_test: %d FAILED\n" : "panel_test: all passed\n", failures);
    return failures != 0;
}
