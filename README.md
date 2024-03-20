# IMPRessif (Improv32)

This project aims to make the [Improv protocol](https://www.improv-wifi.com/) easy to implement on any ESP32 project.

## What is Improv?

In a nutshell Improv is a BT LE based protocol used to provide network credentials to an IoT device via (you guessed it!) Bluetooth.

The device requiring the credentials (the server) advertises the Improv service to any BLE client device who can provide the network credentials if desired to do so

## Current state

* Improv service is advertised correctly and can be seen by any client who expects said advertising
* Networking connection is not implemented yet, function placeholders are set in place
* Implementation uses the NimBLE library implementation of BLE for ESP32 as native implementation is to expensive for small chips as the ESP-WROOM-32 

## Dependencies
This Improv implementation uses the NimBLE-Arduino port that can be found on it's [GitHub repo](https://github.com/h2zero/NimBLE-Arduino)