/*
 * Videopac / Odyssey² (o2em) — Retro-Go SD dynamic core.
 *
 * Memory layout:
 *   ITCM  — hot CODE only (cpu / vdc / vmachine / keyboard / audio / table)
 *   DTCM  — collision buffer via dtc_malloc (~85 KiB); leftover for hot state
 *   RAM_EMU — image + large BSS (bmp, snapedlines, rom_table, VPP buffers)
 *   AHB   — avoid (tight heap on this firmware)
 *
 * BIOS: /bios/videopac/o2rom.bin (or c52.bin / g7400.bin / jopac.bin)
 * ROMs: /roms/videopac/ (*.bin)
 */

#include <odroid_system.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "gw_lcd.h"
#include "gw_buttons.h"
#include "gw_malloc.h"
#include "rom_manager.h"
#include "common.h"
#include "appid.h"
#include "crc32.h"

#include "audio.h"
#include "o2em_config.h"
#include "cpu.h"
#include "keyboard.h"
#include "score.h"
#include "vdc.h"
#include "vmachine.h"
#include "voice.h"
#include "vpp.h"
#include "wrapalleg.h"

#ifndef HOST_BUILD
#include "gw_core_bridge.h"
#else
#include "host_compat.h"
#endif

#define AUDIO_SAMPLE_RATE_VIDEOPAC 63360
#define FPS_VIDEOPAC               60
#define RETROK_RETURN              13

#ifdef HOST_BUILD
#define BIOS_PATH_O2ROM  "./bios/videopac/o2rom.bin"
#define BIOS_PATH_C52    "./bios/videopac/c52.bin"
#define BIOS_PATH_G7400  "./bios/videopac/g7400.bin"
#define BIOS_PATH_JOPAC  "./bios/videopac/jopac.bin"
#else
#define BIOS_PATH_O2ROM  "/bios/videopac/o2rom.bin"
#define BIOS_PATH_C52    "/bios/videopac/c52.bin"
#define BIOS_PATH_G7400  "/bios/videopac/g7400.bin"
#define BIOS_PATH_JOPAC  "/bios/videopac/jopac.bin"
#endif

/* Prefer Odyssey² / G7000, then European / French variants. */
static const char *const bios_candidates[] = {
    BIOS_PATH_O2ROM,
    BIOS_PATH_C52,
    BIOS_PATH_G7400,
    BIOS_PATH_JOPAC,
    NULL,
};

static bool low_pass_enabled  = true;
static int32_t low_pass_range = (60 * 0x10000) / 100;
static int32_t low_pass_prev  = 0;

static odroid_gamepad_state_t previous_joystick_state;

/* Shared with o2em (declared extern in engine). */
uint8_t soundBuffer[SOUND_BUFFER_LEN];
int RLOOP = 0;
int joystick_data[2][5] = { { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } };

void update_joy(void)
{
}

static void blit_empty(void)
{
}

static bool SaveState(const char *savePathName)
{
    size_t size = savestate_size();
    uint8_t *buf;
    FILE *file;
    size_t written;

    buf = ram_malloc(size);
    if (!buf)
        return false;
    if (!savestate_to_mem(buf, size))
        return false;

    file = fopen(savePathName, "wb");
    if (!file)
        return false;
    written = fwrite(buf, 1, size, file);
    fclose(file);
    return written == size;
}

static bool LoadState(const char *savePathName)
{
    size_t size = savestate_size();
    uint8_t *buf;
    FILE *file;
    size_t n;

    buf = ram_malloc(size);
    if (!buf)
        return false;

    file = fopen(savePathName, "rb");
    if (!file)
        return false;
    n = fread(buf, 1, size, file);
    fclose(file);
    if (n < size)
        return false;
    return loadstate_from_mem(buf, size);
}

static void *Screenshot(void)
{
    lcd_wait_for_vblank();
    /* Last presented frame is already in the active LCD buffer. */
    return lcd_get_active_buffer();
}

static void SleepWake(void)
{
    odroid_audio_init(AUDIO_SAMPLE_RATE_VIDEOPAC);
    audio_clear_buffers();
    audio_start_playing(SOUND_BUFFER_LEN);
}

