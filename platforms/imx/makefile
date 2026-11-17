# Variables to provide (example values):
#
# TOOLCHAIN=/usr/local/bin/arm-none-eabi-
# TRACKER_DIR=../Tracker/
# TARGET_POSTFIX=mybuild
# DEBUG=1 (any value turns on)

# Target configuration
TRACKER_DIR	?= ../../core
POLYUTIL_DIR	?= ../../util
POLYSYNTH_DIR	?= ../../synth
-include ../../version.mk
-include ../version.mk
TARGET_POSTFIX	?= $(DEFAULT_TARGET_POSTFIX)
BUILD_ID_STR	?= $(TARGET_POSTFIX)

ifeq ($(PRODUCT), MINI)
TARGET		:= TrackerMini
else ifeq ($(PRODUCT), PLUS)
TARGET		:= TrackerPlus
else 
$(error Invalid PRODUCT value $(PRODUCT))
endif
# Build environment setup
.DEFAULT_GOAL := build

BUILD_DIR	:= build-$(PRODUCT)
OBJ_DIR 	:= $(BUILD_DIR)/obj/
RM 			:= rm -rf

CPP_SRCS 	+= $(shell find ./3rdparty/ -name *.cpp)
CPP_SRCS 	+= $(shell find ./sdmmc/ -name *.cpp)
CPP_SRCS 	+= $(shell find ./source/ -name *.cpp)
CPP_SRCS 	+= $(shell find ./startup/ -name *.cpp)
C_SRCS 	 	+= $(shell find ./3rdparty/ -name *.c)
C_SRCS 	 	+= $(shell find ./source/ -name *.c)
C_SRCS 	 	+= $(shell find ./board/ -name *.c)
C_SRCS 	 	+= $(shell find ./component/ -name *.c)
C_SRCS 	 	+= $(shell find ./device/ -name *.c)
C_SRCS 	 	+= $(shell find ./drivers/ -name *.c)
C_SRCS 	 	+= $(shell find ./fatfs/ -name *.c)
C_SRCS 	 	+= $(shell find ./sdmmc/ -name *.c)
C_SRCS 	 	+= $(shell find ./usb/ -name *.c)
C_SRCS 	 	+= $(shell find ./utilities/ -name *.c)
ASM_SRCS 	+= $(shell find ./3rdparty/ -name *.S)
ASM_SRCS 	+= $(shell find ./source/ -name *.S)

_CPP_OBJS	= $(patsubst ./%.cpp, %.o, $(CPP_SRCS))
_C_OBJS		= $(patsubst ./%.c, %.o, $(C_SRCS))
_ASM_OBJS	= $(patsubst ./%.S, %.o, $(ASM_SRCS))
OBJS		+= $(addprefix $(OBJ_DIR)/, $(_CPP_OBJS))
OBJS		+= $(addprefix $(OBJ_DIR)/, $(_C_OBJS))
OBJS		+= $(addprefix $(OBJ_DIR)/, $(_ASM_OBJS))

OBJS_SORTED := $(sort $(OBJS))
OBJ_DIRS	:= $(dir $(OBJS_SORTED))
DEPS		:= $(OBJS_SORTED:.o=.d)

NOW := $(shell date +"%Y%m%d_%H%M")

INCLUDES=-I./drivers -I./component/serial_manager -I./utilities -I./component/lists -I./component/osa -I./device -I./CMSIS -I./drivers -I./component/serial_manager -I./utilities -I./component/lists -I./CMSIS -I./device -I./board -I./component/osa -I./config -I./source -I./3rdparty -I./3rdparty/Audio -I./3rdparty/MIDI -I./3rdparty/FT812 -I./source/Drivers -I./usb -I./usb/device/class/cdc -I./usb/include -I./usb/device/class -I./usb/device/source -I./usb/device/include -I./usb/device/source/ehci -I./usb/phy -I./usb/device/class/audio -I./usb/device/class/msc -I./sdmmc/host -I./sdmmc/osa -I./sdmmc/inc -I./fatfs/source -I./fatfs/source/fsl_sd_disk -I./fatfs/source/fsl_ram_disk -I./sdmmc/sd -I${POLYSYNTH_DIR}/include -I${POLYUTIL_DIR}/include -I${POLYUTIL_DIR}/3rdparty -I${TRACKER_DIR}/include

