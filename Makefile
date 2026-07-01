DEVKITPRO ?= $(CURDIR)/externals/devkitpro
DEVKITARM ?= $(DEVKITPRO)/devkitARM
DEVKITPPC ?= $(DEVKITPRO)/devkitPPC
export DEVKITPRO DEVKITARM DEVKITPPC

# --- Compiler and Flags ---
CC = $(DEVKITPPC)/bin/powerpc-eabi-gcc
LD = $(DEVKITPPC)/bin/powerpc-eabi-ld
PYTHON = uv run --with pyelftools --with pyisotools python

# --- Directories ---
BUILD_DIR 		= build
SCRIPT_DIR 		?= scripts
HOSHI_DIR		= externals/hoshi
LIB_ROOT_DIR 	= $(HOSHI_DIR)/Lib
INC_DIR 		?= $(HOSHI_DIR)/include
PACKTOOL_DIR 	?= $(HOSHI_DIR)/packtool
ORIG_DOL		= $(HOSHI_DIR)/dol/kar.dol
OUT_DIR 		= out
MODS_OUT_DIR 	= $(OUT_DIR)/files/mods
MODS_ROOT_DIR = mods
DOLPHIN_RIIVOLUTION_DIR ?= $(HOME)/.var/app/org.DolphinEmu.dolphin-emu/data/dolphin-emu/Load/Riivolution
DOLPHIN_GC_DIR ?= $(HOME)/.var/app/org.DolphinEmu.dolphin-emu/data/dolphin-emu/GC

# --- File Paths ---
ISO_PATH		?= kar.iso
MOD_NAME		?= KARchipelago

# --- Script Paths ---
ISOPATCH_SCRIPT		= $(SCRIPT_DIR)/iso.py
DOLEXTRACT_SCRIPT	= $(SCRIPT_DIR)/dol.py

# User-defined CFLAGS.
CFLAGS = -O1 -mcpu=750 -meabi -msdata=none -mhard-float -ffreestanding \
           -fno-unwind-tables -fno-exceptions -fno-asynchronous-unwind-tables \
           -fno-merge-constants -ffunction-sections -fdata-sections \
           -MMD # needed for automatic dependency generation

LDFLAGS  ?= -r -T$(PACKTOOL_DIR)/link.ld

# Helpers for the EXCLUDE_MODS comma-to-space substitution below.
comma := ,
empty :=
space := $(empty) $(empty)

# --- Derived Variables ---
# INCLUDES: Transforms include paths into compiler -I flags
INCLUDES = -I$(INC_DIR) -I$(LIB_ROOT_DIR) \
           -I$(MODS_ROOT_DIR)/custom_events/include \
           -I$(MODS_ROOT_DIR)/textbox/include \
           -I$(MODS_ROOT_DIR)/archipelago/include \
           -I$(MODS_ROOT_DIR)/hypernova/include \
           -I$(MODS_ROOT_DIR)/custom_items/include \
           -I$(MODS_ROOT_DIR)/custom_checklist/include

# --- Source File Discovery ---

# 1. Libraries: Find all C source files recursively under the LIB_ROOT_DIR.
LIB_SOURCES := $(shell find $(LIB_ROOT_DIR) -name "*.c")

