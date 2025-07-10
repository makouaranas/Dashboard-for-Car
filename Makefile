# Makefile for Vehicle Simulator

# Compiler and flags
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -pthread
LDFLAGS = -pthread

# Target executable
TARGET = vehicle_simulator

# Source files
SOURCES = main.cpp VehicleSimulator.cpp

# Object files
OBJECTS = $(SOURCES:.cpp=.o)

# Header files
HEADERS = VehicleSimulator.h

# Default target
all: $(TARGET)

# Link the executable
$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $(TARGET) $(LDFLAGS)
    

# Compile source files
%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean build files
clean:
	rm -f $(OBJECTS) $(TARGET)


# Create virtual CAN interface (requires root)
setup-vcan:
	sudo modprobe vcan
	sudo ip link add dev vcan0 type vcan
	sudo ip link set up vcan0

# Remove virtual CAN interface
cleanup-vcan:
	sudo ip link set down vcan0
	sudo ip link delete vcan0

# Run the simulator (requires vcan0 to be set up)
run: $(TARGET)
	./$(TARGET)


