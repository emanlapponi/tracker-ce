# Variables to provide (example values):
# TOOLCHAIN=/usr/local/bin/arm-none-eabi-
# PLATFORM=MK66 (MK66 and IMX are valid values)
# DEBUG=1 (any value turns on)

# Target configuration
TARGET		:= libTracker.a

# Build environment setup
.DEFAULT_GOAL := build

BUILD_DIR   := build-$(PLATFORM)-$(PRODUCT)
OBJ_DIR 	:= $(BUILD_DIR)/obj/
RM 			:= rm -rf

CPP_SRCS 	+= $(shell find ./source/ -name *.cpp)
CPP_SRCS 	+= $(shell find ./libs/ -name *.cpp)
CPP_SRCS 	+= $(shell find ./interface/ -name *.cpp)
CPP_SRCS 	+= $(shell find ./effector/ -name *.cpp)
CPP_SRCS 	+= $(shell find ./FileManager/ -name *.cpp)
CPP_SRCS 	+= $(shell find ./Exporter/ -name *.cpp)
CPP_SRCS 	+= $(shell find ./AudioEngine/ -name *.cpp)
CPP_SRCS 	+= $(shell find ./synth/ -name *.cpp)
C_SRCS 	 	+= $(shell find ./libs/ -name *.c)

_CPP_OBJS	= $(patsubst ./%.cpp, %.o, $(CPP_SRCS))
_C_OBJS		= $(patsubst ./%.c, %.o, $(C_SRCS))

OBJS		+= $(addprefix $(OBJ_DIR)/, $(_CPP_OBJS))
OBJS		+= $(addprefix $(OBJ_DIR)/, $(_C_OBJS))

OBJS_SORTED := $(sort $(OBJS))
OBJ_DIRS	:= $(dir $(OBJS_SORTED))
DEPS		:= $(OBJS_SORTED:.o=.d)

INCLUDES=-I./include -I./config -I./source -I./synth -I./libs -I./libs/Audio -I./libs/ElapsedMillis -I./libs/SerialFlash -I./libs/SPI -I./libs/Audio/utility -I./libs/FT812 -I./libs/fonts -I./libs/NewDisplay -I./interface -I./effector -I./FileManager -I./Exporter -I./AudioEngine -I./libs/Audio/Polyverb -I./AudioEngine/ConstantValues -I./AudioEngine/MasterEnhancer -I${POLYUTIL_DIR}/include -I${POLYUTIL_DIR}/3rdparty -I${POLYSYNTH_DIR}/include  
COMPILER_OPTIONS=-MMD -MP -fno-common -g -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -fstack-usage -specs=nano.specs -Wall -Wextra -c -ffunction-sections -fdata-sections -ffreestanding -fno-builtin 
ifdef DEBUG
COMPILER_OPTIONS+=-Og -DDEBUG
else
COMPILER_OPTIONS+=-O2 -DNDEBUG
endif

CPP_OPTIONS=-std=gnu++20 -felide-constructors -includecstdint -fno-rtti -fno-exceptions 
CC_OPTIONS=

DEFINES+=-DAUDIO_PERFORMANCE_INFO_ENABLED -DMEMORY_USAGE_INFO_ENABLED -DAUDIO_NULL_IS_SILENCE=1 -D__NEWLIB__ -DKINETISK -D__MK66FX1M0__ -DF_CPU=180000000 -DUSB_MIDI -DTEENSYDUINO=142 -DARDUINO=10805 -DLAYOUT_US_ENGLISH -DCPU_MK66FX1M0VMD18
POLYUTIL_DIR ?= ../util
POLYSYNTH_DIR ?= ../synth
-include ../version.mk
DEFINES += $(FW_VERSION_DEFINES) $(CUSTOM_DEFINES)

ifeq ($(PLATFORM), MK66)
COMPILER_OPTIONS+=-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 
else ifeq ($(PLATFORM), IMX)
COMPILER_OPTIONS+=-mcpu=cortex-m7 -mfpu=fpv5-sp-d16 -mfp16-format=ieee
DEFINES+=-DPLATFORM_IMX -DFEATURE_SYNTH
else 
$(error Invalid PLATFORM value $(PLATFORM))
endif

ifeq ($(PRODUCT), MINI)
DEFINES+=-DTRACKER_MINI
else ifeq ($(PRODUCT), ORIGINAL)
else ifeq ($(PRODUCT), PLUS)
else 
$(error Invalid PRODUCT value $(PRODUCT))
endif




# Build rules

$(OBJ_DIR)/%.o: %.cpp
	$(TOOLCHAIN)c++ $(DEFINES) $(INCLUDES) $(CPP_OPTIONS) $(COMPILER_OPTIONS) -o "$@" "$<"

$(OBJ_DIR)/%.o: %.c
	$(TOOLCHAIN)gcc $(DEFINES) $(INCLUDES) $(CC_OPTIONS) $(COMPILER_OPTIONS) -o "$@" "$<"

$(TARGET): $(OBJS)
	@rm -f "$(BUILD_DIR)/$(TARGET)"
	$(TOOLCHAIN)ar -r "$(BUILD_DIR)/$(TARGET)" $(OBJS_SORTED)
	$(MAKE) --no-print-directory post-build
	
# Main rules

build:
	$(MAKE) --no-print-directory pre-build
	$(MAKE) --no-print-directory main-build

main-build: $(TARGET)

pre-build:
	$(foreach dir,$(sort $(OBJ_DIRS)),mkdir -p $(dir);)
post-build: 

clean:
	-$(RM) $(BUILD_DIR)
	
-include $(DEPS)

.PHONY: build
