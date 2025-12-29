#!/bin/bash

sudo apt update
sudo apt install build-essential libpcap-dev libusb-1.0-0-dev libasound2-dev libi2c-dev libgpiod-dev


sudo apt purge wiringpi -y

git clone https://github.com/WiringPi/WiringPi.git
cd WiringPi
./build


sudo apt install libsdl2-dev -y

sudo apt install libcairo2-dev pkg-config -y

sudo chmod 777 /home/pi/ruby/start.sh

sudo cp /home/pi/ruby/systemd_ruby.service /etc/systemd/system/ruby.service
sudo systemctl enable ruby.service

# change /boot/firmware/config.txt
# put binaries to /home/pi/ruby/
# add executing "ruby_start" file in /home/pi/.profile and /root/.profile
#