static void videopac_input_update(odroid_gamepad_state_t *joystick)
{
    if (joystick->values[ODROID_INPUT_LEFT] && !previous_joystick_state.values[ODROID_INPUT_LEFT])
        joystick_data[0][2] = 1;
    else if (!joystick->values[ODROID_INPUT_LEFT] && previous_joystick_state.values[ODROID_INPUT_LEFT])
        joystick_data[0][2] = 0;

    if (joystick->values[ODROID_INPUT_RIGHT] && !previous_joystick_state.values[ODROID_INPUT_RIGHT])
        joystick_data[0][3] = 1;
    else if (!joystick->values[ODROID_INPUT_RIGHT] && previous_joystick_state.values[ODROID_INPUT_RIGHT])
        joystick_data[0][3] = 0;

    if (joystick->values[ODROID_INPUT_UP] && !previous_joystick_state.values[ODROID_INPUT_UP])
        joystick_data[0][0] = 1;
    else if (!joystick->values[ODROID_INPUT_UP] && previous_joystick_state.values[ODROID_INPUT_UP])
        joystick_data[0][0] = 0;

    if (joystick->values[ODROID_INPUT_DOWN] && !previous_joystick_state.values[ODROID_INPUT_DOWN])
        joystick_data[0][1] = 1;
    else if (!joystick->values[ODROID_INPUT_DOWN] && previous_joystick_state.values[ODROID_INPUT_DOWN])
        joystick_data[0][1] = 0;

    if ((joystick->values[ODROID_INPUT_A] || joystick->values[ODROID_INPUT_B]) &&
        !(previous_joystick_state.values[ODROID_INPUT_A] || previous_joystick_state.values[ODROID_INPUT_B]))
        joystick_data[0][4] = 1;
    else if (!(joystick->values[ODROID_INPUT_A] || joystick->values[ODROID_INPUT_B]) &&
             (previous_joystick_state.values[ODROID_INPUT_A] || previous_joystick_state.values[ODROID_INPUT_B]))
        joystick_data[0][4] = 0;

    /* GAME / START → Enter (many carts use it as fire / start). */
    if ((joystick->values[ODROID_INPUT_START] || joystick->values[ODROID_INPUT_X]) &&
        !(previous_joystick_state.values[ODROID_INPUT_START] || previous_joystick_state.values[ODROID_INPUT_X]))
        key[RETROK_RETURN] = 1;
    else if (!(joystick->values[ODROID_INPUT_START] || joystick->values[ODROID_INPUT_X]) &&
             (previous_joystick_state.values[ODROID_INPUT_START] || previous_joystick_state.values[ODROID_INPUT_X]))
        key[RETROK_RETURN] = 0;

    memcpy(&previous_joystick_state, joystick, sizeof(odroid_gamepad_state_t));
}

static bool load_bios_file(const char *path, uint8_t *dest, size_t dest_len, size_t *out_size)
{
    FILE *f;
    size_t n;

    f = fopen(path, "rb");
    if (!f)
        return false;
    n = fread(dest, 1, dest_len, f);
    fclose(f);
    if (n != 1024)
        return false;
    *out_size = n;
    return true;
}

static bool load_bios(void)
{
    uint8_t bios_data[1024];
    size_t bios_size = 0;
    uint32_t crc;
    size_t i;
    const char *const *path;

    for (path = bios_candidates; *path; path++) {
        if (load_bios_file(*path, bios_data, sizeof(bios_data), &bios_size)) {
            printf("[O2EM]: BIOS from %s\n", *path);
            break;
        }
    }
    if (bios_size != 1024) {
        printf("[O2EM]: Error loading BIOS ROM (tried /bios/videopac/*.bin)\n");
        return false;
    }

    memcpy(rom_table[0], bios_data, 1024);
    for (i = 1; i < 8; i++)
        memcpy(rom_table[i], rom_table[0], 1024);

    crc = crc32_le(0, rom_table[0], 1024);
    switch (crc) {
    case 0x8016A315:
        printf("[O2EM]: Magnavox Odyssey2 BIOS ROM loaded (G7000 model)\n");
        app_data.vpp  = 0;
        app_data.bios = ROM_O2;
        break;
    case 0xE20A9F41:
        printf("[O2EM]: Philips Videopac+ European BIOS ROM loaded (G7400 model)\n");
        app_data.vpp  = 1;
        app_data.bios = ROM_G7400;
        break;
    case 0xA318E8D6:
        printf("[O2EM]: Philips Videopac+ French BIOS ROM loaded (G7000 model)\n");
        app_data.vpp  = 0;
        app_data.bios = ROM_C52;
        break;
    case 0x11647CA5:
        printf("[O2EM]: Philips Videopac+ French BIOS ROM loaded (G7400 model)\n");
        app_data.vpp  = 1;
        app_data.bios = ROM_JOPAC;
        break;
    default:
        printf("[O2EM]: BIOS ROM loaded (unknown version, crc=%08lx)\n", (unsigned long)crc);
        app_data.vpp  = 0;
        app_data.bios = ROM_UNKNOWN;
        break;
    }
    return true;
}

