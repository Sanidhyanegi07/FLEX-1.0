CC = gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -pedantic
INCLUDES = -Icore-c/include

SRC = core-c/src/command_parser.c core-c/src/state_manager.c core-c/src/orchestrator.c
TEST_SRC = core-c/tests/test_command_parser.c core-c/tests/test_state_manager.c core-c/tests/test_orchestrator.c
BRIDGE_SRC = $(SRC) core-c/src/bridge_main.c
TARGET = test_command_parser.exe
BRIDGE_TARGET = flex_bridge.exe

.PHONY: all test bridge clean

all: test bridge

$(TARGET): $(SRC) $(TEST_SRC)
	$(CC) $(CFLAGS) $(INCLUDES) $(SRC) $(TEST_SRC) -o $(TARGET)

$(BRIDGE_TARGET): $(BRIDGE_SRC)
	$(CC) $(CFLAGS) $(INCLUDES) $(BRIDGE_SRC) -o $(BRIDGE_TARGET)

bridge: $(BRIDGE_TARGET)

test: $(TARGET)
	./$(TARGET)

clean:
	-rm -f $(TARGET) $(BRIDGE_TARGET)
