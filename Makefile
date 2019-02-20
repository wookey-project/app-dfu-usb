APP_NAME ?= dfuusb
DIR_NAME = dfuusb

PROJ_FILES = ../../
BIN_NAME = $(APP_NAME).bin
HEX_NAME = $(APP_NAME).hex
ELF_NAME = $(APP_NAME).elf

######### Metadata ##########
ifeq ($(APP_NAME),dfuusb)
    IMAGE_TYPE = IMAGE_TYPE0
else
    IMAGE_TYPE = IMAGE_TYPE1
endif

VERSION = 1
#############################

-include $(PROJ_FILES)/Makefile.conf
-include $(PROJ_FILES)/Makefile.gen

# use an app-specific build dir
APP_BUILD_DIR = $(BUILD_DIR)/apps/$(DIR_NAME)

CFLAGS += -Isrc/ -Iinc/
CFLAGS += $(APPS_CFLAGS)
CFLAGS += -MMD -MP -O3

LDFLAGS += $(AFLAGS) -fno-builtin -nostdlib -nostartfiles -Xlinker -Map=$(APP_BUILD_DIR)/$(APP_NAME).map

# FIXME: -lg should be replaced by local malloc/free
LD_LIBS += -ldfu -lusb -lfirmware -lstd

EXTRA_LDFLAGS ?= -Tdfuusb.fw1.ld
LDFLAGS += $(EXTRA_LDFLAGS) -L$(APP_BUILD_DIR)

BUILD_DIR ?= $(PROJ_FILE)build

CSRC_DIR = src
SRC = $(wildcard $(CSRC_DIR)/*.c)
OBJ = $(patsubst %.c,$(APP_BUILD_DIR)/%.o,$(SRC))
DEP = $(OBJ:.o=.d)

OUT_DIRS = $(dir $(OBJ))

LDSCRIPT_NAME = $(APP_BUILD_DIR)/$(APP_NAME).ld

# file to (dist)clean
# objects and compilation related
TODEL_CLEAN += $(OBJ) $(LDSCRIPT_NAME)
# targets
TODEL_DISTCLEAN += $(APP_BUILD_DIR)

.PHONY: app

#############################################################
# build targets (driver, core, SoC, Board... and local)

all: $(APP_BUILD_DIR) alldeps app

############################################################
# eplicit dependency on the application libs and drivers
# compiling the application requires the compilation of its
# dependencies
#
## library dependencies
LIBDEP := $(BUILD_DIR)/libs/libdfu/libdfu.a \
		  $(BUILD_DIR)/libs/libstd/libstd.a \
		  $(BUILD_DIR)/libs/libfirmware/libfirmware.a


libdep: $(LIBDEP)

$(LIBDEP):
	$(Q)$(MAKE) -C $(PROJ_FILES)libs/$(patsubst lib%.a,%,$(notdir $@))


# drivers dependencies
SOCDRVDEP := $(BUILD_DIR)/drivers/libusb/libusb.a

socdrvdep: $(SOCDRVDEP)

$(SOCDRVDEP):
	$(Q)$(MAKE) -C $(PROJ_FILES)drivers/socs/$(SOC)/$(patsubst lib%.a,%,$(notdir $@))

# board drivers dependencies
BRDDRVDEP    :=

brddrvdep: $(BRDDRVDEP)

$(BRDDRVDEP):
	$(Q)$(MAKE) -C $(PROJ_FILES)drivers/boards/$(BOARD)/$(patsubst lib%.a,%,$(notdir $@))

# external dependencies
EXTDEP    := $(BUILD_DIR)/libs/libsign/libsign.a

extdep: $(EXTDEP)

$(EXTDEP):
	$(Q)$(MAKE) -C $(PROJ_FILES)externals


alldeps: libdep socdrvdep brddrvdep extdep

##########################################################

app: $(APP_BUILD_DIR)/$(ELF_NAME) $(APP_BUILD_DIR)/$(HEX_NAME)

$(APP_BUILD_DIR)/%.o: %.c
	$(call if_changed,cc_o_c)


# ELF
$(APP_BUILD_DIR)/$(ELF_NAME): $(OBJ)
	$(call if_changed,link_o_target)

# HEX
$(APP_BUILD_DIR)/$(HEX_NAME): $(APP_BUILD_DIR)/$(ELF_NAME)
	$(call if_changed,objcopy_ihex)

# BIN
$(APP_BUILD_DIR)/$(BIN_NAME): $(APP_BUILD_DIR)/$(ELF_NAME)
	$(call if_changed,objcopy_bin)

$(APP_BUILD_DIR):
	$(call cmd,mkdir)


-include $(DEP)
