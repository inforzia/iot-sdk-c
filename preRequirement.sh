#!/bin/bash

# Update and upgrade system
sudo apt update
sudo apt upgrade -y

# Install essential packages
sudo apt install -y build-essential git cmake

# Clone the Paho MQTT C repository
git clone https://github.com/eclipse/paho.mqtt.c.git

# Install additional packages
sudo apt install -y cmake

# Create and navigate to build directory
cd paho.mqtt.c
mkdir build
cd build
# Configure the build
cmake ..
# Run make install
make
sudo make install

# Add library path to LD_LIBRARY_PATH
cd /home/flomon/rda-bmt/iot-2/c-sdk-test/iot-sdk-c
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/lib

echo "Setup completed!"

