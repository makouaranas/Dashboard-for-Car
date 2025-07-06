#include "VehicleSimulator.h"

VehicleSimulator::VehicleSimulator() : // Constructor initializes the simulator
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

VehicleSimulator::~VehicleSimulator() {
    cleanup();
}

void VehicleSimulator::initializeCAN() {// Initialize the CAN socket for communication
    can_socket = socket(PF_CAN, SOCK_RAW, CAN_RAW);// Create a raw CAN socket, SOCK_RAW allows direct access to the CAN protocol,CAN_RAW specifies the raw CAN protocol.
    if (can_socket < 0) {
        cerr << "Error creating CAN socket: " << strerror(errno) << endl;
        exit(1);
    }
    
    struct ifreq interface_request;// Request the interface index for the CAN interface
    strcpy(interface_request.ifr_name, "vcan0");// "vcan0" is a virtual CAN interface used for testing.
    
    if (ioctl(can_socket, SIOCGIFINDEX, &interface_request) < 0) {
        cerr << "CAN interface 'vcan0' not found: " << strerror(errno) << endl;
        close(can_socket);
        exit(1);
    }
    
    struct sockaddr_can can_address;
    memset(&can_address, 0, sizeof(can_address));
    can_address.can_family = AF_CAN;
    can_address.can_ifindex = interface_request.ifr_ifindex;
    
    if (bind(can_socket, (struct sockaddr *)&can_address, sizeof(can_address)) < 0) {
        cerr << "Error binding CAN socket: " << strerror(errno) << endl;
        close(can_socket);
        exit(1);
    }
}

void VehicleSimulator::setupTerminal() {
    if (tcgetattr(STDIN_FILENO, &original_termios) != 0) {
        return;
    }
    
    struct termios raw_termios = original_termios;
    raw_termios.c_lflag &= ~(ICANON | ECHO);
    raw_termios.c_cc[VMIN] = 0;
    raw_termios.c_cc[VTIME] = 0;
    
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw_termios) != 0) {
        return;
    }
    
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags != -1) {
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    }
    
    terminal_configured = true;
}

void VehicleSimulator::restoreTerminal() {
    if (terminal_configured) {
        tcsetattr(STDIN_FILENO, TCSANOW, &original_termios);
        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        if (flags != -1) {
            fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);
        }
        terminal_configured = false;
    }
}

bool VehicleSimulator::keyboardHit() {
    fd_set read_descriptors;
    FD_ZERO(&read_descriptors);
    FD_SET(STDIN_FILENO, &read_descriptors);
    
    struct timeval timeout = {0, 0};
    return select(STDIN_FILENO + 1, &read_descriptors, nullptr, nullptr, &timeout) > 0;
}

char VehicleSimulator::getCharacter() {
    char character = 0;
    return (read(STDIN_FILENO, &character, 1) == 1) ? character : 0;
}

void VehicleSimulator::transmitCANMessage(uint32_t message_id, const uint8_t* data, uint8_t data_length) {
    if (can_socket < 0) return;
    
    struct can_frame frame;
    frame.can_id = message_id;
    frame.can_dlc = min(data_length, static_cast<uint8_t>(8));
    memcpy(frame.data, data, frame.can_dlc);
    
    write(can_socket, &frame, sizeof(frame));
}

double VehicleSimulator::calculateRPM() {
    if (!engine_on) return 0.0;
    
    const double idle_rpm = 800.0;
    if (transmission_mode == 'P' || transmission_mode == 'N') 
        return idle_rpm;
    
    double gear_ratio = 0.0;
    
    if (transmission_mode == 'R') {
        gear_ratio = GEAR_RATIOS[5];
    }
    else if (transmission_mode == 'D') {
        if (current_gear >= 0 && current_gear <= 4) {
            gear_ratio = GEAR_RATIOS[current_gear];
        }
    }
    
    if (gear_ratio == 0.0) return idle_rpm;
    
    double wheel_rotation = speed_ms / (2 * M_PI * WHEEL_RADIUS);
    return max(idle_rpm, wheel_rotation * gear_ratio * FINAL_DRIVE_RATIO * 60.0);
}

