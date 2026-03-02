# argonOneUpLidMonitor

This program is a replacement for the service provided by Argon40 that monitors the closing of the laptop lid.

A C++ daemon to monitor the Argon40 One Up laptop lid and shutdown after the lid has been closed after a configured number of seconds.


## C++

This program uses features from the C++ 23 standard. You will need a compiler that can compile C++ 23 features. It will compile on Raspberry Pi OS Trixie.

## Libraries

This project uses the following libraries. You will need to install the developer packages to compile all the programs.

* libgpiod
* libsystemd

It uses pkg-config to find these libraries.

### On Raspberry Pi OS

Use the following command to install the required libraries.

    sudo apt install libgpiod-dev

## Build

This project uses CMake. To build

     mkdir build
     cd build
     cmake ..
     make
     sudo make install

## Command Line Options

Usage: argonOneUpLidMonitor

    --help,-h - print usage and exit
    --shutdownCommand,-s <command> - command to execute when lid has been closed for the configured number of seconds (default: "shutdown -h now")

The shutdown command defaults to `shutdown -h now`. This is the same command as used by the Argon40 python script available for the One Up. This can be changed in the service file. For example

    ExecStart=/usr/local/bin/argonOneUpLidMonitor --daemon --pidfile /run/argonOneUpLidMonitor.pid --shutdownCommand "poweroff"

## Systemd service

To use this monitor you will need to install the provided systemd service file.

    sudo cp argonOneUpLidMonitor.service /etc/systemd/system/
    sudo systemctl daemon-reload
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

## Changelog

| **Version** | **Changes** |
|:-----------:|:----------- |
| 1.0.0 | <ul><li>Encapsulated monitor into a class</li><li>Removed use of libbsd.</li><li>No longer forks and so is a service type simple.</li></ul> |
| 1.0.1 | <ul><li>Read lid state from gpio at startup</li><li>Changed action closed/opened to state closed/open.</li></ul> |
| 1.1.0 | <ul><li>Moved timer to separate thread</li><li>Using blocking calls for gpio changes and timer</li></ul> |
