# Retro-Go SD — Videopac / Odyssey² (o2em) standalone dynamic core.
#
#   make                  — build + pack → o2em.bin
#   make docker           — same inside the firmware builder image
#   make host             — Linux/macOS SDL binary
#
# Memory: hot cpu/vdc/vmachine/keyboard/audio/table .text in ITCM;
# collision buffer in DTCM (dtc_malloc). ITCM is not used for heap data.
# BIOS: /bios/videopac/o2rom.bin (or c52/g7400/jopac). ROMs: /roms/videopac/

#######################################
# Project identity
#######################################
PROJECT_KIND ?= core

CORE_NAME  := o2em
CORE_ENTRY := app_main

CORE_O2EM := src/o2em

CORE_C_SOURCES := \
src/main.c \
$(CORE_O2EM)/src/o2em_audio.c \
$(CORE_O2EM)/src/o2em_cpu.c \
$(CORE_O2EM)/src/o2em_cset.c \
$(CORE_O2EM)/src/o2em_keyboard.c \
$(CORE_O2EM)/src/o2em_score.c \
$(CORE_O2EM)/src/o2em_table.c \
$(CORE_O2EM)/src/o2em_vdc.c \
$(CORE_O2EM)/src/o2em_vmachine.c \
$(CORE_O2EM)/src/o2em_voice.c \
$(CORE_O2EM)/src/o2em_vpp.c \
$(CORE_O2EM)/src/o2em_vpp_cset.c \
$(CORE_O2EM)/allegrowrapper/wrapalleg.c

CORE_C_INCLUDES := \
-I$(CORE_O2EM)/src \
-I$(CORE_O2EM)/allegrowrapper \
-I$(CORE_O2EM)/include

# Relative path so Docker bind-mounts work (do NOT use $(abspath)).
GNW_CORE_SDK ?= sdk
BUILD_DIR ?= build/$(PROJECT_KIND)

CORE_LDSCRIPT := ld/o2em_core.ld
CORE_EXTRA_SEGMENTS := itcm:core_itcm

#######################################
# Kind-specific compile defs + packing
#######################################
ifeq ($(PROJECT_KIND),core)
CORE_C_DEFS := \
-DPROJECT_KIND_CORE=1 \
-DCOVERFLOW=1 \
-DCHEAT_CODES=0 \
-DGNW_DISABLE_COMPRESSION \
-DTARGET_GNW \
-D__LIBRETRO__

PACKED_BIN  := $(CORE_NAME).bin
PAD_LOGO    := src/assets/pad.png
HEADER_LOGO := src/assets/header.png

else
$(error PROJECT_KIND must be 'core' (got '$(PROJECT_KIND)'))
endif

include $(GNW_CORE_SDK)/Makefile

PACK_CORE := $(GNW_CORE_SDK)/tools/pack_core.py

#######################################
# Packed header version
#######################################
CORE_VERSION ?= $(shell git describe --tags --dirty 2>/dev/null || echo NOTAG)

#######################################
# Pack
#######################################
.PHONY: pack

pack: $(TARGET_BIN) $(PAD_LOGO) $(HEADER_LOGO)
	$(V)$(ECHO) [ PACK CORE ] $(PACKED_BIN) version=$(CORE_VERSION)
	$(V)python3 $(PACK_CORE) \
		--elf $(TARGET_ELF) --bin $(TARGET_BIN) \
		--system-name "Videopac / Odyssey2" --dirname videopac \
		--extensions "bin" \
		--core-name "O2EM" \
		--version "$(CORE_VERSION)" \
		--pad-logo $(PAD_LOGO) \
		--header-logo $(HEADER_LOGO) \
		--logo-invert \
		--out $(PACKED_BIN)

all: pack

.PHONY: print-PROJECT_KIND print-PACKED_BIN print-SIDECARS print-RO_BIN print-CORE_NAME print-DOCKER_IMAGE \
	print-TARGET_ELF print-TARGET_MAP print-CORE_VERSION
print-PROJECT_KIND:
	@echo $(PROJECT_KIND)
print-PACKED_BIN:
	@echo $(PACKED_BIN)
# Extra device files installed beside PACKED_BIN, space separated. Empty
# here: only a project that installs a second device file sets it. RO_BIN
# is the older single-slot spelling, read for every project so the shared
# stage_release.py needs no per-project variant.
print-SIDECARS:
	@echo $(SIDECARS)
print-RO_BIN:
	@echo $(RO_BIN)
print-CORE_NAME:
	@echo $(CORE_NAME)
print-DOCKER_IMAGE:
	@echo $(DOCKER_IMAGE)
print-TARGET_ELF:
	@echo $(TARGET_ELF)
print-TARGET_MAP:
	@echo $(BUILD_DIR)/$(CORE_NAME)_core.map
print-CORE_VERSION:
	@echo $(CORE_VERSION)

clean::
	$(V)rm -f $(PACKED_BIN)

#######################################
# Docker
#######################################
.PHONY: docker docker_pull docker_shell

RELEASE_VERSION ?= v1.5
DOCKER_REPOSITORY ?= sylverb/retro-go-sd-builder
DOCKER_IMAGE ?= $(DOCKER_REPOSITORY):$(RELEASE_VERSION)

DOCKER_TTY_FLAG := $(shell if [ -t 0 ]; then echo -it; else echo; fi)
DOCKER_USER := $(shell id -u):$(shell id -g)
DOCKER_RUN := docker run --rm $(DOCKER_TTY_FLAG) \
	--user $(DOCKER_USER) \
	-v "$(CURDIR):/opt/workdir" \
	-w /opt/workdir \
	$(DOCKER_IMAGE)

docker:
	$(V)$(ECHO) "[ DOCKER ]" $(DOCKER_IMAGE) "PROJECT_KIND=$(PROJECT_KIND)"
	$(V)$(DOCKER_RUN) make --no-print-directory -j$$(nproc) PROJECT_KIND=$(PROJECT_KIND)

docker_pull:
	$(V)$(ECHO) "[ PULL ]" $(DOCKER_IMAGE)
	$(V)docker pull $(DOCKER_IMAGE)

docker_shell:
	$(DOCKER_RUN) bash

#######################################
# Host SDL
#######################################
include host/Makefile.host
