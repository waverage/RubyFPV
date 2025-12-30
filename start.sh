#!/bin/bash

touch /tmp/debug
echo "Mounting file systems..."
#mount -o remount,rw /
#mount -o remount,rw /boot
echo "adding eth0 ip for debugging..."
ip link set eth0 up
ip addr add 192.168.1.2/24 dev eth0 2>/dev/null || true
ip addr show eth0
echo "stop network service..."
#systemctl stop NetworkManager
#systemctl stop wpa_supplicant
rfkill unblock wifi

echo "Launching..."
cd /home/pi/ruby
./ruby_start
echo "Launch done."

sleep 2
tail -f logs/log_system.txt