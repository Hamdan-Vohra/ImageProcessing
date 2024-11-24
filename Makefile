# Compiler and flags
CC = gcc
CFLAGS = -fopenmp
LIBS = -lpng

# Directory paths
SRC_DIR = .
BUILD_DIR = build
OUT_DIR1 = BlurImages
OUT_DIR2 = NegationImages
OUT_DIR3 = BrightnessImages
OUT_DIR4 = GrayscaleImages


# Input and Output source_file names
SRC = $(SRC_DIR)/imageProcessing.c
EXEC = image_results

# All images processing target executable file
TARGET = $(EXEC)

# Create build and output directories if they don't exist
$(shell mkdir -p $(BUILD_DIR))

# Default target
all: $(TARGET)

# Compiling the C program into an executable
$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(BUILD_DIR)/$(EXEC) $(LIBS)

# Running the program (ensure DataSet exists)
run: $(TARGET)
	@echo "Running the program..."
	./$(BUILD_DIR)/$(EXEC)

# Clean up generated files
clean:
	rm -rf $(BUILD_DIR)
	rm -rf $(OUT_DIR1)
	rm -rf $(OUT_DIR2)
	rm -rf $(OUT_DIR3)
	rm -rf $(OUT_DIR4)

.PHONY: all run clean
