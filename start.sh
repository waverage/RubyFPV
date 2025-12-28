#!/bin/bash

echo "Mounting file systems..."
mount -o remount,rw /
mount -o remount,rw /boot
echo "adding eth0 ip for debugging..."
ip addr add 192.168.1.2/24 dev eth0
echo "Launching..."
cd /home/pi/ruby
./ruby_start
echo "Launch done."