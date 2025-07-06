#include "VehicleSimulator.h"
#include <iostream>
#include <exception>

using namespace std;

int main() {
    try {
        VehicleSimulator simulator;
        simulator.runSimulation();
    } 
    catch (const exception& error) {
        cerr << "Error: " << error.what() << endl;
        return 1;
    }
    
    return 0;
}