# Compiler and flags
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O3

# Source files and target
SOURCES = main.cpp
HEADERS = common.h cache.h tlb.h pwc.h physical_memory.h page_table.h data_cache.h
TARGET = memory_simulator

# Default target
all: $(TARGET)

# Build the target
$(TARGET): $(SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET)

# Clean built files
clean:
	rm -f $(TARGET)

# Run with default parameters (smaller amounts for testing)
run: $(TARGET)
	./$(TARGET) trace.bin 32 4 256 8 8 4 1

# Run with a specific trace file
run_trace: $(TARGET)
	./$(TARGET) $(TRACE_FILE)

# Run with specific TLB configurations
run_tlb_test: $(TARGET)
	./$(TARGET) $(TRACE_FILE) $(L1_TLB_SIZE) $(L1_TLB_WAYS) $(L2_TLB_SIZE) $(L2_TLB_WAYS)

debug: $(TARGET)
	$(CXX) $(CXXFLAGS) -g $(SOURCES) -o $(TARGET)_debug

# Phony targets
.PHONY: all clean run run_trace run_tlb_test