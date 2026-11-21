# Variables to provide (example values):
#
# TOOLCHAIN=/usr/local/bin/arm-none-eabi-
# POLYDSP_DIR=../PolyDSP/
# POLYUTIL_DIR=../PolyUtil/
# TESTS=1 (any value turns on)
# PLATFORM=MK66 (MK66 and IMX are valid values)
# DEBUG=1 (any value turns on)
# CUSTOM_COMPILER_OPTIONS="-Wno-error=double-promotion" (anything you want to pass to gcc)

# Target configuration
ifdef TESTS
TARGET		:= polysynth-tests
else ifdef REGRESSION_TESTS
TARGET		:= polysynth-regression-tests-$(PRODUCT)
else
TARGET		:= libPolySynth.a
endif

# Build environment setup
.DEFAULT_GOAL := build

POLYUTIL_DIR ?= ../util
POLYDSP_DIR ?= .

ifdef TESTS
BUILD_DIR	:= build-tests
else ifdef REGRESSION_TESTS
BUILD_DIR	:= build-regression-tests
else
BUILD_DIR := build-$(PLATFORM)-$(PRODUCT)
endif
OBJ_DIR 	:= $(BUILD_DIR)/obj/
RM 			:= rm -rf

CLANG_TIDY  := clang-tidy

CPP_SRCS 	+= $(shell find ./source -name *.cpp)
CPP_SRCS 	+= $(shell find ./config -name *.cpp)
CPP_SRCS 	+= ${POLYUTIL_DIR}/source/AudioCS.cpp
LINT_SRCS   := $(CPP_SRCS)
DEP_SRCS    := $(CPP_SRCS)
CPP_SRCS 	+= $(shell find ./3rdparty -name *.cpp 2>/dev/null)
CPP_SRCS 	+= $(shell find ${POLYUTIL_DIR}/3rdparty/fmt -name *.cc)
C_SRCS 	 	+= $(shell find ./3rdparty -name *.c 2>/dev/null)
C_SRCS 	 	+= $(shell find ${POLYUTIL_DIR}/3rdparty -name *.c)
C_SRCS 	 	+= $(shell find ./source -name *.c)
ASM_SRCS 	+= $(shell find ./3rdparty -name *.S 2>/dev/null)
ifdef TESTS
TEST_SRCS	+= $(shell find ./tests -path "./tests/regression" -prune -o -name "*.cpp" -print)
DEP_SRCS    += $(TEST_SRCS)
TEST_SRCS 	+= $(shell find ${POLYUTIL_DIR}/3rdparty/catch2 -name *.cpp)
TEST_SRCS 	+= $(shell find ${POLYUTIL_DIR}/3rdparty/google* -name *.cc -and -not -name "g*-all.cc" -and -not -name "g*_main.cc")
endif
ifdef REGRESSION_TESTS
REGRESSION_SRCS 	+= $(shell find ./tests/regression -name *.cpp)
REGRESSION_SRCS 	+= ${POLYUTIL_DIR}/source/WavIO.cpp
endif

_CPP_OBJS	= $(patsubst $(POLYUTIL_DIR)/%, PolyUtil/%, $(patsubst ./%.o, %.o, $(patsubst %.cc, %.o, $(patsubst %.cpp, %.o, $(CPP_SRCS)))))
_TEST_OBJS	= $(patsubst $(POLYUTIL_DIR)/%, PolyUtil/%, $(patsubst ./%.o, %.o, $(patsubst %.cpp, %.o, $(TEST_SRCS))))
_TEST_OBJS	:= $(patsubst %.cc, %.o, $(_TEST_OBJS))
_REGRESSION_OBJS = $(patsubst $(POLYUTIL_DIR)/%, PolyUtil/%, $(patsubst ./%.o, %.o, $(patsubst %.cpp, %.o, $(REGRESSION_SRCS))))
_REGRESSION_OBJS := $(patsubst %.cc, %.o, $(_REGRESSION_OBJS))
_C_OBJS		= $(patsubst $(POLYUTIL_DIR)/%, PolyUtil/%, $(patsubst ./%.o, %.o, $(patsubst %.c, %.o, $(C_SRCS))))
_ASM_OBJS	= $(patsubst ./%.o, %.o, $(patsubst %.S, %.o, $(ASM_SRCS)))
OBJS		+= $(addprefix $(OBJ_DIR)/, $(_CPP_OBJS))
OBJS		+= $(addprefix $(OBJ_DIR)/, $(_TEST_OBJS))
OBJS		+= $(addprefix $(OBJ_DIR)/, $(_REGRESSION_OBJS))
OBJS		+= $(addprefix $(OBJ_DIR)/, $(_C_OBJS))
OBJS		+= $(addprefix $(OBJ_DIR)/, $(_ASM_OBJS))

