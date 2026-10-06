# Distributed Flight Information System - Makefile
#
#   make          build server and client
#   make test     build and run the unit tests (needs Google Test)
#   make clean    remove build output
#
# Works on macOS/Linux and on Windows with MinGW-w64 (e.g. MSYS2: mingw32-make).

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -g -I include -I/opt/homebrew/include
LDFLAGS = -L/opt/homebrew/lib

ifeq ($(OS),Windows_NT)
    EXE = .exe
    LDLIBS = -lws2_32
else
    EXE =
    LDLIBS =
endif

# Binaries
SERVER_BIN = server$(EXE)
CLIENT_BIN = client$(EXE)
TEST_BIN = tests_run$(EXE)

# Sources
COMMON_SRCS = src/common/message_serializer.cpp \
              src/common/udp_socket.cpp \
              src/common/logger.cpp

SERVER_SRCS = src/server/flight_server.cpp \
              src/server/flight_manager.cpp \
              src/server/monitor_manager.cpp \
              src/server/request_history.cpp \
              src/server/server_main.cpp

CLIENT_SRCS = src/client/flight_client.cpp \
              src/client/client_ui.cpp \
              src/client/client_main.cpp

TEST_SRCS = tests/test_main.cpp \
            tests/test_message_serializer.cpp \
            tests/test_flight_manager.cpp

# Server sources needed by the tests (everything except server_main.cpp)
TEST_SERVER_SRCS = src/server/flight_manager.cpp \
                   src/server/monitor_manager.cpp \
                   src/server/request_history.cpp

# Objects
COMMON_OBJS = $(COMMON_SRCS:.cpp=.o)
SERVER_OBJS = $(SERVER_SRCS:.cpp=.o)
CLIENT_OBJS = $(CLIENT_SRCS:.cpp=.o)
TEST_OBJS = $(TEST_SRCS:.cpp=.o)
TEST_SERVER_OBJS = $(TEST_SERVER_SRCS:.cpp=.o)
ALL_OBJS = $(sort $(COMMON_OBJS) $(SERVER_OBJS) $(CLIENT_OBJS) $(TEST_OBJS))

# Default target
all: $(SERVER_BIN) $(CLIENT_BIN)

$(SERVER_BIN): $(COMMON_OBJS) $(SERVER_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)
	@echo "Built: $(SERVER_BIN)"

$(CLIENT_BIN): $(COMMON_OBJS) $(CLIENT_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)
	@echo "Built: $(CLIENT_BIN)"

# On Windows also allow "make server" / "make client"
ifneq ($(EXE),)
server: $(SERVER_BIN)
client: $(CLIENT_BIN)
.PHONY: server client
endif

# Check that Google Test can be found
check-gtest:
	@echo '#include <gtest/gtest.h>' > gtest_check.cpp
	@echo 'int main() { return 0; }' >> gtest_check.cpp
	@if ! $(CXX) $(CXXFLAGS) gtest_check.cpp -o gtest_check$(EXE) 2>/dev/null; then \
		rm -f gtest_check.cpp gtest_check$(EXE); \
		echo ""; \
		echo "Error: Google Test is not installed."; \
		echo "  macOS:  brew install googletest   (or ./tools/install_gtest.sh)"; \
		echo "  Ubuntu: sudo apt-get install libgtest-dev"; \
		echo "The semantics experiment does not need it: ./tools/test_semantics.sh"; \
		echo ""; \
		exit 1; \
	fi
	@rm -f gtest_check.cpp gtest_check$(EXE)
	@echo "Google Test found"

# Unit tests (need Google Test)
$(TEST_BIN): check-gtest $(COMMON_OBJS) $(TEST_SERVER_OBJS) $(TEST_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(COMMON_OBJS) $(TEST_SERVER_OBJS) $(TEST_OBJS) $(LDFLAGS) -lgtest -lgtest_main -pthread $(LDLIBS)
	@echo "Built: $(TEST_BIN)"

# Compile; -MMD -MP also records header dependencies so that changing a
# header rebuilds every file that includes it.
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(ALL_OBJS:.o=.d)

clean:
	rm -f $(ALL_OBJS) $(ALL_OBJS:.o=.d)
	rm -f $(SERVER_BIN) $(CLIENT_BIN) $(TEST_BIN)
	@echo "Cleaned"

test: $(TEST_BIN)
	./$(TEST_BIN)

help:
	@echo "Targets:"
	@echo "  all     - Build server and client"
	@echo "  test    - Build and run the unit tests (requires Google Test)"
	@echo "  clean   - Remove build files"
	@echo "  help    - Show this help"

.PHONY: all clean test help check-gtest
