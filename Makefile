CC = gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -pedantic
INCLUDES = -Icore-c/include

SRC = core-c/src/command_parser.c core-c/src/state_manager.c core-c/src/orchestrator.c
TEST_SRC = core-c/tests/test_command_parser.c core-c/tests/test_state_manager.c core-c/tests/test_orchestrator.c
TARGET = test_command_parser.exe



.PHONY: all test clean

all: test

$(TARGET): $(SRC) $(TEST_SRC)
	$(CC) $(CFLAGS) $(INCLUDES) $(SRC) $(TEST_SRC) -o $(TARGET)

test: $(TARGET)
	./$(TARGET)

clean:
	-rm -f $(TARGET)
