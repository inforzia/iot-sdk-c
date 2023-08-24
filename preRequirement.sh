#!/bin/bash

# Update and upgrade system
sudo apt update
sudo apt upgrade -y

# Install essential packages
sudo apt install -y build-essential git cmake

# Clone the Paho MQTT C repository
git clone https://github.com/eclipse/paho.mqtt.c.git

# Create and navigate to build directory
mkdir build
cd build

# Install additional packages
sudo apt install -y cmake

# Run make install
sudo make install

# Configure the build
cmake ..

# Add library path to LD_LIBRARY_PATH
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/lib

echo "Setup completed!"

