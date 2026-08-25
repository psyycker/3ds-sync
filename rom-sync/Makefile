#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

APP_TITLE       := rom-sync
APP_DESCRIPTION := Sync files from Google Drive to the SD card
APP_AUTHOR      := 3ds-homebrew-save-sync

TARGET      := rom-sync
BUILD       := build
SOURCES     := source
DATA        := data
INCLUDES    := include source 3rd-party/json
GRAPHICS    := assets/gfx
ROMFS       := assets/romfs
GFXBUILD    := $(ROMFS)/gfx

ICON         := assets/icon.png
BANNER_AUDIO := assets/audio.wav
BANNER_IMAGE := assets/banner.png
RSF_PATH     := assets/app.rsf
ICON_FLAGS   := nosavebackups,visible

ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS := -g -Wall -Wextra -Wno-psabi -O2 -mword-relocations \
          -ffunction-sections -Wno-unused-parameter \
          $(ARCH) \
          -DARM11 -D__3DS__ -D_GNU_SOURCE=1 \
          -DJSON_HAS_FILESYSTEM=0 -DJSON_HAS_EXPERIMENTAL_FILESYSTEM=0

CFLAGS += $(INCLUDE)

CXXFLAGS := $(CFLAGS) -fno-rtti -std=gnu++23

ASFLAGS := -g $(ARCH)
LDFLAGS  = -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS := -lcitro2d -lcitro3d -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -lctru -lm

LIBDIRS := $(CTRULIB) $(PORTLIBS)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)

export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                $(foreach dir,$(GRAPHICS),$(CURDIR)/$(dir)) \
                $(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES    := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
GFXFILES    := $(foreach dir,$(GRAPHICS),$(notdir $(wildcard $(dir)/*.t3s)))
BINFILES    := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

ifeq ($(strip $(CPPFILES)),)
    export LD := $(CC)
else
    export LD := $(CXX)
endif

export T3XFILES := $(GFXFILES:.t3s=.t3x)

export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES_BIN     := $(addsuffix .o,$(BINFILES)) \
                          $(if $(filter $(BUILD),$(GFXBUILD)),$(addsuffix .o,$(T3XFILES)))
export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)

export HFILES := $(addsuffix .h,$(subst .,_,$(BINFILES))) $(GFXFILES:.t3s=.h)

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                   -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export _3DSXDEPS := $(if $(NO_SMDH),,$(OUTPUT).smdh)

ifeq ($(strip $(ICON)),)
    export APP_ICON := $(TOPDIR)/icon.png
else
    export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_SMDH)),)
    export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh
endif

ifneq ($(ROMFS),)
    export _3DSXFLAGS += --romfs=$(CURDIR)/$(ROMFS)
endif

.PHONY: all clean sprites

all: sprites
	@mkdir -p $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile $(OUTPUT).3dsx

$(BUILD):
	@mkdir -p $@

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(GFXBUILD) $(TARGET).3dsx $(OUTPUT).smdh $(TARGET).elf

sprites:
	@mkdir -p $(BUILD) $(GFXBUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile sprites.t3x
	@cp $(BUILD)/sprites.t3x $(GFXBUILD)/sprites.t3x

#---------------------------------------------------------------------------------
else

$(OUTPUT).3dsx : $(OUTPUT).elf $(_3DSXDEPS)

$(OFILES_SOURCES) : $(HFILES)

$(OUTPUT).elf : $(OFILES)

%.bin.o %_bin.h : %.bin
	@echo $(notdir $<)
	@$(bin2o)

%.der.o %_der.h : %.der
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPSDIR)/*.d

endif
