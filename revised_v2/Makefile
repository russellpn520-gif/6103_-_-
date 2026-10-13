# Distributed Flight Information System - Makefile
#
#   make          build server and client
#   make clean    remove build output
#
# Works on macOS/Linux and on Windows with MinGW-w64 (e.g. MSYS2: mingw32-make).

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -g -I include
LDFLAGS =

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

# Objects
COMMON_OBJS = $(COMMON_SRCS:.cpp=.o)
SERVER_OBJS = $(SERVER_SRCS:.cpp=.o)
CLIENT_OBJS = $(CLIENT_SRCS:.cpp=.o)
ALL_OBJS = $(sort $(COMMON_OBJS) $(SERVER_OBJS) $(CLIENT_OBJS))

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

# Compile; -MMD -MP also records header dependencies so that changing a
# header rebuilds every file that includes it.
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(ALL_OBJS:.o=.d)

clean:
	rm -f $(ALL_OBJS) $(ALL_OBJS:.o=.d)
	rm -f $(SERVER_BIN) $(CLIENT_BIN)
	@echo "Cleaned"

help:
	@echo "Targets:"
	@echo "  all     - Build server and client"
	@echo "  clean   - Remove build files"
	@echo "  help    - Show this help"

.PHONY: all clean help
