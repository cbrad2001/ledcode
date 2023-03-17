# chmod +x ./ledtrigger.sh

cd /sys/class/leds/beaglebone:green:usr0 
echo morse-code | sudo tee trigger