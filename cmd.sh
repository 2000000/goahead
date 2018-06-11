#! /bin/sh

ifconfig eth0 192.168.3.100 netmask 255.255.255.0

cd /run/media/mmcblk0p1
PATH=/etc/init.d:$PATH
alias reboot='reboot -f'
echo 127.0.0.1 beamcontrol_zynq >> /etc/hosts
./goahead/goahead-test -v --home /run/media/mmcblk0p1/goahead/config /run/media/mmcblk0p1/goahead/html 127.0.0.1: 80  > /dev/null &
#./goahead/goahead-test -v --home /run/media/mmcblk0p1/goahead/config /run/media/mmcblk0p1/goahead/html 127.0.0.1: 80 
echo web server start...


cat /run/media/mmcblk0p1/Beamcontrol_SYS_TOP.bin > /dev/xdevcfg
echo "begin download fpga..."
./fpgaload 1
./fpgaload 2
./flashwrite cali_init

./flashwrite cali_int &

