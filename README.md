# Introduction

This is a daemon for controlling the zBitx transceiver. It is based on the official GTK client, from which all GTK GUI elements have been removed. This means that a graphical desktop environment is no longer required. The device is still controlled via the touchscreen of the zBitx frontend or via a browser. The daemon is started and stopped with the support of systemd.

At the moment, installation is only recommended for users who have experience with Linux and Raspberry Pi or are willing to learn. It is recommended to install it on an additional SD card with the latest Raspberry Pi OS. Only this is described below. One advantage is that if problems arise, you can always revert to the original software by simply changing the SD cards. However, installation and use on the original SD card is possible in principle.


# Installation

## Requirements

- PC for creating the SD card and accessing the zBitx, preferably via SSH. In principle, this could also be done with just the zBitx and a connected monitor, keyboard, and mouse.
- SD card with at least 8 GB
- SD card reader (integrated in the PC or with USB connection)
- WLAN
- A copy of the configuration and log data from the currently used (old) SD card:
  - /home/pi/sbitx/data/hw_settings.ini
  - /home/pi/sbitx/data/sbitx.db
  - /home/pi/sbitx/data/user_settings.ini


## Preparing the SD card

- Install Raspberry Pi Imager on the PC
- Insert the new SD card into the connected card reader
- Start Raspberry Pi Imager and select the following settings:
- Operating System: Raspberry Pi OS (other) > Raspberry Pi OS Lite (64-bit)
- Storage: select the new SD card
- Advanced Options (click on the gear icon):
  - Set hostname: zbitx
  - Activate “Enable SSH”, authentication with password or public key
  - Activate “Set username and password” and enter these
  - Activate “Configure wireless LAN” and set it up
  - Activate “Set locale settings,” select “Time zone” and “Keyboard layout”
  - Save

## WiFi problems

At the time this guide was created, it was not possible to connect to some WiFi networks. The cause is probably an incompatibility of the firmware for the WiFi chip used in the Raspberry Pi Zero 2 W: https://github.com/raspberrypi/bookworm-feedback/issues/279. In most cases, creating a file named “brcmfmac.conf” in the “/etc/modprobe.d” directory will help. Contents:
```
options brcmfmac feature_disable=0x2000
```
Without a working WiFi connection, there are only two ways to create this file:

### On the PC

To do this, the SD card must be mounted on the PC. Since the partition with the directory “/etc/modprobe.d” is formatted with ext4, this is only possible with a PC running Linux. After mounting, enter the following commands in a console:
```
sudo mkdir <mountpoint>/etc/modprobe.d
echo “options brcmfmac feature_disable=0x2000” | sudo tee <mountpoint>/etc/modprobe.d/brcmfmac.conf
```
<mountpoint> has to be replaced with the directory where the SD card was mounted.


### On the zBitx

To do this, the prepared SD card must already be inserted into the zBitx and a monitor and keyboard must also be connected. After logging in to the console, enter the following commands:
```
sudo mkdir /etc/modprobe.d
echo “options brcmfmac feature_disable=0x2000” | sudo tee /etc/modprobe.d/brcmfmac.conf
sudo reboot
```
After restarting, the zBitx should connect to the WiFi network.

## Installing zbitxd

First, an update of the Raspberry Pi OS is recommended:
```
sudo apt update
sudo apt upgrade
reboot
```

Then install the packages that zbitxd requires as dependencies:
```
sudo apt install git libasound2-dev libfftw3-dev libsqlite3-dev libsystemd-dev sqlite3
```
Of course, any other packages can be added.

WiringPi is used to control the GPIO pins. This is not available as a ready-made package and must be installed as follows:
```
cd
git clone https://github.com/wiringpi/wiringpi
cd wiringpi
./build
```

zbitxd requires the ALSA aloop driver:
```
echo “snd-aloop” | sudo tee /etc/modules
echo “options snd-aloop enable=1,1,1 index=1,2,3” | sudo tee /etc/modprobe.d/snd-aloop.conf
```

The boot configuration needs to be modified:
```
echo “# zBitx related options” | sudo tee -a /boot/firmware/config.txt
echo “gpio=4,5,9,10,11,17,22,27=ip,pu” | sudo tee -a /boot/firmware/config.txt
echo “gpio=24,23=op,pu” | sudo tee -a /boot/firmware/config.txt
echo “avoid_warnings=1” | sudo tee -a /boot/firmware/config.txt
echo “dtoverlay=audioinjector-wm8731-audio” | sudo tee -a /boot/firmware/config.txt
echo “dtoverlay=i2c-rtc-gpio,ds1307,bus=2,i2c_gpio_sda=13,i2c_gpio_scl=6” | sudo tee -a /boot/firmware/config.txt
sudo sed -i “s/dtparam=audio=on/##dtparam=audio=on/” /boot/firmware/config.txt
sudo sed -i “s/dtoverlay=vc4-kms-v3d/dtoverlay=vc4-kms-v3d,noaudio/” /boot/firmware/config.txt
```

Unlike the original zBitx software, zbitxd does not use its own routines for accurate time. Instead, it uses the Raspberry Pi's system time. Therefore, these must be very accurate, especially for FT8. To achieve this, the fake hardware clock is disabled:
```
sudo systemctl disable fake-hwclock
```

The system time is now set to the RTC (RealTime Clock) of the zBitx when booting. For this, it is important that the RTC battery is installed in the zBitx. This is not the case with all zBitx devices delivered.
If the WiFi connection is active, the system time is additionally updated via NTP. For outdoor applications, furthersteps are recommended to ensure a more accurate system time, e.g., using GPS.

Now the source code of zbitxd can be downloaded:
```
cd
git clone https://github.com/dg0jde/zbitxd.git
```

The update script installs the latest release of zbitxd:
```
~/zbitxd/update
```
This command is also used for future updates.


If you want to install the current developer version instead, use the following commands:
```
cd ~/zbitxd
git checkout main
git pull
make
sudo make install
```

The copies of the previous configurations and log data (hw_settings.ini, sbitx.db, user_settings.ini) should now be copied to /var/lib/zbitxd. User and Group must be set to “zbitxd” with:
```
sudo chown zbitxd:zbitxd /var/lib/zbitxd/*
```

Now zbitxd can be started and should work as usual, only without the GTK GUI:
```
sudo systemctl daemon-reload
sudo systemctl start zbitxd
```

To start automatically when booting, use this command:
```
sudo systemctl enable zbitxd
```

# Additional extensions
## Automated WiFi AccessPoint

from:  
https://www.raspberryconnect.com/projects/65-raspberrypi-hotspot-accesspoints/203-automated-switching-accesspoint-wifi-network
```
cd /tmp
curl “https://www.raspberryconnect.com/images/scripts/AccessPopup.tar.gz” -o AccessPopup.tar.gz
tar -xvf ./AccessPopup.tar.gz
cd AccessPopup
sudo ./installconfig.sh
```
- Installation: press 1
- Configuration SSID and pre-shared key: press 2 (e.g. zBitxAP/1234567890)
- End: enter 10