void VehicleSimulator::handleKeyboardInput() {
    accelerator_pressed = false;
    brake_pressed = false;
    
    while (keyboardHit()) {
        char key = getCharacter();
        if (!key) continue;
        
        if (key >= 'a' && key <= 'z') {
            key = key - 'a' + 'A';
        }
        
        switch(key) {
            case 'A':  // Accelerator
                accelerator_pressed = true;
                break;
                
            case 'B':  // Brake
                brake_pressed = true;
                break;
                
            case 'S':  // Start/Stop engine
                if (engine_on || (battery_ok && fuel_level > 5 && 
                    (transmission_mode == 'P' || transmission_mode == 'N'))) {
                    engine_on = !engine_on;
                }
                break;
                
            case 'D':  // Drive mode
                if (speed_ms < 0.5 && transmission_mode != 'D') {
                    transmission_mode = 'D';
                    current_gear = 0;
                }
                break;
                
            case 'R':  // Reverse mode
                if (speed_ms < 0.5 && transmission_mode != 'R') {
                    transmission_mode = 'R';
                    current_gear = 5;
                }
                break;
                
            case 'N':  // Neutral mode
                transmission_mode = 'N';
                break;
                
            case 'P':  // Park mode
                if (speed_ms < 0.5) {
                    transmission_mode = 'P';
                    speed_ms = 0.0;
                }
                break;
                
            case 'Q':  // Quit
                running = false;
                break;
                
            case 27:  // ESC key
                handleArrowKeys();
                break;
                
            case 32:  // Handbrake
                if (speed_ms < 8.0) {
                    speed_ms = 0.0;
                }
                break;
                
            case 'L':  // Toggle lights
                backlight_on = !backlight_on;
                break;
                
            case 'T':  // Reset trip
                trip_distance = 0.0;
                fuel_accumulator = 0.0;
                break;
        }
    }
    
    // Update throttle and brake
    if (accelerator_pressed) {
        throttle_position = 1.0;
        brake_position = 0.0;
    } else if (brake_pressed) {
        brake_position = 1.0;
        throttle_position = 0.0;
    } else {
        if (throttle_position > 0.0) {
            throttle_position = max(0.0, throttle_position - 0.1);
        }
        if (brake_position > 0.0) {
            brake_position = max(0.0, brake_position - 0.2);
        }
    }
}

void VehicleSimulator::handleArrowKeys() {
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
            case 'A':  // Hazard lights
                turn_left = !turn_left;
                turn_right = turn_left.load();
                break;
        }
    }
}

void VehicleSimulator::updateAutomaticGear() {
    if (transmission_mode != 'D') return;
    
    int new_gear = current_gear;
    double speed_kmh = speed_ms * 3.6;
    
    if (engine_rpm > 3000 && current_gear < 4) {
        if ((current_gear == 0 && speed_kmh > 15) ||
            (current_gear == 1 && speed_kmh > 30) ||
            (current_gear == 2 && speed_kmh > 45) ||
            (current_gear == 3 && speed_kmh > 65)) {
            new_gear = current_gear + 1;
        }
    } 
    else if (engine_rpm < 1500 && current_gear > 0) {
        if ((current_gear == 1 && speed_kmh < 10) ||
            (current_gear == 2 && speed_kmh < 25) ||
            (current_gear == 3 && speed_kmh < 40) ||
            (current_gear == 4 && speed_kmh < 55)) {
            new_gear = current_gear - 1;
        }
    }
    
    if (new_gear != current_gear) {
        current_gear = new_gear;
    }
}

