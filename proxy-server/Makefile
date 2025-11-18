# ProxyMax - Multithreaded Proxy Server Makefile

CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread -I./include
LDFLAGS = -pthread

# Directories
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = .

# Target executable
TARGET = $(BIN_DIR)/proxymax

# Source files
SRCS = $(SRC_DIR)/main.c \
       $(SRC_DIR)/core/server.c \
       $(SRC_DIR)/core/connection.c \
       $(SRC_DIR)/core/threading.c \
       $(SRC_DIR)/protocols/http.c \
       $(SRC_DIR)/protocols/socks5.c \
       $(SRC_DIR)/cache/cache.c \
       $(SRC_DIR)/auth/auth.c \
       $(SRC_DIR)/filter/filter.c \
       $(SRC_DIR)/monitor/logger.c \
       $(SRC_DIR)/monitor/stats.c \
       $(SRC_DIR)/utils/config.c \
       $(SRC_DIR)/utils/network.c

# Object files
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

# Create object directory structure
OBJ_SUBDIRS = $(OBJ_DIR) \
              $(OBJ_DIR)/core \
              $(OBJ_DIR)/protocols \
              $(OBJ_DIR)/cache \
              $(OBJ_DIR)/auth \
              $(OBJ_DIR)/filter \
              $(OBJ_DIR)/monitor \
              $(OBJ_DIR)/utils

# Default target
all: $(TARGET)

# Create directories
$(OBJ_SUBDIRS):
	@mkdir -p $@

# Link
$(TARGET): $(OBJS)
	@echo "Linking $@..."
	@$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@echo "Build complete: $@"

# Compile
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_SUBDIRS)
	@echo "Compiling $<..."
	@$(CC) $(CFLAGS) -c $< -o $@

# Clean
clean:
	@echo "Cleaning..."
	@rm -rf $(OBJ_DIR) $(TARGET)
	@echo "Clean complete"

# Install (optional)
install: $(TARGET)
	@echo "Installing to /usr/local/bin..."
	@sudo cp $(TARGET) /usr/local/bin/
	@echo "Installation complete"

# Uninstall
uninstall:
	@echo "Uninstalling..."
	@sudo rm -f /usr/local/bin/proxymax
	@echo "Uninstall complete"

# Run
run: $(TARGET)
	@./$(TARGET)

# Debug build
debug: CFLAGS += -g -DDEBUG
debug: clean $(TARGET)

# Help
help:
	@echo "ProxyMax - Multithreaded Proxy Server"
	@echo ""
	@echo "Targets:"
	@echo "  all      - Build the proxy server (default)"
	@echo "  clean    - Remove build artifacts"
	@echo "  debug    - Build with debug symbols"
	@echo "  install  - Install to /usr/local/bin"
	@echo "  uninstall- Remove from /usr/local/bin"
	@echo "  run      - Build and run the server"
	@echo "  help     - Show this help message"

.PHONY: all clean install uninstall run debug help
