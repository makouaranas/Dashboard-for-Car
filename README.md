# Vehicle Dashboard System

A comprehensive real-time vehicle simulation and dashboard system that communicates via CAN bus protocol. This project consists of two main components: a C++ vehicle simulator that generates realistic vehicle data and a JavaFX dashboard application that displays the data with an attractive automotive-style interface.

## 🚗 System Overview

The system simulates a complete vehicle with realistic physics and displays the data through a professional automotive dashboard interface. Data flows from the C++ simulator through CAN bus to the JavaFX dashboard, which also logs all data to an SQLite database.

## 🔧 Components

### 1. Vehicle Simulator (C++)
- **Real-time Physics Engine**: Accurate vehicle dynamics with drag, rolling resistance, and acceleration
- **Engine Management**: Realistic RPM calculation, torque simulation, and engine start/stop
- **Transmission System**: Automatic transmission with gear shifting based on speed and RPM
- **Fuel System**: Dynamic fuel consumption with realistic fuel flow rates
- **CAN Bus Communication**: Broadcasts vehicle data via standardized CAN frames

### 2. Dashboard Application (JavaFX)
- **Modern UI**: Sleek automotive-style interface with glowing gauges and animations
- **Real-time Display**: Live updates of speed, RPM, fuel level, temperature, and more
- **Visual Indicators**: Turn signals, battery status, lights, and gear position
- **Data Logging**: SQLite database integration for historical data storage
- **Responsive Design**: Professional dashboard layout with smooth animations

## 🚀 Features

### Vehicle Systems Simulation
- **Engine Management**: RPM calculation, torque curves, engine temperature
- **Transmission**: Automatic gear shifting (P, R, N, D with 5 forward gears)
- **Fuel System**: Realistic consumption rates and fuel level monitoring
- **Electrical System**: Battery management and lighting control
- **Vehicle Dynamics**: Physics-based speed, acceleration, and braking

### Dashboard Features
- **Circular Gauges**: Speed and RPM with glowing effects and animations
- **Progress Bars**: Fuel level and engine temperature with color-coded warnings
- **Status Indicators**: Turn signals, battery, lights, and gear position
- **Trip Computer**: Odometer, trip distance, and fuel consumption rate
- **Real-time Clock**: Date and time display with external temperature

### Data Management
- **CAN Bus Protocol**: Industry-standard automotive communication
- **Database Logging**: SQLite database for data persistence
- **Real-time Updates**: 20Hz update rate for smooth operation

## 📋 Prerequisites

### For C++ Simulator
- Linux operating system (Ubuntu/Debian recommended)
- GCC compiler with C++17 support
- CAN utilities (`can-utils` package)
- Make build system

### For JavaFX Dashboard
- Java 11 or higher
- JavaFX runtime
- SQLite JDBC driver
- Linux system with CAN support

## 🛠️ Installation

### 1. Install System Dependencies
```bash
# Install CAN utilities and development tools
sudo apt update
sudo apt install can-utils build-essential openjdk-11-jdk

# Install JavaFX (if not included with your JDK)
sudo apt install openjfx
```

### 2. Setup Virtual CAN Interface
```bash
# Load CAN kernel module
sudo modprobe vcan

# Create virtual CAN interface
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0

# Verify interface is active
ip link show vcan0
```

### 3. Build the C++ Simulator
```bash
# Clone the repository
git clone <your-repo-url>
cd vehicle-simulator

# Build the simulator
make

# Or use the convenience targets
make setup-vcan  # Setup CAN interface (requires sudo)
make run         # Build and run the simulator
```

### 4. Compile the Java Dashboard
```bash
# Navigate to the Java source directory
cd java-dashboard

# Compile the Java classes
javac -cp ".:sqlite-jdbc-*.jar" *.java

# Or create a simple build script
```

## 🎮 Usage

### Starting the System

1. **Setup CAN Interface** (run once per session):
```bash
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
```

2. **Start the Vehicle Simulator**:
```bash
./vehicle_simulator
```

3. **Launch the Dashboard** (in a separate terminal):
```bash
java -cp ".:sqlite-jdbc-*.jar" h.CarDashboard
```

