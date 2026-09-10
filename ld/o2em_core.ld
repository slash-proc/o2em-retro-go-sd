/*
 * Videopac / Odyssey² (o2em) — RAM_EMU entry + ITCM hot CODE only.
 *
 * ITCM (no .data / .bss / .rodata):
 *   CPU interpreter, VDC draw, vmachine frame, keyboard, audio tick.
 *
 * Large BSS (snapedlines, bmp buffers, rom_table, VPP) stays in RAM_EMU.
 * Collision buffer is dtc_malloc'd (DTCM). ITCM is not used for heap data.
 *
 * Object paths match BUILD_DIR=build/core (PROJECT_KIND=core).
 */

INCLUDE gnw_ram_emu.ld
INCLUDE gnw_itcm_core.ld

MEMORY
{
  RAM_EMU (xrw) : ORIGIN = __RAM_EMU_START__, LENGTH = __RAM_EMU_LENGTH__
  ITCM    (xrw) : ORIGIN = __ITCM_CORE_START__, LENGTH = __ITCM_CORE_LENGTH__
}

PHDRS
{
  text PT_LOAD FLAGS(5); /* PF_R | PF_X */
  data PT_LOAD FLAGS(6); /* PF_R | PF_W */
  itcm PT_LOAD FLAGS(5);
}

SECTIONS
{
  .core_entry __RAM_EMU_START__ :
  {
    KEEP(*(.core_entry))
    KEEP(*(.gw_core_bridge_probe))
    . = ALIGN(4);
    EXCLUDE_FILE(
      build/core/o2em_cpu.o
      build/core/o2em_vdc.o
      build/core/o2em_vmachine.o
      build/core/o2em_keyboard.o
      build/core/o2em_audio.o
      build/core/o2em_table.o
    ) *(.text*)
    *(.rodata*)
    *(.glue_7)
    *(.glue_7t)

    . = ALIGN(4);
    __CORE_INIT_ARRAY_START__ = .;
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    __CORE_INIT_ARRAY_END__ = .;
  } >RAM_EMU :text

  .data :
  {
    *(.data*)
  } >RAM_EMU :data

  __CORE_CODE_END__ = .;

  .bss (NOLOAD) :
  {
    . = ALIGN(8);
    *(.bss*)
    *(COMMON)
    . = ALIGN(8);
  } >RAM_EMU :data

  __CORE_BSS_END__ = .;

  ASSERT(__CORE_BSS_END__ <= __RAM_EMU_END__,
         "Error: o2em core RAM_EMU segment (code+data+bss) overflows RAM_EMU")

  /* Hot CODE only — no .data / .bss / .rodata in ITCM. */
  .core_itcm __ITCM_CORE_START__ :
  {
    . = ALIGN(4);
    __CORE_ITCM_CODE_START__ = .;
    build/core/o2em_cpu.o(.text*)
    build/core/o2em_vdc.o(.text*)
    build/core/o2em_vmachine.o(.text*)
    build/core/o2em_keyboard.o(.text*)
    build/core/o2em_audio.o(.text*)
    build/core/o2em_table.o(.text*)
    . = ALIGN(4);
    __CORE_ITCM_CODE_END__ = .;
  } >ITCM :itcm

  __CORE_ITCM_BSS_END__ = __CORE_ITCM_CODE_END__;

  ASSERT(__CORE_ITCM_BSS_END__ <= __ITCM_CORE_START__ + __ITCM_CORE_LENGTH__,
         "Error: o2em core ITCM segment (hot code) overflows the 64K ITCM region")

  __exidx_start = .;
  __exidx_end = .;

  /DISCARD/ :
  {
    *(.fini_array*)
    *(.preinit_array*)
    *(.ARM.exidx*)
    *(.ARM.extab*)
    *(.comment)
    *(.ARM.attributes)
  }
}