void VehicleSimulator::updateVehicleSimulation() {
    auto now = chrono::steady_clock::now();
    double dt = chrono::duration<double>(now - last_update).count();
    last_update = now;
    
    engine_rpm = static_cast<int>(calculateRPM());
    
    if (engine_on && transmission_mode == 'D') {
        updateAutomaticGear();
    }
    
    double max_torque = MAX_ENGINE_TORQUE * (1.0 - (abs(engine_rpm - 3000.0) / 4000.0));
    engine_torque = throttle_position * max(50.0, max_torque);
    
    double drag_force = 0.5 * 1.225 * DRAG_COEFFICIENT * 2.2 * speed_ms * speed_ms;
    double rolling_force = ROLLING_RESISTANCE * VEHICLE_MASS * 9.81;
    double brake_force = brake_position * 2000.0;
    
    double engine_force = 0.0;
    if (engine_on && (transmission_mode == 'D' || transmission_mode == 'R')) {
        double gear_ratio = 0.0;
        
        if (transmission_mode == 'D') {
            gear_ratio = GEAR_RATIOS[current_gear];
        } 
        else if (transmission_mode == 'R') {
            gear_ratio = GEAR_RATIOS[5];
        }
        
        if (gear_ratio > 0.0) {
            engine_force = (engine_torque * gear_ratio * FINAL_DRIVE_RATIO) / WHEEL_RADIUS;
        }
    }
    
    double total_force = engine_force - drag_force - rolling_force - brake_force;
    double acceleration = total_force / VEHICLE_MASS;
    double new_speed = speed_ms + acceleration * dt;
    
    if (transmission_mode == 'P') {
        new_speed = 0.0;
    }
    else if (transmission_mode != 'N' && new_speed < 0.1) {
        new_speed = 0.0;
    }
    
    speed_ms = clamp(new_speed, 0.0, 60.0);
    
    double distance_m = speed_ms * dt;
    odometer = odometer +distance_m / 1000.0;
    trip_distance = trip_distance +distance_m / 1000.0;
    
    double fuel_flow = 0.0;
    if (engine_on) {
        fuel_flow = IDLE_FUEL_RATE + (MAX_FUEL_RATE - IDLE_FUEL_RATE) * 
                   (throttle_position * 0.7 + (engine_rpm / 6000.0) * 0.3);
        
        double fuel_used = fuel_flow * (dt / 3600.0);
        fuel_accumulator = fuel_accumulator +fuel_used;
        
        if (fuel_accumulator >= (TANK_CAPACITY / 100.0)) {
            fuel_level = max(0, fuel_level - 1);
            fuel_accumulator =  fuel_accumulator -(TANK_CAPACITY / 100.0);
        }
    }
    
    if (speed_ms > 1.0) {
        double hours_per_100km = 100.0 / (speed_ms * 3.6);
        fuel_rate = fuel_flow * hours_per_100km;
    } else {
        fuel_rate = 0.0;
    }
    
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
    
    static double turn_distance = 0.0;
    turn_distance += distance_m;
    if (turn_distance > 200.0) {
        turn_left = false;
        turn_right = false;
        turn_distance = 0.0;
    }
    
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
    
    if (engine_on && engine_rpm < 500 && transmission_mode != 'N') {
        engine_on = false;
        speed_ms = 0.0;
        engine_rpm = 0;
    }
}

void VehicleSimulator::transmitVehicleData() {
    uint8_t data[8] = {0};
    
    // Speed (2 bytes, km/h)
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
    
    // Transmission mode
    data[0] = static_cast<uint8_t>(transmission_mode);
    transmitCANMessage(CAN_ID_GEAR, data, 1);
    
    // Engine status
    data[0] = engine_on ? 1 : 0;
    transmitCANMessage(CAN_ID_ENGINE_START, data, 1);
    
    // Odometer (4 bytes, km * 10)
    uint32_t odo_10km = static_cast<uint32_t>(odometer * 10);
    data[0] = odo_10km & 0xFF;
    data[1] = (odo_10km >> 8) & 0xFF;
    data[2] = (odo_10km >> 16) & 0xFF;
    data[3] = (odo_10km >> 24) & 0xFF;
    transmitCANMessage(CAN_ID_ODOMETER, data, 4);
    
    // Trip distance (2 bytes, 0.1km units)
    uint16_t trip_10km = static_cast<uint16_t>(trip_distance * 10);
    data[0] = trip_10km & 0xFF;
    data[1] = (trip_10km >> 8) & 0xFF;
    transmitCANMessage(CAN_ID_TRIP, data, 2);
    
    // Fuel rate (2 bytes, 0.1L/100km units)
    uint16_t fuel_rate_10 = static_cast<uint16_t>(fuel_rate * 10);
    data[0] = fuel_rate_10 & 0xFF;
    data[1] = (fuel_rate_10 >> 8) & 0xFF;
    transmitCANMessage(CAN_ID_FUEL_RATE, data, 2);
    
    // Gear position (1 byte)
    data[0] = static_cast<uint8_t>(current_gear + 1);
    transmitCANMessage(CAN_ID_GEAR_POS, data, 1);
}

void VehicleSimulator::cleanup() {
    restoreTerminal();
    if (can_socket >= 0) {
        close(can_socket);
    }
}

void VehicleSimulator::runSimulation() {
    cout << "Vehicle simulator running. Press Q to quit." << endl;
    
    while (running) {
        handleKeyboardInput();
        updateVehicleSimulation();
        transmitVehicleData();
        
        this_thread::sleep_for(chrono::milliseconds(50));
    }
    
    cout << "Simulator stopped." << endl;
}