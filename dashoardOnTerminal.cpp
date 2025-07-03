#include <iostream>
#include <iomanip>
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
#include <sstream>
#include <cstring>

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
#define CAN_ID_TRIP         0x10B  // Trip distance
#define CAN_ID_FUEL_RATE    0x10C  // Fuel consumption rate
#define CAN_ID_GEAR_POS     0x10D  // Current gear position

using namespace std;


class VehicleSimulator {
private:
    int can_socket;
    atomic<bool> running;
    atomic<bool> engine_on;
    atomic<double> speed_ms;       // Speed in m/s
    atomic<int> engine_rpm;
    atomic<int> fuel_level;
    atomic<int> engine_temp;
    atomic<bool> turn_left;
    atomic<bool> turn_right;
    atomic<bool> battery_ok;
    atomic<bool> backlight_on;
    atomic<char> transmission_mode; // P, R, N, D
    atomic<int> current_gear;      // 1-6 (1-5 forward, 6 reverse)
    atomic<bool> accelerator_pressed;
    atomic<bool> brake_pressed;
    atomic<double> odometer;
    atomic<double> trip_distance;
    atomic<double> fuel_accumulator;
    atomic<double> fuel_rate;
    
    struct termios original_termios;
    bool terminal_configured;
    
    // Physical constants
    const double DRAG_COEFFICIENT = 0.32;
    const double ROLLING_RESISTANCE = 0.015;
    const double VEHICLE_MASS = 1450.0;  // kg
    const double WHEEL_RADIUS = 0.3;     // meters
    const double FINAL_DRIVE_RATIO = 3.7;
    const double MAX_ENGINE_TORQUE = 250.0;  // Nm
    const double IDLE_FUEL_RATE = 0.8;        // L/h at idle
    const double MAX_FUEL_RATE = 25.0;        // L/h at full load
    const double TANK_CAPACITY = 20.0;        // Liters
    
    // Gear ratios
    const vector<double> GEAR_RATIOS = {3.5, 2.2, 1.6, 1.2, 0.9, 3.2}; // 1-5 forward, 6 reverse
    
    // State tracking
    double throttle_position;  // 0.0 to 1.0
    double brake_position;     // 0.0 to 1.0
    double engine_torque;
    double last_speed_ms;
    chrono::steady_clock::time_point last_update;

public:
    VehicleSimulator() : 
        can_socket(-1),
        running(true), 
        engine_on(false), 
        speed_ms(0.0), 
        engine_rpm(0),
        fuel_level(75), 
        engine_temp(20), 
        turn_left(false),
        turn_right(false), 
        battery_ok(true), 
        backlight_on(false),
        transmission_mode('P'),
        current_gear(0),
        accelerator_pressed(false),
        brake_pressed(false),
        odometer(0.0),
        trip_distance(0.0),
        fuel_accumulator(0.0),
        fuel_rate(0.0),
        terminal_configured(false),
        throttle_position(0.0),
        brake_position(0.0),
        engine_torque(0.0),
        last_speed_ms(0.0) {
        
        last_update = chrono::steady_clock::now();
        initializeCAN();
        setupTerminal();
    }
    
    ~VehicleSimulator() {
        cleanup();
    }
    
    void initializeCAN() {
        can_socket = socket(PF_CAN, SOCK_RAW, CAN_RAW);
        if (can_socket < 0) {
            cerr << "Error: Failed to create CAN socket: " << strerror(errno) << endl;
            cerr << "Run: sudo modprobe can && sudo modprobe vcan" << endl;
            exit(1);
        }
        
        struct ifreq interface_request;
        strcpy(interface_request.ifr_name, "vcan0");
        
        if (ioctl(can_socket, SIOCGIFINDEX, &interface_request) < 0) {
            cerr << "Error: CAN interface 'vcan0' not found: " << strerror(errno) << endl;
            cerr << "Setup virtual CAN interface with:" << endl;
            cerr << "sudo ip link add dev vcan0 type vcan" << endl;
            cerr << "sudo ip link set up vcan0" << endl;
            close(can_socket);
            exit(1);
        }
        
        struct sockaddr_can can_address;
        memset(&can_address, 0, sizeof(can_address));
        can_address.can_family = AF_CAN;
        can_address.can_ifindex = interface_request.ifr_ifindex;
        
        int enable_canfd = 0;
        if (setsockopt(can_socket, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable_canfd, sizeof(enable_canfd))) {
            cerr << "Warning: Failed to disable CAN FD support. Continuing..." << endl;
        }
        
        if (bind(can_socket, (struct sockaddr *)&can_address, sizeof(can_address)) < 0) {
            cerr << "Error: Failed to bind CAN socket: " << strerror(errno) << endl;
            close(can_socket);
            exit(1);
        }
        
        cout << "✅ CAN socket successfully initialized on vcan0" << endl;
    }
    
