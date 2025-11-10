# Makefile for File Deduplication System
# Compatible with MinGW-w64/MSYS2 on Windows

CC = gcc
CFLAGS = -Wall -Wextra -O2 `pkg-config --cflags gtk+-3.0` -mwindows
LDFLAGS = `pkg-config --libs gtk+-3.0`
TARGET = file_dedup.exe

# Source files - IMPORTANT: Use exact case as your actual filenames
# Change Traversal.c to traversal.c if your file is lowercase
SOURCES = main.c gui.c Traversal.c filter.c report.c action.c
OBJECTS = $(SOURCES:.c=.o)

# Header files for dependency tracking
HEADERS = common.h gui.h Traversal.h filter.h report.h action.h

# Default target
all: $(TARGET)

# Link the executable
$(TARGET): $(OBJECTS)
	@echo "Linking $(TARGET)..."
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)
	@echo "Build successful! Run with: ./$(TARGET)"

# Compile source files to object files
%.o: %.c $(HEADERS)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -f $(OBJECTS) $(TARGET)
	@echo "Clean complete."

# Run the application
run: $(TARGET)
	@echo "Running $(TARGET)..."
	./$(TARGET)

# Install dependencies (for MSYS2)
install-deps:
	@echo "Installing dependencies with pacman..."
	pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-gtk3 mingw-w64-x86_64-pkg-config make
	@echo "Dependencies installed."

# Debug build with symbols
debug: CFLAGS += -g -DDEBUG
debug: clean $(TARGET)
	@echo "Debug build complete."

# Release build (optimized, no console window)
release: CFLAGS += -O3 -mwindows
release: clean $(TARGET)
	@echo "Release build complete."

# Help target
help:
	@echo "Available targets:"
	@echo "  all          - Build the application (default)"
	@echo "  clean        - Remove build artifacts"
	@echo "  run          - Build and run the application"
	@echo "  debug        - Build with debug symbols"
	@echo "  release      - Build optimized release version"
	@echo "  install-deps - Install required dependencies (MSYS2)"
	@echo "  help         - Show this help message"

.PHONY: all clean run install-deps debug release help