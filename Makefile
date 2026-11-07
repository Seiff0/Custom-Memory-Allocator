CC = gcc
CPPFLAGS ?= -Iinclude
CFLAGS ?= -std=c11 -Wall -Wextra -pthread
BUILD := build
SOURCE := src/mymalloc.c
TESTS := test_overflow test_next_fit advanced_test_mymalloc
ifeq ($(OS),Windows_NT)
EXE := .exe
MKDIR = if not exist $(BUILD) mkdir $(BUILD)
CLEAN = if exist $(BUILD) rmdir /s /q $(BUILD)
else
EXE :=
MKDIR = mkdir -p $(BUILD)
CLEAN = rm -rf $(BUILD)
endif
BINS := $(addsuffix $(EXE),$(addprefix $(BUILD)/,$(TESTS)))

.PHONY: all test clean
all: $(BINS)

$(BUILD):
	$(MKDIR)

$(BUILD)/%$(EXE): tests/%.c $(SOURCE) include/mymalloc.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCE) $< -o $@

test: all
	$(BUILD)/test_overflow$(EXE)
	$(BUILD)/test_next_fit$(EXE)
	$(BUILD)/advanced_test_mymalloc$(EXE)

clean:
	$(CLEAN)