DEFINES=-DCPU_MIMXRT1051CVL5B -DI2C_RETRY_TIMES=20000 -DCPU_MIMXRT1051CVL5B_cm7 -DSDK_OS_BAREMETAL -D__MCUXPRESSO -D__USE_CMSIS -DNDEBUG -D__NEWLIB__ -DDEBUG_CONSOLE_RX_ENABLE=0 -DSDK_DEBUGCONSOLE=1 -DSERIAL_PORT_TYPE_VIRTUAL=1 -DDEBUG_CONSOLE_TRANSFER_NON_BLOCKING -DXIP_BOOT_HEADER_ENABLE=1 -DXIP_EXTERNAL_FLASH=1 -DXIP_BOOT_HEADER_DCD_ENABLE=0 -DSD_ENABLED -DBUILD_ID=\"$(BUILD_ID_STR)\"
DEFINES += $(CUSTOM_DEFINES)

ifeq ($(PRODUCT), MINI)
DEFINES+=-DTRACKER_MINI
else ifeq ($(PRODUCT), PLUS)
INCLUDES+=-I./3rdparty/Leds -I./3rdparty/radio
else 
$(error Invalid PRODUCT value $(PRODUCT))
endif

COMPILER_OPTIONS+=-fno-common -g -mcpu=cortex-m7 -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -D__NEWLIB__ -fstack-usage -specs=nano.specs -Wall -c -ffunction-sections -fdata-sections -ffreestanding -fno-builtin
ifdef DEBUG
COMPILER_OPTIONS+=-Og -DDEBUG
else
COMPILER_OPTIONS+=-O2 -DNDEBUG
endif

CPP_OPTIONS=-std=gnu++17 -felide-constructors -includecstdint -fno-rtti -fno-exceptions
CC_OPTIONS=

# All synth engines are loaded from SD card /Synths/*.psyn at runtime
SYNTH_ENGINE_LIBS ?=
LIBS:= -Wl,--start-group -lTracker -larm_cortexM4lf_math -lPolySynth $(SYNTH_ENGINE_LIBS) -Wl,--end-group -Xlinker -z -Xlinker muldefs
 


# Build rules

$(OBJ_DIR)/%.o: %.cpp config/config.h
	$(TOOLCHAIN)c++ -MMD -MP $(DEFINES) $(INCLUDES) $(CPP_OPTIONS) $(COMPILER_OPTIONS) -o "$@" "$<"

$(OBJ_DIR)/%.o: %.c config/config.h
	$(TOOLCHAIN)gcc -MMD -MP $(DEFINES) $(INCLUDES) $(CC_OPTIONS) $(COMPILER_OPTIONS) -o "$@" "$<"

$(OBJ_DIR)/%.o: %.S config/config.h
	$(TOOLCHAIN)gcc -c -x assembler-with-cpp -mcpu=cortex-m7 -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb $(DEFINES) $(INCLUDES) -g -o "$@" "$<"
 
$(TARGET): $(OBJS_SORTED)
	@$(TOOLCHAIN)c++ -L ./linker -L ./linker/$(TARGET) -L ./3rdparty/math -L ${POLYSYNTH_DIR}/build-IMX-TrackerPlus -L${TRACKER_DIR}/build-IMX-$(PRODUCT) -Xlinker --defsym=__rtc_localtime=0 -Xlinker -print-memory-usage -Xlinker --gc-sections -mcpu=cortex-m7 -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -nostdlib -T linkerScript.ld -u _printf_float -o "$(BUILD_DIR)/$(TARGET).axf" $(OBJS_SORTED) $(LIBS)
	@$(MAKE) --no-print-directory post-build
	
# Main rules

build:
	@$(MAKE) --no-print-directory pre-build
	@$(MAKE) --no-print-directory main-build

main-build: $(TARGET)

pre-build:
	@$(foreach dir,$(OBJ_DIRS),mkdir -p $(dir);)

post-build: 
	cp "$(BUILD_DIR)/$(TARGET).axf" "$(BUILD_DIR)/$(TARGET)_$(NOW)_$(TARGET_POSTFIX).axf"
	$(TOOLCHAIN)objcopy --gap-fill=0xFF -v -O ihex "$(BUILD_DIR)/$(TARGET).axf" "$(BUILD_DIR)/$(TARGET)_$(TARGET_POSTFIX).ptf" 
#	$(TOOLCHAIN)nm -S --size-sort -s "$(BUILD_DIR)/$(TARGET).axf" > "$(BUILD_DIR)/$(TARGET).txt"
#	$(TOOLCHAIN)nm -S --numeric-sort -s "$(BUILD_DIR)/$(TARGET).axf" > "$(BUILD_DIR)/$(TARGET).txt"

build-lss: build
	$(TOOLCHAIN)objdump -S -C "$(BUILD_DIR)/$(TARGET).axf" > "$(BUILD_DIR)/$(TARGET)_$(NOW)_$(TARGET_POSTFIX).lss" 
	$(TOOLCHAIN)size "$(BUILD_DIR)/$(TARGET).axf"

clean:
	-$(RM) $(BUILD_DIR)

-include $(DEPS)
	
.PHONY: build
