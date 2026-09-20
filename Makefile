CC ?= cc

PROGRAM := alife
SRC_DIR := src
TEST_DIR := tests
BUILD_ROOT := build

STANDARD_FLAGS := -std=c11
WARNING_FLAGS := -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
	-Wstrict-prototypes -Wmissing-prototypes
DEPENDENCY_FLAGS := -MMD -MP

CPPFLAGS += -Iinclude
CFLAGS ?= -O2 -g
LDFLAGS ?=
LDLIBS += -lm

SANITIZE ?= 0
ifeq ($(SANITIZE),1)
BUILD_DIR := $(BUILD_ROOT)/sanitize
SANITIZER_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
CFLAGS += -O1 -g3 $(SANITIZER_FLAGS)
LDFLAGS += $(SANITIZER_FLAGS)
TEST_ENV := ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1
else
BUILD_DIR := $(BUILD_ROOT)
TEST_ENV :=
endif

ALL_CFLAGS := $(STANDARD_FLAGS) $(WARNING_FLAGS) $(DEPENDENCY_FLAGS) $(CFLAGS)

ALL_SOURCES := $(wildcard $(SRC_DIR)/*.c)
MAIN_SOURCE := $(SRC_DIR)/main.c
LIB_SOURCES := $(filter-out $(MAIN_SOURCE),$(ALL_SOURCES))
APP_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(ALL_SOURCES))
LIB_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(LIB_SOURCES))
TEST_OBJECT := $(BUILD_DIR)/$(TEST_DIR)/test_runner.o

APP_BINARY := $(BUILD_DIR)/$(PROGRAM)
TEST_BINARY := $(BUILD_DIR)/test_runner
DEPENDENCIES := $(APP_OBJECTS:.o=.d) $(TEST_OBJECT:.o=.d)

.PHONY: all test sanitize clean

all: $(APP_BINARY)

$(APP_BINARY): $(APP_OBJECTS)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(TEST_BINARY): $(LIB_OBJECTS) $(TEST_OBJECT)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(ALL_CFLAGS) -c $< -o $@

test: $(TEST_BINARY)
	$(TEST_ENV) ./$(TEST_BINARY)

sanitize:
	$(MAKE) SANITIZE=1 all test

clean:
	rm -rf -- $(BUILD_ROOT)

-include $(DEPENDENCIES)
