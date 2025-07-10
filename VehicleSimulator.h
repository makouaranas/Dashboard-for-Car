#ifndef VEHICLE_SIMULATOR_H
#define VEHICLE_SIMULATOR_H

#include <iostream>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>
#include <termios.h>
#include <fcntl.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <cmath>
#include <sys/select.h>
#include <cstdlib>
#include <cerrno>
#include <algorithm>
#include <vector>
#include <cstring>

using namespace std;

// CAN ID definitions
#define CAN_ID_SPEED        0x100
#define CAN_ID_RPM          0x101
#define CAN_ID_FUEL_LEVEL   0x102
#define CAN_ID_ENGINE_TEMP  0x103
#define CAN_ID_TURN_LEFT    0x104
#define CAN_ID_TURN_RIGHT   0x105
#define CAN_ID_BATTERY      0x106
#define CAN_ID_BACKLIGHT    0x107
#define CAN_ID_GEAR         0x108
#define CAN_ID_ENGINE_START 0x109
#define CAN_ID_ODOMETER     0x10A
#define CAN_ID_TRIP         0x10B
#define CAN_ID_FUEL_RATE    0x10C
#define CAN_ID_GEAR_POS     0x10D

class VehicleSimulator {
private:
    int can_socket;
    atomic<bool> running;
    atomic<bool> engine_on;
    atomic<double> speed_ms;
    atomic<int> engine_rpm;
    atomic<int> fuel_level;
    atomic<int> engine_temp;
    atomic<bool> turn_left;
    atomic<bool> turn_right;
    atomic<bool> battery_ok;
    atomic<bool> backlight_on;
    atomic<char> transmission_mode;
    atomic<int> current_gear;
    atomic<bool> accelerator_pressed;
    atomic<bool> brake_pressed;
    atomic<double> odometer;
    atomic<double> trip_distance;
    atomic<double> fuel_accumulator;
    atomic<double> fuel_rate;
    
    struct termios original_termios;
    bool terminal_configured;
    
    // Physical constants
    const double DRAG_COEFFICIENT = 0.39;
    const double ROLLING_RESISTANCE = 0.02;
    const double VEHICLE_MASS = 1950.0;
    const double WHEEL_RADIUS = 0.3;
    const double FINAL_DRIVE_RATIO = 3.7;
    const double MAX_ENGINE_TORQUE = 250.0;
    const double IDLE_FUEL_RATE = 0.8;
    const double MAX_FUEL_RATE = 25.0;
    const double TANK_CAPACITY = 50.0;
    
    // Gear ratios
    const vector<double> GEAR_RATIOS = {3.5, 2.2, 1.6, 1.2, 0.9, 3.2};
    
    // State tracking
    double throttle_position;
    double brake_position;
    double engine_torque;
    double last_speed_ms;
    chrono::steady_clock::time_point last_update;
    
    // Private methods
    void initializeCAN();
    void setupTerminal();
    void restoreTerminal();
    bool keyboardHit();
    char getCharacter();
    void transmitCANMessage(uint32_t message_id, const uint8_t* data, uint8_t data_length);
    double calculateRPM();
    void handleKeyboardInput();
    void handleArrowKeys();
    void updateAutomaticGear();
    void updateVehicleSimulation();
    void transmitVehicleData();
    void cleanup();

public:
    VehicleSimulator();
    ~VehicleSimulator();
    void runSimulation();
};

#endif // VEHICLE_SIMULATOR_H