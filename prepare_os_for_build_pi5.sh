#!/bin/bash

sudo apt update
sudo apt install build-essential libpcap-dev libusb-1.0-0-dev libasound2-dev libi2c-dev libgpiod-dev


sudo apt purge wiringpi -y

git clone https://github.com/WiringPi/WiringPi.git
cd WiringPi
./build


sudo apt install libsdl2-dev -y

sudo apt install libcairo2-dev pkg-config -y

cd /home/pi

git clone https://github.com/waverage/RubyFPV.git

mv RubyFPV ruby

sudo chmod 777 ruby

cd ruby

git checkout pi5

sudo chmod 777 /home/pi/ruby/start.sh

sudo cp /home/pi/ruby/systemd_fixip.service /etc/systemd/system/fixip.service
sudo systemctl enable fixip.service

# change /boot/firmware/config.txt
# put binaries to /home/pi/ruby/
# add executing "ruby_start" file in /home/pi/.profile and /root/.profile
#