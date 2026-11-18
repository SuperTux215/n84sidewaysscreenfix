# TI-84 Plus CE machine

Most files in this directory come from **CEmu** (https://github.com/CE-Programming/CEmu,
`core/`, commit 430510c5, November 2026), Copyright (C) 2015-2026 CEmu contributors,
licensed under the GNU GPL v3 (or later) like N84.

Taken from CEmu with small changes:
`schedule.c` `control.c` `flash.c` `interrupt.c` `timers.c` `keypad.c` `realclock.c`
`sha256.c` `backlight.c` `misc.c` `lcd.c` `spi.c` `bootver.c` `bus.c` `port.c` `mem.c`
and their headers, plus `defines.h`, `atomics.h`, `asic.h`.

Changes made for N84 (2026):

- `cpu.cycles` is N84's eZ80 core cycle counter (`cpu.z.cycles`), see `cpu.h`.
  `sched_rewind_cpu` was adapted to the signed counter.
- `interrupt.c` drives the core's IRQ line instead of being polled.
- `lcd.c`: the pixel-accurate DMA/panel path was removed; frames are read from
  the frame buffer (CEmu's non-DMA mode).
- `mem.c`: debugger hooks removed.
- `panel.c`/`panel.h` (N84 code, replacing CEmu's `panel.c`): a minimal ST7789 model that only
  tracks the MV (row/column exchange) bit of MADCTL. The OS leaves it clear (MADCTL 0x08);
  programs that set it (lcddrvce users, such as the Buckshot Roulette port, which writes 0x28)
  get a column-major frame buffer, which `ce_lcd_render_rows` transposes. The N84 menu can force
  either layout by hand. The setting is stored at the end of save states (older states still load).
- Replaced by N84 code: `cpu.h`, `emu.h`, `usb/usb.h`, `debug/debug.h`,
  `os/os.h` (shims/stubs) and `ce.c` (CEmu's `asic.c`/`emu.c` role: device wiring,
  reset, page table for the core, run loop, frame conversion).

Not emulated: USB (no device ever connected), the SPI LCD panel (except MV, above), UART, hardware
cursor overlay, memory protection on the direct RAM/Flash fast path, and the serial
Flash used by revision M+ calculators (parallel-Flash models only for now).
