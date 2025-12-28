#!/bin/bash

sudo apt update
sudo apt install build-essential libpcap-dev libusb-1.0-0-dev libasound2-dev libi2c-dev libgpiod-dev


sudo apt purge wiringpi -y

git clone https://github.com/WiringPi/WiringPi.git
cd WiringPi
./build