### Vehicle Controls

| Key | Function |
|-----|----------|
| `A` | Accelerator |
| `B` | Brake |
| `S` | Start/Stop Engine |
| `D` | Drive Mode |
| `R` | Reverse Mode |
| `N` | Neutral Mode |
| `P` | Park Mode |
| `L` | Toggle Lights |
| `Space` | Handbrake |
| `T` | Reset Trip |
| `←/→` | Turn Signals |
| `↑` | Hazard Lights |
| `Q` | Quit |

### Dashboard Features

- **Speed and RPM Gauges**: Real-time circular displays with glowing effects
- **Gear Indicator**: Shows current transmission mode and gear
- **Fuel and Temperature Bars**: Color-coded progress indicators
- **Turn Signals**: Animated arrows that blink when active
- **Status Lights**: Battery and lights indicators
- **Trip Computer**: Odometer, trip distance, and fuel consumption

## 🏗️ Architecture

### Data Flow
```
C++ Simulator → CAN Bus (vcan0) → JavaFX Dashboard → SQLite Database
```

### CAN Frame Structure
- **Speed**: 0x100 (2 bytes, km/h)
- **RPM**: 0x101 (2 bytes)
- **Fuel Level**: 0x102 (1 byte, percentage)
- **Engine Temperature**: 0x103 (1 byte, °C)
- **Turn Signals**: 0x104/0x105 (1 byte each)
- **Battery Status**: 0x106 (1 byte)
- **Lights**: 0x107 (1 byte)
- **Gear**: 0x108 (1 byte)
- **And more...**

### Database Schema
```sql
CREATE TABLE dashboard_data (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
    speed INTEGER, rpm INTEGER, fuel_level INTEGER,
    engine_temp INTEGER, turn_left BOOLEAN, turn_right BOOLEAN,
    battery_ok BOOLEAN, lights_on BOOLEAN, ext_temp INTEGER,
    odometer REAL, trip_distance REAL, fuel_rate REAL,
    transmission_mode CHAR(1)
);
```

## 🔧 Configuration

### Vehicle Parameters (C++)
- **Engine**: Max torque 300 Nm, redline 6000 RPM
- **Transmission**: 5-speed automatic with final drive ratio
- **Vehicle**: 1500 kg mass, 0.28 drag coefficient
- **Fuel**: 50L tank capacity, realistic consumption rates

### Dashboard Settings (Java)
- **Update Rate**: 50ms for smooth animations
- **Database**: Auto-saves every second
- **UI Colors**: Customizable gauge colors and effects

## 🚧 Troubleshooting

### Common Issues

1. **CAN Interface Not Found**:
```bash
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
```

2. **Permission Denied**:
```bash
# Add user to dialout group
sudo usermod -a -G dialout $USER
# Logout and login again
```

3. **JavaFX Not Found**:
```bash
# Install JavaFX
sudo apt install openjfx
# Or download from https://openjfx.io/
```

4. **Database Errors**:
- Check write permissions in current directory
- Ensure SQLite JDBC driver is in classpath

## 📊 Performance

- **Update Rate**: 20 Hz (50ms intervals)
- **CAN Throughput**: ~13 messages per cycle
- **Memory Usage**: ~50MB for dashboard application
- **Database Size**: ~1MB per hour of operation

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

## 📄 License

This project is licensed under the MIT License - see the LICENSE file for details.

## 🎯 Future Enhancements

- **GPS Integration**: Location tracking and mapping
- **Advanced Diagnostics**: Engine fault codes and diagnostics
- **Mobile App**: Remote monitoring via smartphone
- **Data Export**: CSV/JSON export functionality
- **Multiple Vehicle Support**: Fleet management capabilities
- **Web Interface**: Browser-based dashboard alternative

## 📞 Support

For issues, questions, or contributions:
- Create an issue in the GitHub repository
- Check the troubleshooting section
- Review the CAN bus setup requirements

---

**Note**: This system requires Linux with CAN support. Virtual CAN (vcan0) is used for development and testing.