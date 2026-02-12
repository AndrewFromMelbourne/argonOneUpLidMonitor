# argonOneUpLidMonitor

This program is a replacement for the service provided by Argon40 that monitors the closing of the laptop lid.

A C++ daemon to monitor the Argon40 One Up laptop lid and shutdown after the lid has been closed after a configured number of seconds.


## C++

This program uses features from the C++ 23 standard. You will need a compiler that can compile C++ 23 features. It will compile on Raspberry Pi OS Trixie.

## Libraries

This project uses the following libraries. You will need to install the developer packages to compile all the programs.

* libbsd
* libgpiod

It uses pkg-config to find these libraries.

### On Raspberry Pi OS

Use the following command to install the required libraries.

    sudo apt install libbsd-dev libgpiod-dev

## Build

This project uses CMake. To build

     mkdir build
     cd build
     cmake ..
     make
     sudo make install

## Command Line Options

Usage: argonOneUpLidMonitor

    --daemon,-d - start in the background as a daemon
    --help,-h - print usage and exit
    --pidfile,-p <pidfile> - create and lock PID file
    --shutdownCommand,-s <command> - command to execute when lid has been closed for the configured number of seconds (default: "shutdown -h now")

The shutdown command defaults to `shutdown -h now`. This is the same command as used by the Argon40 python script available for the One Up. This can be changed in the service file. For example

    ExecStart=/usr/local/bin/argonOneUpLidMonitor --daemon --pidfile /run/argonOneUpLidMonitor.pid --shutdownCommand "poweroff"

## Systemd service

To use this monitor you will need to install the provided systemd service file.

    sudo cp argonOneUpLidMonitor.service /etc/systemd/system/
    sudo systemctl start argonOneUpLidMonitor.service
    sudo systemctl enable argonOneUpLidMonitor.service

## Monitoring the service

You can use either of the following commands to display the logs messages from the service

    journalctl -u argonOneUpLidMonitor.service
    sudo systemctl status argonOneUpLidMonitor.service

## Configuration

The program reads the timeout in seconds from the configuration file used by the Argon40 code.

    /etc/argononeupd.conf

It looks for an line in the configuration file as follows. In this example the timeout is set for 300 seconds or 5 minutes.

    lidshutdownsecs=300

