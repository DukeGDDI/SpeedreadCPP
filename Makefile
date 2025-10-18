# Project settings
TARGET     := speedread
SRC_DIR    := src
BUILD_DIR  := build
OBJ_DIR    := $(BUILD_DIR)/obj
BIN_DIR    := $(BUILD_DIR)/bin

# Compiler settings
CXX        := g++
CXXFLAGS   := -std=c++17 -O2 -Wall -Wextra
LDFLAGS    := 

# Source and object files
SRCS       := $(wildcard $(SRC_DIR)/*.cpp)
OBJS       := $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(SRCS))

# Default rule
all: $(BIN_DIR)/$(TARGET)

# Link step
$(BIN_DIR)/$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "✅ Built $@"

# Compile step
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo "Compiled $< -> $@"

# Run program
run: $(BIN_DIR)/$(TARGET)
	@$(BIN_DIR)/$(TARGET)

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)
	@echo "🧹 Cleaned build directory."

# Convenience alias
rebuild: clean all

.PHONY: all clean rebuild run
