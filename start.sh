#!/bin/bash

export PATH=$PATH:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

touch /tmp/debug

echo "Mounting file systems..."

#mount -o remount,rw /
#mount -o remount,rw /boot

echo "stop getty"

systemctl stop getty@tty1.service

echo "adding eth0 ip for debugging..."

ip link set eth0 up

ip addr add 192.168.1.2/24 dev eth0 2>/dev/null || true

ip addr show eth0

echo "stop network service..."

#systemctl stop NetworkManager
#systemctl stop wpa_supplicant
#rfkill unblock wifi
echo "Fixing semaphores..."
# Clean up POSIX semaphores
rm -f /dev/shm/sem.RUBY*
# Clean up System V semaphores (just in case)
for semid in $(ipcs -s | awk '/0x/ {print $2}'); do ipcrm -s $semid; done

echo "Launching..."

cd /home/pi/ruby

./ruby_start

echo "Launch done."

sleep 2

mkdir -p logs
touch logs/log_system.txt
tail -f logs/log_system.txt