static bool load_cart(const uint8_t *data, size_t size)
{
    int i, nb;

    app_data.crc = crc32_le(0, data, size);

    if (app_data.crc == 0xAFB23F89)
        app_data.exrom = 1; /* Musician */
    if (app_data.crc == 0x3BFEF56B)
        app_data.exrom = 1; /* Four in 1 Row! */
    if (app_data.crc == 0x9B5E9356)
        app_data.exrom = 1; /* Four in 1 Row! (french) */

    if ((app_data.crc == 0x975AB8DA) || (app_data.crc == 0xE246A812)) {
        printf("[O2EM]: Loaded content is an incomplete ROM dump.\n");
        return false;
    }

    if ((size % 1024) != 0) {
        printf("[O2EM]: Error: Loaded content is an invalid ROM dump.\n");
        return false;
    }

    if ((size % 3072) == 0) {
        app_data.three_k = 1;
        nb               = (int)(size / 3072);
        for (i = (nb - 1); i >= 0; i--) {
            memcpy(&rom_table[i][1024], data, 3072);
            data += 3072;
        }
        printf("[O2EM]: %uK\n", (unsigned)(nb * 3));
    } else {
        nb = (int)(size / 2048);
        if ((nb == 2) && (app_data.exrom)) {
            memcpy(&extROM[0], data, 1024);
            data += 1024;
            memcpy(&rom_table[0][1024], data, 3072);
            data += 3072;
            printf("[O2EM]: 3K EXROM\n");
        } else {
            for (i = (nb - 1); i >= 0; i--) {
                memcpy(&rom_table[i][1024], data, 2048);
                data += 2048;
                /* simulate missing A10 */
                memcpy(&rom_table[i][3072], &rom_table[i][2048], 1024);
            }
            printf("[O2EM]: %uK\n", (unsigned)(nb * 2));
        }
    }

    o2em_rom = rom_table[0];
    if (nb == 1)
        app_data.bank = 1;
    else if (nb == 2)
        app_data.bank = app_data.exrom ? 1 : 2;
    else if (nb == 4)
        app_data.bank = 3;
    else
        app_data.bank = 4;

    if ((rom_table[nb - 1][1024 + 12] == 'O') &&
        (rom_table[nb - 1][1024 + 13] == 'P') &&
        (rom_table[nb - 1][1024 + 14] == 'N') &&
        (rom_table[nb - 1][1024 + 15] == 'B'))
        app_data.openb = 1;

    return true;
}

static size_t get_rom_data(unsigned char **data)
{
    uint32_t size = 0;
    FILE *f;

    if (!ACTIVE_FILE || !ACTIVE_FILE->path[0]) {
        *data = NULL;
        return 0;
    }

    if (ACTIVE_FILE->size)
        size = ACTIVE_FILE->size;
    else {
        f = fopen(ACTIVE_FILE->path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fclose(f);
            if (sz > 0)
                size = (uint32_t)sz;
        }
    }
    if (size == 0) {
        *data = NULL;
        return 0;
    }

#ifdef HOST_BUILD
    /* Host has no QSPI flash cache — always load into the RAM pool. */
    *data = ram_malloc(size);
    if (*data && odroid_overlay_cache_file_in_ram(ACTIVE_FILE->path, *data) == size)
        return size;
    *data = NULL;
    return 0;
#else
    /* Prefer flash XIP for cart ROM — RAM_EMU is tight with video BSS. */
    if (size > ram_get_free_size() / 2) {
        *data = odroid_overlay_cache_file_in_flash(ACTIVE_FILE->path, &size, false);
    } else {
        *data = ram_malloc(size);
        if (*data)
            odroid_overlay_cache_file_in_ram(ACTIVE_FILE->path, *data);
        else
            *data = odroid_overlay_cache_file_in_flash(ACTIVE_FILE->path, &size, false);
    }
    return size;
#endif
}