OBJ_DIRS	:= $(sort $(dir $(OBJS)))

DEPS		= $(addprefix $(OBJ_DIR)/, $(patsubst $(POLYUTIL_DIR)/%, PolyUtil/%, $(patsubst ./%.d, %.d, $(DEP_SRCS:.cpp=.d))))

INCLUDES=-I./include -I./config -I./source -I./tests -I. -I../synth-plugin-sdk/include -I${POLYUTIL_DIR}/3rdparty/nanopb -I./3rdparty -I${POLYDSP_DIR}/include -I${POLYUTIL_DIR}/3rdparty -I${POLYUTIL_DIR}/include

ifneq (,$(findstring Play,$(PRODUCT)))
DEFINES:=$(DEFINES) -DPRODUCT_PLAY
ifeq ($(PLATFORM), IMX)
DEFINES:=$(DEFINES) -DPRODUCT_PLAY_PLUS
else ifneq (,$(findstring Plus,$(PRODUCT)))
DEFINES:=$(DEFINES) -DPRODUCT_PLAY_PLUS
endif
endif
ifeq ($(PRODUCT), Wave)
DEFINES:=$(DEFINES) -DPRODUCT_WAVE
endif
ifeq ($(PRODUCT), TrackerPlus)
DEFINES:=$(DEFINES) -DPRODUCT_TRACKER_PLUS
endif

COMPILER_OPTIONS=-Wall -Wextra -Werror -c -fno-builtin -fno-common -ffunction-sections -fdata-sections -ffreestanding -fsingle-precision-constant -ffast-math
CPP_OPTIONS=-std=c++20 -includecstdint -felide-constructors -includecstdint -Wno-psabi
CC_OPTIONS=
ifdef TESTS
DEFINES+=-DUNIT_TESTS -DDEBUG -DGTEST_DONT_DEFINE_SUCCEED=1 -DGTEST_DONT_DEFINE_FAIL=1 -DCATCH_AMALGAMATED_CUSTOM_MAIN
INCLUDES+=-I${POLYUTIL_DIR}/3rdparty/googlemock/include -I${POLYUTIL_DIR}/3rdparty/googletest/include -I${POLYUTIL_DIR}/3rdparty/googletest
COMPILER_OPTIONS+=-O0 -g3 -c
ifeq ($(PLATFORM), DARWIN)
DEAD_STRIP=-Wl,-dead_strip
else
DEAD_STRIP=-Wl,--gc-sections
endif
else ifdef REGRESSION_TESTS
DEFINES+=-DREGRESSION_TESTS
COMPILER_OPTIONS+=-c
ifdef DEBUG
COMPILER_OPTIONS+=-O0 -g3 -DDEBUG
else
COMPILER_OPTIONS+=-O3 -DNDEBUG
endif
DEFINES:=$(DEFINES) -DTEST_DATA_PATH=\"$(CURDIR)/tests-data/regression\"
REGRESSION_LD_FLAGS=-lsndfile
else
DEFINES+=-DCPU_MIMXRT1051CVL5B -DCPU_MIMXRT1051CVL5B_cm7 -DFSL_RTOS_BM -DSDK_OS_BAREMETAL -DSDK_DEBUGCONSOLE=0 -D__MCUXPRESSO -D__USE_CMSIS -D__NEWLIB__ -DFMT_STATIC_THOUSANDS_SEPARATOR="','"
CPP_OPTIONS+=-fno-exceptions -fno-rtti
ifdef DEBUG
COMPILER_OPTIONS+=-Og -DDEBUG
else
COMPILER_OPTIONS+=-O3 -DNDEBUG
endif
COMPILER_OPTIONS+=-g -mfloat-abi=hard -mthumb -fstack-usage -specs=nano.specs
ifeq ($(PLATFORM), MK66)
COMPILER_OPTIONS+=-mcpu=cortex-m4 -mfpu=fpv4-sp-d16
else ifeq ($(PLATFORM), IMX)
COMPILER_OPTIONS+=-mcpu=cortex-m7 -mfpu=fpv5-sp-d16 -mfp16-format=ieee
else 
$(error Invalid PLATFORM value $(PLATFORM))
endif
endif
CLANG_TIDY_OPTIONS  := -warnings-as-errors=* -- ${INCLUDES} ${DEFINES} -std=c++20 -cl-single-precision-constant
COMPILER_OPTIONS+=$(CUSTOM_COMPILER_OPTIONS)