    void setupTerminal() {
        if (tcgetattr(STDIN_FILENO, &original_termios) != 0) {
            cerr << "Error: Failed to get terminal attributes." << endl;
            return;
        }
        
        struct termios raw_termios = original_termios;
        raw_termios.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHOK | ECHONL | ECHOPRT | ECHOKE | ICRNL);
        raw_termios.c_cc[VMIN] = 0;
        raw_termios.c_cc[VTIME] = 0;
        
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw_termios) != 0) {
            cerr << "Error: Failed to set terminal to raw mode." << endl;
            return;
        }
        
        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        if (flags != -1) {
            fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
        }
        
        terminal_configured = true;
        cout << "✅ Terminal configured for raw input" << endl;
    }
    
    void restoreTerminal() {
        if (terminal_configured) {
            tcsetattr(STDIN_FILENO, TCSANOW, &original_termios);
            int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
            if (flags != -1) {
                fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);
            }
            terminal_configured = false;
        }
    }
    
    bool keyboardHit() {
        fd_set read_descriptors;
        FD_ZERO(&read_descriptors);
        FD_SET(STDIN_FILENO, &read_descriptors);
        
        struct timeval timeout = {0, 0};
        return select(STDIN_FILENO + 1, &read_descriptors, nullptr, nullptr, &timeout) > 0;
    }
    
    char getCharacter() {
        char character = 0;
        return (read(STDIN_FILENO, &character, 1) == 1) ? character : 0;
    }
    
    void transmitCANMessage(uint32_t message_id, const uint8_t* data, uint8_t data_length) {
        if (can_socket < 0) return;
        
        struct can_frame frame;
        frame.can_id = message_id;
        frame.can_dlc = min(data_length, static_cast<uint8_t>(8));
        memcpy(frame.data, data, frame.can_dlc);
        
        if (write(can_socket, &frame, sizeof(frame)) != sizeof(frame)) {
            static int error_count = 0;
            if (error_count++ % 10 == 0) {
                cerr << "CAN TX Error: ID 0x" << hex << message_id 
                          << " - " << strerror(errno) << dec << endl;
            }
        }
    }
    
    double calculateRPM() {
        if (!engine_on) return 0.0;
        
        const double idle_rpm = 800.0;
        
        // In Park or Neutral, engine idles
        if (transmission_mode == 'P' || transmission_mode == 'N') 
            return idle_rpm;
        
        double gear_ratio = 0.0;
        
        // Reverse gear
        if (transmission_mode == 'R') {
            gear_ratio = GEAR_RATIOS[5];
        }
        // Drive mode
        else if (transmission_mode == 'D') {
            // Ensure current_gear is within valid range
            if (current_gear < 0 || current_gear > 4) {
                current_gear = 0; // Default to first gear
            }
            gear_ratio = GEAR_RATIOS[current_gear];
        }
        
        if (gear_ratio == 0.0) return idle_rpm;
        
        // Calculate RPM based on wheel speed and gear ratio
        double wheel_rotation = speed_ms / (2 * M_PI * WHEEL_RADIUS); // rotations per second
        double calculated_rpm = wheel_rotation * gear_ratio * FINAL_DRIVE_RATIO * 60.0;
        
        return max(idle_rpm, calculated_rpm);
    }
    
    void handleKeyboardInput() {
        // Reset input states each frame
        accelerator_pressed = false;
        brake_pressed = false;
        
        while (keyboardHit()) {
            char key = getCharacter();
            if (!key) continue;
            
            if (key >= 'a' && key <= 'z') {
                key = key - 'a' + 'A';
            }
            
            switch(key) {
                case 'A':  // Accelerator pressed
                    accelerator_pressed = true;
                    break;
                    
                case 'B':  // Brake pressed
                    brake_pressed = true;
                    break;
                    
                case 'S':  // Start/Stop engine
                    if (engine_on || (battery_ok && fuel_level > 5 && 
                        (transmission_mode == 'P' || transmission_mode == 'N'))) {
                        engine_on = !engine_on;
                        if (engine_on) {
                            engine_rpm = 800;
                            cout << "🔥 Engine Started" << endl;
                        } else {
                            speed_ms = 0.0;
                            engine_rpm = 0;
                            cout << "🔴 Engine Stopped" << endl;
                        }
                    }
                    break;
                    
                case 'D':  // Drive mode
                    if (speed_ms < 0.5 && transmission_mode != 'D') {
                        transmission_mode = 'D';
                        current_gear = 0; // Start in first gear
                        cout << "🚘 Shifted to Drive" << endl;
                    }
                    break;
                    
                case 'R':  // Reverse mode
                    if (speed_ms < 0.5 && transmission_mode != 'R') {
                        transmission_mode = 'R';
                        current_gear = 5; // Set to reverse gear
                        cout << "↩️ Shifted to Reverse" << endl;
                    }
                    break;
                    
                case 'N':  // Neutral mode
                    transmission_mode = 'N';
                    cout << "⚙️ Shifted to Neutral" << endl;
                    break;
                    
                case 'P':  // Park mode
                    if (speed_ms < 0.5) {
                        transmission_mode = 'P';
                        speed_ms = 0.0; // Park locks the transmission
                        cout << "🅿️ Shifted to Park" << endl;
                    }
                    break;
                    
                case 'Q':  // Quit
                    running = false;
                    cout << "👋 Shutting down simulator..." << endl;
                    break;
                    
                case 27:  // ESC key
                    handleArrowKeys();
                    break;
                    
                case 32:  // Space bar - handbrake
                    if (speed_ms < 8.0) {
                        speed_ms = 0.0;
                        cout << "🛑 Handbrake engaged" << endl;
                    }
                    break;
                    
                case 'L':  // Toggle lights
                    backlight_on = !backlight_on;
                    cout << "💡 Lights: " << (backlight_on ? "ON" : "OFF") << endl;
                    break;
                    
                case 'T':  // Reset trip
                    trip_distance = 0.0;
                    fuel_accumulator = 0.0;
                    cout << "🔄 Trip reset" << endl;
                    break;
            }
        }
        
        // Update throttle and brake positions
        if (accelerator_pressed) {
            throttle_position = 1.0;
            brake_position = 0.0;
        } else if (brake_pressed) {
            brake_position = 1.0;
            throttle_position = 0.0;
        } else {
            // Gradually reduce throttle and brake when released
            if (throttle_position > 0.0) {
                throttle_position = max(0.0, throttle_position - 0.1);
            }
            if (brake_position > 0.0) {
                brake_position = max(0.0, brake_position - 0.2);
            }
        }
    }
    
    void handleArrowKeys() {
        if (!keyboardHit()) return;
        
        char bracket = getCharacter();
        if (bracket == '[' && keyboardHit()) {
            char direction = getCharacter();
            switch(direction) {
                case 'C':  // Right arrow
                    turn_right = !turn_right;
                    turn_left = false;
                    break;
                case 'D':  // Left arrow
                    turn_left = !turn_left;
                    turn_right = false;
                    break;
                case 'A':  // Up arrow - hazard lights
                    turn_left = !turn_left;
                    turn_right = turn_left.load();
                    break;
            }
        }
    }
    
    void updateAutomaticGear() {
        if (transmission_mode != 'D') return;
        
        int new_gear = current_gear;
        
        // Gear shifting logic based on speed and RPM
        double speed_kmh = speed_ms * 3.6;
        
        if (engine_rpm > 3000 && current_gear < 4) {
            // Upshift conditions
            if ((current_gear == 0 && speed_kmh > 15) ||
                (current_gear == 1 && speed_kmh > 30) ||
                (current_gear == 2 && speed_kmh > 45) ||
                (current_gear == 3 && speed_kmh > 65)) {
                new_gear = current_gear + 1;
            }
        } 
        else if (engine_rpm < 1500 && current_gear > 0) {
            // Downshift conditions
            if ((current_gear == 1 && speed_kmh < 10) ||
                (current_gear == 2 && speed_kmh < 25) ||
                (current_gear == 3 && speed_kmh < 40) ||
                (current_gear == 4 && speed_kmh < 55)) {
                new_gear = current_gear - 1;
            }
        }
        
        // Only show message if gear actually changed
        if (new_gear != current_gear) {
            current_gear = new_gear;
            cout << "⚙️  Shifted to Gear " << (current_gear + 1) << endl;
        }
    }
    
    void updateVehicleSimulation() {
        // Calculate time delta
        auto now = chrono::steady_clock::now();
        double dt = chrono::duration<double>(now - last_update).count();
        last_update = now;
        
        // Update RPM based on current speed and gear
        engine_rpm = static_cast<int>(calculateRPM());
        
        // Update automatic transmission gear
        if (engine_on && transmission_mode == 'D') {
            updateAutomaticGear();
        }
        
        // Calculate engine torque
        double max_torque = MAX_ENGINE_TORQUE * (1.0 - (abs(engine_rpm - 3000.0) / 4000.0));
        engine_torque = throttle_position * max(50.0, max_torque);
        
        // Calculate forces
        double drag_force = 0.5 * 1.225 * DRAG_COEFFICIENT * 2.2 * speed_ms * speed_ms;
        double rolling_force = ROLLING_RESISTANCE * VEHICLE_MASS * 9.81;
        double brake_force = brake_position * 2000.0;
        
        // Engine force (only when in gear)
        double engine_force = 0.0;
        if (engine_on && (transmission_mode == 'D' || transmission_mode == 'R')) {
            double gear_ratio = 0.0;
            
            if (transmission_mode == 'D') {
                gear_ratio = GEAR_RATIOS[current_gear];
            } 
            else if (transmission_mode == 'R') {
                gear_ratio = GEAR_RATIOS[5]; // Reverse gear
            }
            
            if (gear_ratio > 0.0) {
                engine_force = (engine_torque * gear_ratio * FINAL_DRIVE_RATIO) / WHEEL_RADIUS;
            }
        }
        
        // Calculate acceleration
        double total_force = engine_force - drag_force - rolling_force - brake_force;
        double acceleration = total_force / VEHICLE_MASS;
        
        // Apply acceleration to speed
        double new_speed = speed_ms + acceleration * dt;
        
        // Prevent movement in Park
        if (transmission_mode == 'P') {
            new_speed = 0.0;
        }
        // Prevent rolling backwards when in gear
        else if (transmission_mode != 'N' && new_speed < 0.1) {
            new_speed = 0.0;
        }
        
        speed_ms = clamp(new_speed, 0.0, 60.0); // Max 216 km/h
        
        // Update distances
        double distance_m = speed_ms * dt;
        odometer =odometer + distance_m / 1000.0;
        trip_distance =  trip_distance +distance_m / 1000.0;
        
        // Calculate fuel consumption
        double fuel_flow = 0.0;
        if (engine_on) {
            fuel_flow = IDLE_FUEL_RATE + (MAX_FUEL_RATE - IDLE_FUEL_RATE) * 
                       (throttle_position * 0.7 + (engine_rpm / 6000.0) * 0.3);
            
            double fuel_used = fuel_flow * (dt / 3600.0);
            fuel_accumulator = fuel_accumulator +fuel_used;
            
            // Update fuel level based on consumption
            if (fuel_accumulator >= (TANK_CAPACITY / 100.0)) {
                fuel_level = max(0, fuel_level - 1);
                fuel_accumulator = fuel_accumulator -(TANK_CAPACITY / 100.0);
            }
        }
        
        // Calculate fuel rate (L/100km)
        if (speed_ms > 1.0) {
            double hours_per_100km = 100.0 / (speed_ms * 3.6);
            fuel_rate = fuel_flow * hours_per_100km;
        } else {
            fuel_rate = 0.0;
        }
        
        // Simulate engine temperature
        if (engine_on) {
            double temp_increase = (engine_rpm / 5000.0) * 0.5 + (throttle_position * 0.5);
            double cooling = (speed_ms / 20.0) * 0.8;
            engine_temp = clamp(
                engine_temp + static_cast<int>(temp_increase - cooling),
                20, 120
            );
        } else {
            if (engine_temp > 20) engine_temp -= 1;
        }
        
        // Auto-cancel turn signals after distance
        static double turn_distance = 0.0;
        turn_distance += distance_m;
        if (turn_distance > 200.0) {
            turn_left = false;
            turn_right = false;
            turn_distance = 0.0;
        }
        
        // Battery drain/recharge simulation
        static double battery_time = 0.0;
        if (engine_on) {
            battery_ok = true;
            battery_time = 0.0;
        } else {
            battery_time += dt;
            if (battery_time > 300.0) {
                battery_ok = false;
            }
        }
        
        // Engine stall simulation
        if (engine_on && engine_rpm < 500 && transmission_mode != 'N') {
            engine_on = false;
            speed_ms = 0.0;
            engine_rpm = 0;
            cout << "💥 Engine stalled!" << endl;
        }
    }
    
    void transmitVehicleData() {
        uint8_t data[8] = {0};
        
        // Speed in km/h (2 bytes)
        int speed_kmh = static_cast<int>(speed_ms * 3.6);
        data[0] = speed_kmh & 0xFF;
        data[1] = (speed_kmh >> 8) & 0xFF;
        transmitCANMessage(CAN_ID_SPEED, data, 2);
        
        // RPM (2 bytes)
        data[0] = engine_rpm & 0xFF;
        data[1] = (engine_rpm >> 8) & 0xFF;
        transmitCANMessage(CAN_ID_RPM, data, 2);
        
        // Fuel level (1 byte)
        data[0] = static_cast<uint8_t>(fuel_level);
        transmitCANMessage(CAN_ID_FUEL_LEVEL, data, 1);
        
        // Engine temperature (1 byte)
        data[0] = static_cast<uint8_t>(engine_temp);
        transmitCANMessage(CAN_ID_ENGINE_TEMP, data, 1);
        
        // Turn signals
        data[0] = turn_left ? 1 : 0;
        transmitCANMessage(CAN_ID_TURN_LEFT, data, 1);
        data[0] = turn_right ? 1 : 0;
        transmitCANMessage(CAN_ID_TURN_RIGHT, data, 1);
        
        // Battery status
        data[0] = battery_ok ? 1 : 0;
        transmitCANMessage(CAN_ID_BATTERY, data, 1);
        
        // Backlight status
        data[0] = backlight_on ? 1 : 0;
        transmitCANMessage(CAN_ID_BACKLIGHT, data, 1);
        
        // Transmission mode (P, R, N, D)
        data[0] = static_cast<uint8_t>(transmission_mode);
        transmitCANMessage(CAN_ID_GEAR, data, 1);
        
        // Engine status
        data[0] = engine_on ? 1 : 0;
        transmitCANMessage(CAN_ID_ENGINE_START, data, 1);
        
        // Odometer (4 bytes, in km * 10 for 0.1km precision)
        uint32_t odo_10km = static_cast<uint32_t>(odometer * 10);
        data[0] = odo_10km & 0xFF;
        data[1] = (odo_10km >> 8) & 0xFF;
        data[2] = (odo_10km >> 16) & 0xFF;
        data[3] = (odo_10km >> 24) & 0xFF;
        transmitCANMessage(CAN_ID_ODOMETER, data, 4);
        
        // Trip distance (2 bytes, in 0.1km units)
        uint16_t trip_10km = static_cast<uint16_t>(trip_distance * 10);
        data[0] = trip_10km & 0xFF;
        data[1] = (trip_10km >> 8) & 0xFF;
        transmitCANMessage(CAN_ID_TRIP, data, 2);
        
        // Fuel rate (2 bytes, in 0.1L/100km units)
        uint16_t fuel_rate_10 = static_cast<uint16_t>(fuel_rate * 10);
        data[0] = fuel_rate_10 & 0xFF;
        data[1] = (fuel_rate_10 >> 8) & 0xFF;
        transmitCANMessage(CAN_ID_FUEL_RATE, data, 2);
        
        // Current gear position (1 byte)
        data[0] = static_cast<uint8_t>(current_gear + 1); // 1-6
        transmitCANMessage(CAN_ID_GEAR_POS, data, 1);
    }
    
    void displayVehicleStatus() {
        cout << "\033[2J\033[H";  // Clear screen
        
        int speed_kmh = static_cast<int>(speed_ms * 3.6);
        string gear_display;
        
        // Create gear display string based on transmission mode
        switch (transmission_mode) {
            case 'P': gear_display = "PARK"; break;
            case 'R': gear_display = "REVERSE"; break;
            case 'N': gear_display = "NEUTRAL"; break;
            case 'D': 
                gear_display = "DRIVE ";
                gear_display += to_string(current_gear + 1);
                break;
            default: gear_display = "UNKNOWN";
        }
        
        ostringstream oss;
        oss << "╔══════════════════════════════════════════╗" << endl
            << "║        🚗 AUTOMATIC VEHICLE SIMULATOR 🚗  ║" << endl
            << "╠══════════════════════════════════════════╣" << endl
            << "║ Engine:      " << (engine_on ? "🟢 ON " : "🔴 OFF") << "                     ║" << endl
            << "║ Speed:       " << setw(3) << speed_kmh << " km/h                ║" << endl
            << "║ RPM:         " << setw(4) << engine_rpm.load() << " rpm               ║" << endl
            << "║ Gear:        " << setw(10) << left << gear_display << "         ║" << endl
            << "║ Fuel:        " << setw(3) << fuel_level.load() << "%                   ║" << endl
            << "║ Fuel Rate:   " << fixed << setprecision(1) << fuel_rate.load() << " L/100km        ║" << endl
            << "║ Engine Temp: " << setw(3) << engine_temp.load() << "°C                 ║" << endl
            << "║ Odometer:    " << fixed << setprecision(1) << odometer.load() << " km         ║" << endl
            << "║ Trip:        " << fixed << setprecision(1) << trip_distance.load() << " km         ║" << endl
            << "║ Turn Left:   " << (turn_left ? "🟡 ON " : "⚫ OFF") << "                 ║" << endl
            << "║ Turn Right:  " << (turn_right ? "🟡 ON " : "⚫ OFF") << "                 ║" << endl
            << "║ Battery:     " << (battery_ok ? "🟢 OK " : "🔴 LOW") << "                 ║" << endl
            << "║ Backlight:   " << (backlight_on ? "💡 ON " : "⚫ OFF") << "                 ║" << endl
            << "╠══════════════════════════════════════════╣" << endl
            << "║               CONTROLS                   ║" << endl
            << "║ A - Accelerate    B - Brake              ║" << endl
            << "║ S - Start/Stop    D - Drive              ║" << endl
            << "║ R - Reverse       N - Neutral            ║" << endl
            << "║ P - Park          T - Reset Trip         ║" << endl
            << "║ ←→ - Turn Signals Space - Handbrake      ║" << endl
            << "║ L - Lights        Q - Quit               ║" << endl
            << "╚══════════════════════════════════════════╝" << endl;
        
        cout << oss.str();
        
        // Display throttle/brake status
        cout << "\nThrottle: [" << string(static_cast<int>(throttle_position * 20), '=')
                  << string(20 - static_cast<int>(throttle_position * 20), ' ') << "] "
                  << static_cast<int>(throttle_position * 100) << "%";
        
        cout << "   Brake: [" << string(static_cast<int>(brake_position * 20), '=')
                  << string(20 - static_cast<int>(brake_position * 20), ' ') << "] "
                  << static_cast<int>(brake_position * 100) << "%\n";
        
        // System status
        cout << "CAN: " << (can_socket >= 0 ? "🟢 ACTIVE" : "🔴 DISABLED")
                  << " | vcan0 | Physics: " << (speed_ms > 0.1 ? "ACTIVE" : "IDLE") 
                  << " | Transmission: " << transmission_mode << endl;
    }
    
    void cleanup() {
        restoreTerminal();
        if (can_socket >= 0) {
            close(can_socket);
            can_socket = -1;
        }
    }
    
    void runSimulation() {
        cout << "🚀 Starting Automatic Vehicle Simulator..." << endl;
        cout << "📡 CAN messages will be sent on vcan0" << endl;
        cout << "⌨️  Use keyboard controls to operate the vehicle" << endl;
        
        for (int i = 3; i > 0; i--) {
            cout << "🔄 Starting in " << i << "..." << endl;
            this_thread::sleep_for(chrono::seconds(1));
        }
        
        while (running) {
            handleKeyboardInput();
            updateVehicleSimulation();
            transmitVehicleData();
            displayVehicleStatus();
            
            this_thread::sleep_for(chrono::milliseconds(50));
        }
        
        cout << "\n🛑 Simulator stopped. Total distance: " 
                  << fixed << setprecision(1) << odometer.load() 
                  << " km" << endl;
    }
};

int main() {
    cout << "🚗 AUTOMATIC Vehicle Physics Simulator for Linux" << endl;
    cout << "================================================" << endl;
    
    try {
        VehicleSimulator simulator;
        simulator.runSimulation();
    } 
    catch (const exception& error) {
        cerr << "❌ Fatal Error: " << error.what() << endl;
        return 1;
    }
    
    return 0;
}