static void load_data(void)
{
    uint8_t *rom_data;
    size_t rom_size;

    app_data.stick[0] = app_data.stick[1] = 1;
    app_data.sticknumber[0] = app_data.sticknumber[1] = 0;
    set_defjoykeys(0, 0);
    set_defjoykeys(1, 1);
    set_defsystemkeys();
    app_data.bank               = 0;
    app_data.limit              = 1;
    app_data.sound_en           = 1;
    app_data.speed              = 100;
    app_data.wsize              = 2;
    app_data.scanlines          = 0;
    app_data.voice              = 0;
    app_data.filter             = 0;
    app_data.exrom              = 0;
    app_data.three_k            = 0;
    app_data.crc                = 0;
    app_data.openb              = 0;
    app_data.vpp                = 0;
    app_data.bios               = 0;
    app_data.scoretype          = 0;
    app_data.scoreaddress       = 0;
    app_data.default_highscore  = 0;
    app_data.breakpoint         = 65535;
    app_data.megaxrom           = 0;

    init_audio();
    if (!load_bios())
        return;
    rom_size = get_rom_data(&rom_data);
    if (!rom_data || rom_size == 0) {
        printf("[O2EM]: failed to load ROM\n");
        return;
    }
    if (!load_cart(rom_data, rom_size))
        return;
}

static void pcm_submit(void)
{
    size_t i;

    if (common_emu_sound_loop_is_muted())
        return;

    int32_t factor                 = common_emu_sound_get_volume();
    int16_t *sound_buffer          = audio_get_active_buffer();
    uint16_t sound_buffer_length   = audio_get_buffer_length();
    uint8_t *audio_in_ptr          = soundBuffer;
    int16_t *audio_out_ptr         = sound_buffer;

    if (low_pass_enabled) {
        int32_t low_pass = low_pass_prev;
        int32_t factor_a = low_pass_range;
        int32_t factor_b = 0x10000 - factor_a;

        for (i = 1; i <= sound_buffer_length; i++) {
            int32_t sample16;

            sample16 = ((((*(audio_in_ptr++) * factor) / 256) - 128) << 8) + 32768;
            low_pass = (low_pass * factor_a) + (sample16 * factor_b);
            low_pass >>= 16;
            *(audio_out_ptr++) = (int16_t)low_pass;
        }
        low_pass_prev = low_pass;
    } else {
        for (i = 1; i <= sound_buffer_length; i++) {
            int32_t sample16;

            sample16 = ((((*(audio_in_ptr++) * factor) / 256) - 128) << 8) + 32768;
            *(audio_out_ptr++) = (int16_t)sample16;
        }
    }
}

/* Crop 340×250 indexed frame to 320×240 RGB565 (skip 10 px left border). */
void gnw_videopack_blit(uint8_t *input, APALETTE *palette)
{
    int i, j;
    unsigned char ind;
    uint16_t *outp = (uint16_t *)lcd_get_inactive_buffer();

    for (i = 0; i < HEIGHT; i++) {
        for (j = 0; j < WIDTH; j++) {
            ind        = input[i * 340 + j + 10];
            (*outp++)  = RGB565(palette[ind].r, palette[ind].g, palette[ind].b);
        }
    }
    common_ingame_overlay();
}

void app_main(uint8_t load_state, uint8_t start_paused, int8_t save_slot)
{
    odroid_dialog_choice_t options[] = {
        ODROID_DIALOG_CHOICE_LAST
    };
    odroid_gamepad_state_t joystick;

    /* Host already called gw_core_bridge_init() before host_set_rom_path();
     * calling it again would wipe ACTIVE_FILE. */
#ifndef HOST_BUILD
    gw_core_bridge_init();
#endif
    memset(&previous_joystick_state, 0, sizeof(previous_joystick_state));

    if (start_paused) {
        common_emu_state.pause_after_frames = 2;
        odroid_audio_mute(true);
    } else {
        common_emu_state.pause_after_frames = 0;
    }
    common_emu_state.frame_time_10us = (uint16_t)(100000 / FPS_VIDEOPAC + 0.5f);
    lcd_set_refresh_rate(FPS_VIDEOPAC);

    odroid_system_init(APPID_CORE, AUDIO_SAMPLE_RATE_VIDEOPAC);
    odroid_system_emu_init(&LoadState, &SaveState, &Screenshot,
                           NULL, &SleepWake, NULL, NULL);

    audio_start_playing(SOUND_BUFFER_LEN);

    load_data();

    init_display();
    init_cpu();
    init_system();

    set_score(app_data.scoretype, app_data.scoreaddress, app_data.default_highscore);
    app_data.euro = 0;

    if (load_state)
        odroid_system_emu_load_state(save_slot);
    else
        lcd_clear_buffers();

    while (true) {
        wdog_refresh();

        (void)common_emu_frame_loop();

        odroid_input_read_gamepad(&joystick);
        common_emu_input_loop(&joystick, options, &blit_empty);
        common_emu_input_loop_handle_turbo(&joystick);

        videopac_input_update(&joystick);

        RLOOP = 1;
        cpu_exec();
        lcd_swap();
        pcm_submit();

        common_emu_sound_sync(false);
    }
}