# Build rules

$(OBJ_DIR)/PolyUtil/%.o: $(POLYUTIL_DIR)/%.cpp
	$(TOOLCHAIN)c++ -MMD -MP $(DEFINES) $(INCLUDES) $(CPP_OPTIONS) $(COMPILER_OPTIONS) -o "$@" "$<"

$(OBJ_DIR)/%.o: %.cpp
	$(TOOLCHAIN)c++ -MMD -MP $(DEFINES) $(INCLUDES) $(CPP_OPTIONS) $(COMPILER_OPTIONS) -o "$@" "$<"

$(OBJ_DIR)/PolyUtil/%.o: $(POLYUTIL_DIR)/%.cc
	$(TOOLCHAIN)c++ $(DEFINES) $(INCLUDES) $(CPP_OPTIONS) $(COMPILER_OPTIONS) -o "$@" "$<"

$(OBJ_DIR)/%.o: %.cc
	$(TOOLCHAIN)c++ $(DEFINES) $(INCLUDES) $(CPP_OPTIONS) $(COMPILER_OPTIONS) -o "$@" "$<"

$(OBJ_DIR)/PolyUtil/%.o: $(POLYUTIL_DIR)/%.c
	$(TOOLCHAIN)gcc $(DEFINES) $(INCLUDES) $(CC_OPTIONS) $(COMPILER_OPTIONS) -o "$@" "$<"

$(OBJ_DIR)/%.o: %.c
	$(TOOLCHAIN)gcc $(DEFINES) $(INCLUDES) $(CC_OPTIONS) $(COMPILER_OPTIONS) -o "$@" "$<"

$(OBJ_DIR)/%.o: %.S
	$(TOOLCHAIN)gcc -c -x assembler-with-cpp $(DEFINES) $(INCLUDES) -g3 -o "$@" "$<"

$(TARGET): $(OBJS)
ifdef TESTS
	@$(TOOLCHAIN)g++ -o "$(BUILD_DIR)/$(TARGET)" $(OBJS) $(LIBS) $(DEAD_STRIP)
else ifdef REGRESSION_TESTS
	@$(TOOLCHAIN)g++ -o "$(BUILD_DIR)/$(TARGET)" $(OBJS) $(LIBS) $(REGRESSION_LD_FLAGS) $(DEAD_STRIP)
else
	@rm -f "$(BUILD_DIR)/$(TARGET)"
	@$(TOOLCHAIN)ar -r "$(BUILD_DIR)/$(TARGET)" $(OBJS) $(LIBS)
endif
	@$(MAKE) --no-print-directory post-build
	
# Main rules

build:
	@$(MAKE) --no-print-directory pre-build
	@$(MAKE) --no-print-directory main-build

main-build: $(TARGET)

pre-build:
	@$(foreach dir,$(OBJ_DIRS),mkdir -p $(dir);)

post-build: 

ifdef TESTS
run-tests: build
	$(BUILD_DIR)/$(TARGET)
endif

ifdef REGRESSION_TESTS
run-regression-tests: build
	$(BUILD_DIR)/$(TARGET)
endif

clean:
	-$(RM) $(BUILD_DIR)

clang-tidy:
	$(CLANG_TIDY) --config-file=${POLYUTIL_DIR}/.clang-tidy $(LINT_SRCS) $(CLANG_TIDY_OPTIONS)

clang-tidy-diff:
	git diff --name-only | grep cpp | xargs -t -I {} $(CLANG_TIDY) --config-file=${POLYUTIL_DIR}/.clang-tidy {} $(CLANG_TIDY_OPTIONS)

docs: 
	@doxygen

docs-open: docs
	@open docs-gen/html/index.html

.PHONY: build clang-tidy docs

$(DEPS):

include $(DEPS)
