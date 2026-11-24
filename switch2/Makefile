# SPDX-License-Identifier: GPL-3.0-or-later
.SUFFIXES:
.DEFAULT_GOAL := all
TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libounce/switch2_rules

TARGET := PKForge
BUILD := build
APP_TITLE := PKForge
APP_AUTHOR := PKForge Switch 2 contributors
APP_VERSION := 0.7.5

ARCH := $(SWITCH2_CPU) -mtp=soft -fPIE
CFLAGS := $(ARCH) -O2 -ffunction-sections -fdata-sections $(INCLUDE)
CXXFLAGS := $(CFLAGS) -std=gnu++23
LDFLAGS = -specs=$(LIBNX)/switch.specs $(ARCH) -Wl,--gc-sections
RUNTIME := $(CURDIR)/engine-build/libforge-runtime.a
LIBS := $(RUNTIME) -lnx

ifneq ($(CURDIR),$(TOPDIR))
.PHONY: all
all: $(OUTPUT).nro
$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp $(ROMFS_FILES) $(APP_ICON)
$(OUTPUT).elf: $(OFILES) $(RUNTIME)
$(RUNTIME): $(wildcard $(TOPDIR)/managed/runtime/*)
	$(MAKE) -C $(TOPDIR)/managed/runtime OUT=$(CURDIR)/engine-build CC="$(CC)" AR="$(AR)" ARCH="$(ARCH)"
$(OUTPUT).nacp: $(TOPDIR)/Makefile
-include $(OFILES:.o=.d)
else
SOURCES := source vendor/source $(wildcard vendor/source/*/) vendor/memecrypto vendor/qrcodegen vendor/lz4 $(wildcard vendor/libwebp/src/*/)
CPPFILES := $(wildcard $(addsuffix /*.cpp,$(SOURCES)))
CFILES := $(wildcard $(addsuffix /*.c,$(SOURCES)))
export VPATH := $(addprefix $(CURDIR)/,$(SOURCES))
export INCLUDE := -I$(CURDIR)/managed/runtime -DWASM_RT_USE_MMAP=0 -DWASM_RT_USE_SEGUE=0 -DWASM_RT_STACK_DEPTH_COUNT=1 -DWASM_RT_MAX_CALL_STACK_DEPTH=2048 -I$(CURDIR)/include -I$(CURDIR)/vendor/include \
 -I$(CURDIR)/vendor/include/enums -I$(CURDIR)/vendor/memecrypto -I$(CURDIR)/vendor/qrcodegen \
 -I$(CURDIR)/vendor/pcg-cpp/include -I$(CURDIR)/vendor/lz4 -I$(CURDIR)/vendor/libwebp -I$(LIBNX)/include
export LIBPATHS := -L$(LIBNX)/lib
export OFILES := $(notdir $(CPPFILES:.cpp=.o) $(CFILES:.c=.o))
export LD := $(CXX)
export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)
export DEPSDIR := $(CURDIR)/$(BUILD)
export APP_ICON := $(CURDIR)/icon.jpg
export ROMFS_FILES := $(shell find $(CURDIR)/romfs -type f)
export NROFLAGS := --icon=$(APP_ICON) --nacp=$(OUTPUT).nacp --romfsdir=$(CURDIR)/romfs

.PHONY: all clean
all:
	@mkdir -p $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).nacp $(TARGET).nro
endif
