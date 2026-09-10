# O2EM — Videopac / Odyssey² for Retro-Go SD

Standalone dynamic core for
[Game & Watch Retro-Go SD](https://github.com/sylverb/game-and-watch-retro-go-sd),
based on [o2em-go](https://github.com/sylverb/o2em-go) (O2EM).

| | |
|--|--|
| Packed binary | `o2em.bin` → `/cores/o2em.bin` |
| ROMs | `/roms/videopac/*.bin` |
| BIOS | `/bios/videopac/o2rom.bin` (or `c52.bin` / `g7400.bin` / `jopac.bin`) |

## Memory layout

| Region | Use |
|--|--|
| **ITCM** | Hot **code** only (`cpu`, `vdc`, `vmachine`, `keyboard`, `audio`, `table`) — no heap data |
| **DTCM** | Collision buffer via `dtc_malloc` (~85 KiB); leftover for other hot state |
| **RAM_EMU** | Core image + large BSS (`bmp`, `snapedlines`, `rom_table`, VPP buffers) |
| **AHB** | Avoided (tight firmware heap) |

Cart ROM prefers QSPI flash XIP when RAM is tight. The old in-firmware port
used AHB for the collision buffer; this core uses DTCM instead.

## Build

```bash
make          # → o2em.bin
make docker   # builder image sylverb/retro-go-sd-builder
make host     # optional SDL preview (needs SDL2)
```

Requires `arm-none-eabi-gcc` (hard-float `fpv5-d16`) and Python 3 + Pillow
for logo packing (`pip install -r requirements.txt`).

## Controls

| G&W | Odyssey² |
|--|--|
| D-pad | Joystick |
| A / B | Action |
| GAME / START (X) | Enter / start |

## Assets

- `src/assets/header.png` — VIDEOPAC wordmark (128×18, dark on light)
- `src/assets/pad.png` — joystick silhouette (56×32, dark on light)

## License

Core glue and SDK bridge: see root `LICENSE`. Engine under `src/o2em/` retains
its upstream license (`src/o2em/COPYING`).
