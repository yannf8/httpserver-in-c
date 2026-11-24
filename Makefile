# httpserver-in-c — a guided build of an HTTP server on raw Linux syscalls.
#
#   make                     build everything
#   make test                run every unit test
#   make test MODULE=2       run module 2 only
#   make test SECTION=2.3    run one section
#   make test FILTER=writev  run tests whose name matches
#   make integration         run the end-to-end tests against your servers
#   make status              the ladder: how far up are you?
#   make list                every test, with its section id
#   make run                 start your blocking server on port 8080
#   make gdb SECTION=2.3     run one section under gdb, no fork isolation
#   make SAN=1 test          rebuild with ASan+UBSan and run the tests
#   make clean

CXX      ?= g++
CXXSTD   ?= -std=c++20
OPT      ?= -O1
WARN     ?= -Wall -Wextra -Wno-unused-parameter
INCLUDES := -Iinclude -Itests/framework
CXXFLAGS ?= $(CXXSTD) $(OPT) -g $(WARN) $(INCLUDES)
LDFLAGS  ?=

ifdef SAN
CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS  += -fsanitize=address,undefined
endif

BUILD := build
OBJ   := $(BUILD)/obj

# Everything you implement.
LIB_SRCS  := $(wildcard src/mod*.cpp)
LIB_OBJS  := $(patsubst src/%.cpp,$(OBJ)/src/%.o,$(LIB_SRCS))

# The test harness plus the per-module test files.
TEST_SRCS := tests/framework/runner.cpp $(wildcard tests/unit/*.cpp)
TEST_OBJS := $(patsubst tests/%.cpp,$(OBJ)/tests/%.o,$(TEST_SRCS))

BINS := $(BUILD)/unit_tests $(BUILD)/server_blocking $(BUILD)/server_epoll $(BUILD)/bench

# Test selection, shared by `make test` and `make integration`.
SELECT :=
ifdef MODULE
SELECT += --module $(MODULE)
endif
ifdef SECTION
SELECT += --section $(SECTION)
endif
ifdef FILTER
SELECT += --filter $(FILTER)
endif

.PHONY: all test integration status list run run-epoll gdb clean help
.DEFAULT_GOAL := all

all: $(BINS)

$(BUILD)/unit_tests: $(TEST_OBJS) $(LIB_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD)/server_blocking: $(OBJ)/src/server_blocking.o $(LIB_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD)/server_epoll: $(OBJ)/src/server_epoll.o $(LIB_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD)/bench: $(OBJ)/src/bench.o $(LIB_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(OBJ)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

test: $(BUILD)/unit_tests
	@$(BUILD)/unit_tests $(SELECT)

integration: $(BINS)
	@python3 tests/integration/run.py $(SELECT)

status: $(BINS)
	@python3 tools/status.py $(if $(FAST),--fast,)

list: $(BUILD)/unit_tests
	@echo "== unit tests =="
	@$(BUILD)/unit_tests --list
	@echo
	@echo "== end-to-end tests =="
	@python3 tests/integration/run.py --list

run: $(BUILD)/server_blocking
	$(BUILD)/server_blocking --port 8080 --docroot www

run-epoll: $(BUILD)/server_epoll
	$(BUILD)/server_epoll --port 8080 --docroot www

# One section, in-process, under the debugger.
gdb: $(BUILD)/unit_tests
	gdb --args $(BUILD)/unit_tests --no-fork $(SELECT)

clean:
	rm -rf $(BUILD)

help:
	@sed -n '1,22p' Makefile

-include $(shell find $(OBJ) -name '*.d' 2>/dev/null)