# 2. Mods: Find all mods in the mod folder
# EXCLUDE_MODS lists mod folders to drop from the build (comma- or
# space-separated). Override on the command line: `make package EXCLUDE_MODS=`
# to include everything, or `EXCLUDE_MODS=foo,bar` to drop additional mods.
# custom_events and custom_weather are excluded by default while they remain
# WIP and not wired up to the archipelago mod.
EXCLUDE_MODS ?= custom_events,custom_weather,archipelago_debug
MOD_NAMES ?= $(filter-out $(subst $(comma),$(space),$(EXCLUDE_MODS)),$(notdir $(wildcard $(MODS_ROOT_DIR)/*)))

# 3. Mods Source: For each mod, find its specific source files within its 'src' subdirectory.
MOD_C_SOURCES := $(foreach mod,$(MOD_NAMES),\
                       $(shell find $(MODS_ROOT_DIR)/$(mod)/src -name "*.c"))
MOD_ASM_SOURCES := $(foreach mod,$(MOD_NAMES),\
                       $(shell find $(MODS_ROOT_DIR)/$(mod)/src -name "*.s"))

# --- Object and Dependency File Mapping ---

# Map individual library source files to their corresponding object files in BUILD_DIR.
LIB_OBJECTS := $(patsubst $(LIB_ROOT_DIR)/%.c,$(BUILD_DIR)/$(LIB_ROOT_DIR)/%.o,$(LIB_SOURCES))

# Map individual mod source files to their corresponding object files in BUILD_DIR.
MOD_C_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(MOD_C_SOURCES))
MOD_ASM_OBJECTS := $(patsubst %.s,$(BUILD_DIR)/%.o,$(MOD_ASM_SOURCES))

MOD_OBJECTS		:= $(MOD_C_OBJECTS) $(MOD_ASM_OBJECTS)

# Combine ALL individual object files (from both libraries and mods) that need to be compiled.
ALL_INDIVIDUAL_OBJECTS_TO_COMPILE = $(LIB_OBJECTS) $(MOD_C_OBJECTS) $(MOD_ASM_OBJECTS)

# Map all these compiled objects to their corresponding dependency files (.d files).
DEPS := $(ALL_INDIVIDUAL_OBJECTS_TO_COMPILE:.o=.d)

# Get a list of all unique build directories that need to be created for ALL objects.
OBJ_DIRS := $(sort $(dir $(ALL_INDIVIDUAL_OBJECTS_TO_COMPILE)))

# MOD_BIN_FILES: The final .bin files for each mod (e.g. out/credits.bin)
MOD_BIN_FILES := $(addsuffix .bin, $(addprefix $(MODS_OUT_DIR)/, $(MOD_NAMES)))

# Define a variable for all asset directories
MOD_ASSET_DIRS := $(foreach mod,$(MOD_NAMES),\
                               $(if $(wildcard $(MODS_ROOT_DIR)/$(mod)/assets), \
                                    $(MODS_ROOT_DIR)/$(mod)/assets))

# --- Main Targets ---

.PHONY: all package clean assets riivolution deploy patch

# The 'all' target builds all final .bin files.
all: 		$(MOD_BIN_FILES) hoshi assets
package: 	all riivolution

# --- Directory Creation Rules ---
$(BUILD_DIR) $(OUT_DIR) $(MODS_OUT_DIR) $(OBJ_DIRS):
	@mkdir -p $@

# Rule to extract the original dol from the iso
$(ORIG_DOL):
	$(PYTHON) $(DOLEXTRACT_SCRIPT) $(ISO_PATH) $(ORIG_DOL)

# --- hoshi target ---
hoshi: $(ORIG_DOL)
	$(MAKE) -C $(HOSHI_DIR) MOD_NAME="$(MOD_NAME)" PYTHON="$(PYTHON)"

# --- Generic Compilation Rules ---
# Pattern rules for .c and .s sources. The | $(OBJ_DIRS) order-only prereq
# ensures the output subdirectory exists before compilation.
$(BUILD_DIR)/%.o: %.c | $(OBJ_DIRS)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/%.o: %.s | $(OBJ_DIRS)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# --- Linking Rules for Individual Mod Files ---
# Define a macro to get the mod-specific object files for linking.
# $(1) is the mod name (e.g., 'credits').
define GET_MOD_LINK_OBJECTS
$(filter $(BUILD_DIR)/$(MODS_ROOT_DIR)/$(1)/src/%.o, $(MOD_OBJECTS))
endef

# Define a template for the linking rule (including recipe).
# This template will be used for each mod.
define LINK_MOD_RULE_TEMPLATE
$(BUILD_DIR)/$(1).modlink: $(LIB_OBJECTS) $(call GET_MOD_LINK_OBJECTS,$(1))
	@echo "Linking $(1)..."
	$(LD) $(LDFLAGS) $$^ -o $$@
endef

# Generate a specific linking rule for each mod using the template.
$(foreach mod,$(MOD_NAMES),\
  $(eval $(call LINK_MOD_RULE_TEMPLATE,$(mod))))

# --- Packing Rule for Bin Files ---
# Define a template for the packing rule (including recipe).
# This template will be used for each mod.
define PACK_MOD_RULE_TEMPLATE
$(MODS_OUT_DIR)/$(1).bin: $(BUILD_DIR)/$(1).modlink | $(MODS_OUT_DIR)
	@echo "Packing $(1)..."
	$(PYTHON) $(PACKTOOL_DIR)/main.py $$< -m gbFunction -o $$@

endef

# Generate a specific packing rule for each mod using the template.
$(foreach mod,$(MOD_NAMES),\
  $(eval $(call PACK_MOD_RULE_TEMPLATE,$(mod))))

# --- Include generated dependency files (.d files) ---
-include $(DEPS)

# Rule to copy all assets into the staging files/ overlay
assets: hoshi | $(MODS_OUT_DIR)
	@for dir in $(MOD_ASSET_DIRS); do \
		if [ -d "$$dir" ]; then \
			cp -a "$$dir"/* "$(OUT_DIR)/files/"; \
		fi; \
	done
	cp -a -r "$(HOSHI_DIR)/out/release"/* "$(OUT_DIR)/files/"

patch: $(OUT_DIR) $(MOD_BIN_FILES) hoshi assets | $(BUILD_DIR)
	@echo ""
	@echo "--- Creating ISO Patch... ---"
	rm -rf $(BUILD_DIR)/iso_overlay
	mkdir -p $(BUILD_DIR)/iso_overlay
	ln -s "$(CURDIR)/$(OUT_DIR)/files" "$(BUILD_DIR)/iso_overlay/files"
	$(PYTHON) $(ISOPATCH_SCRIPT) $(ISO_PATH) $(BUILD_DIR)/iso_overlay $(OUT_DIR)/patch.xdelta

riivolution: $(OUT_DIR) $(MOD_BIN_FILES) hoshi assets
	@echo ""
	@echo "--- Creating Riivolution Mod... ---"
	rm -rf $(OUT_DIR)/Riivolution
	cp -a -r "$(HOSHI_DIR)/dol/out/Riivolution" "$(OUT_DIR)"
	cp -a -r "$(OUT_DIR)"/files/* "$(OUT_DIR)/Riivolution/$(MOD_NAME)"

deploy: package
	@echo ""
	@echo "--- Copying Riivolution files into Dolphin dir... ---"
	cp -a -r "$(OUT_DIR)/Riivolution/"* "$(DOLPHIN_RIIVOLUTION_DIR)/"

# --- Clean Target ---
clean:
	@echo "Cleaning build and output directories..."
	$(MAKE) -C $(HOSHI_DIR) clean PYTHON="$(PYTHON)"
	rm -rf $(ORIG_DOL) $(OUT_DIR) $(BUILD_DIR)
	@echo "Cleaning Dolphin Riivolution dir..."
	trash-put -f "$(DOLPHIN_RIIVOLUTION_DIR)/"*
	@echo "Cleaning Dolphin KAR memory cards..."
	@# Raw single-file cards (Dolphin's default format) plus the GCI-folder layout, which is
	@# only populated when Dolphin is set to GCI-folder mode.
	trash-put -f "$(DOLPHIN_GC_DIR)/MemoryCard"?".USA.raw" "$(DOLPHIN_GC_DIR)/USA/Card A/01-GKYE-"*
