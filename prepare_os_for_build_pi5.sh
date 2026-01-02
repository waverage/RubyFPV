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

# fix /boot/firmware/config.txt and /boot/firmware/cmdline.txt

sudo cp /home/pi/ruby/config.txt /boot/firmware/config.txt
sudo cp /home/pi/ruby/cmdline.txt /boot/firmware/cmdline.txt

# change /boot/firmware/config.txt
# put binaries to /home/pi/ruby/
# add executing "ruby_start" file in /home/pi/.profile and /root/.profile
#

# enable internet over wifi:
sudo nmcli device wifi rescan
sudo nmcli device wifi connect "php5" password "000zveroboy000"


# How to install rtl8821au driver
git clone https://github.com/morrownr/8821au-20210708
cd 8821au-20210708
sudo ./install-driver.sh

# fix usb power
sudo nano /boot/firmware/config.txt

# add to the end
#usb_max_current_enable=1

# Fix power detection
sudo -E rpi-eeprom-config --edit
# add to the end
#PSU_MAX_CURRENT=5000

# fix network services
# Stop and block NetworkManager (the main culprit)
sudo systemctl stop NetworkManager
sudo systemctl mask NetworkManager

# Stop and block the "Wait Online" service (prevents boot delays)
sudo systemctl stop NetworkManager-wait-online
sudo systemctl mask NetworkManager-wait-online

# Stop and block wpa_supplicant (prevents it from resetting your Monitor Mode)
sudo systemctl stop wpa_supplicant
sudo systemctl mask wpa_supplicant

# other services
sudo systemctl mask unattended-upgrades
sudo systemctl mask apport
sudo systemctl mask avahi-daemon
sudo systemctl mask avahi-daemon.socket

# add rubyfpv to auto start by cron
sudo crontab -e

# add it to the end
# @reboot sleep 5 && /bin/bash /home/pi/ruby/start.sh > /home/pi/ruby/cron_boot.txt 2>&1




#reboot -f --no-wall

# fix showing pi 5 os desktop

sudo systemctl set-default multi-user.target
sudo systemctl mask gdm3

sudo systemctl stop getty@tty1.service
sudo systemctl mask getty@tty1.service