# FireFly Client
#### About Project FireFly
Project FireFly is a low-voltage lighting control system consisting of momentary LED push buttons.  There are two types of devices: the controller and the client.  The controller and client are configured using the FireFly Configurator.

The FireFly Controller emits 5VDC on a pull-up resistor and detects a button press when the pin senses `LOW`.  The Controller may make a number of actions based on the button press event, including outputting voltage on another pin to a relay and/or broadcasting the event over MQTT.

The FireFly Client listens for MQTT messages from the Controller and uses those messages to determine when to change the luminance level of the button that was pressed, providing the user with feedback that the button was pressed.  FireFly Client can also change brightness levels based on MQTT messages to become more dim or brighter, such as at night or during the day.  Also supported are blink modes and snoring mode.

This project was inspired by [Jonathan Oxer's SuperHouseTV](https://www.superhouse.tv/25-arduino-home-automation-light-switch-controller/).

## Hardware
FireFly Client uses an ESP8266 over WiFi and supports up to 6 buttons being controlled.  An additional pin is used for resetting the controller to defaults.

| FireFly Client Position | Arduino Pin |
| -- | --| 
| A | GPIO5 | 
| B | GPIO4 | 
| C | GPIO14 | 
| D | GPIO12 | 
| E | GPIO13 | 
| F | GPIO15 | 
| Reset | GPIO10 <sup>1</sup> | 

<sup>1</sup> To reset to provisioning mode, short to ground.

The Gerber, BOM, and Pick & Place files area all located in the <a href="hardware/PCB">PCB</a> folder.

Custom faceplates to support a 16mm button in a standard US electrical box are in the <a href="hardware/faceplates">faceplates</a> folder.  Note, you must print both the faceplate and the matching adapter plate.

## Reset to Defaults
Grounding pin reset pin will cause the EEPROM to be marked as not provisioned and enable the user to re-enter provisioning mode.

## Prerequisite Libraries
ArduinoJson 6.16 or later
PubSubClient 2.8.0 or later

## Installation from Source
External libraries required are ArduinoJson, which is used to parse JSON files, and PubSubClient, which is used to send and receive MQTT messages.

After installation, the ESP8266 will boot into a provisioning state, having created its own WiFi hotspot based on the MAC address of the device.  When you connect to the `FireFly-XXXXXXXXXX` SSID, you will be assigned a DHCP address by FireFly Client.  Point your web browser to `192.168.1.1` to begin provisioning.

You must also flash the SPIFFs, which contains assets for the provisioning server.  The easiest way to flash the SPIFFs is using ESP8266 filesystem uploader from https://github.com/esp8266/arduino-esp8266fs-plugin.

## Installation from Release Package
You can also install a pre-compiled version by downloading one of the <a href="releases">releases</a>.  To flash the ESP8266, install ESPTool from https://github.com/espressif/esptool, if it is not already installed on your system.

Flashing the bootloader and the SPIFFs can be done in a single command:
```python3 esptool.py --port {PORT} --baud 115200 write_flash 0x00000 {BOOTLOADER_FILE} 0x00200000 /{SPIFF_FILE}```

## Provisioning
Before the device can be used it must be provisioned.  If you have connected LEDs to the various ports, you will notice them "rotating" in quick succession through each LED.

The process of provisioning requires three pieces of data to be input: the SSID of the WiFi access point you wish to use, the WPA key for the access point, and a IP address or FQDN of the <a href="https://github.com/BrentIO/FireFly_Configurator">Configurator</a> server to retrieve the bootstrap file.  You do not need to use the same access point for production as you do to bootstrap the device.

If the device is unable to connect to the WiFi network specified, or if the bootstrap file is not able to be retrieved, the device will automatically return to provisioning mode after timing out.  Simply reconnecting to the WiFi and re-pointing your browser to `192.168.1.1` will display the error message that occurred.

## Bootstrapping
The bootstrap is simply a JSON document that outlines the settings for the FireFly Client to use, including the WiFi SSID and key, MQTT server information, and definitions of the buttons assigned to this client.  Upon a successful bootstrap retrieval it will be written to the ESP8266's EEPROM.

IP addresses are always assigned via DHCP.

There is a definite limit to the number of times the ESP8266 EEPROM can be written to, and **you should minimize the number of times the version number changes in your bootstrap file to avoid unnecessary writing to the EEPROM.**

You may specify up to 8 defined intensities within the bootstrap, keeping in mind that `ON` and `OFF` are reserved and you do not need to include them.

## Firmware Upgrades
OTA firmware updates are supported, but as of version 1.14, OTA SPIFFs are **not** supported.  Firmware can defined in the Configurator and, using MQTT, the firmware update will be requested.

## Health and Status
FireFly will send both solicited and unsolicited health status messages on the `client` topic.  See MQTT Topics for more information.

## MQTT Topics
### Client Management Requests
FireFly will accept requests for certain actions over the `client` topic specified in the bootstrap, for example `/client/A1B2C3D4E5F6/health/get` will request the device's status to be sent.  The resulting health data will be returned on several topics:
- `/client/A1B2C3D4E5F6/uptime`, the number of milliseconds since the last boot of the device up to the maximum value of `unsigned long`.
- `/client/A1B2C3D4E5F6/status`, typically `ONLINE`.
- `/client/A1B2C3D4E5F6/ip`, the IP address of the device.
- `/client/A1B2C3D4E5F6/firmware`, the current firmware revision.
- `/client/A1B2C3D4E5F6/deviceName`, the name of the device, in this case, `A1B2C3D4E5F6`.
- `/client/A1B2C3D4E5F6/name`, the friendly name of the device.

Firmware can be updated by sending a message to the topic `/client/A1B2C3D4E5F6/firmware/set` with a payload containing the URL of the BIN file to download.  This should be done using the Configurator.

The bootstrap can also be updated by sending a message to the topic `/client/A1B2C3D4E5F6/bootstrap/set` with the URL of the bootstrap file.  This should be done using the Configurator.

### LED Control Requests
FireFly Client will accept requests for certain actions over the `event` topic specified in the bootstrap.

FireFly Client supports a number of message types to provide you with maximum control of each LED by button name, color, or globally for all buttons.  For brightness, either the percentage brightness or an enumerated name defined in the bootstrap can be used.  Reserved illumination names are `ON`, which set the LED brightness to 100%, and `OFF`, which will set the LED brightness to 0%.


**Button Pressed Event for LIVING_ROOM_1 Switch 4 Button 3:**
Post to topic: `/button/S4B3`
```
{
    "type": "EVENT",
    "value": "CLOSED",
    "actor": "LIVING_ROOM_1"
}
```

**Button Released Event for LIVING_ROOM_1 Switch 4 Button 3:**
Post to topic: `/button/S4B3`
```
{
    "type": "EVENT",
    "value": "OPEN",
    "actor": "LIVING_ROOM_1"
}
```

**Set brightness level to the defined intensity DAYTIME for all buttons on LIVING_ROOM_1 Switch:**
Post to topic: `/button/LIVING_ROOM_1`
```
{
    "type": "SWITCH_NAMED",
    "value": "DAYTIME",
    "actor": "LIVING_ROOM_1"
}
```


**Set brightness level to 70% for LIVING_ROOM_1 Switch 4 Button 3:**
Post to topic: `/button/S4B3`
```
{
    "type": "BUTTON_NUMERIC",
    "value": 70,
    "actor": "S4B3"
}
```

**Set brightness level to defined intensity DAYTIME for LIVING_ROOM_1 Switch 4 Button 3:**
Post to topic: `/button/S4B3`
```
{
    "type": "BUTTON_NAMED",
    "value": "DAYTIME",
    "actor": "S4B3"
}
```

**Set LIVING_ROOM_1 Switch 4 Button 3 to toggle Snore:**
Post to topic: `/button/S4B3`
```
{
    "type": "BUTTON_SNORE",
    "actor": "LIVING_ROOM_1"
}
```

**Set LIVING_ROOM_1 Switch 4 Button 3 to toggle Blink:**
Post to topic: `/button/S4B3`
```
{
    "type": "BUTTON_BLINK",
    "actor": "LIVING_ROOM_1"
}
```

**Set brightness level to 70% for all GREEN LEDs:**
Post to topic: `/button/global`
```
{
    "type": "COLOR_NUMERIC",
    "value": 70,
    "actor": "GREEN"
}
```

**Set brightness level to the defined intensity DAYTIME for all GREEN LEDs:**
Post to topic: `/button/global`
```
{
    "type": "COLOR_NAMED",
    "value": "DAYTIME",
    "actor": "GREEN"
}
```

**Set all BLUE LEDs to toggle Snore:**
Post to topic: `/button/global`
```
{
    "type": "COLOR_SNORE",
    "actor": "BLUE"
}
```

**Set BLUE LEDs to toggle Blink:**
Post to topic: `/button/global`
```
{
    "type": "COLOR_BLINK",
    "actor": "BLUE"
}
```

**Set all LEDs to toggle Snore:**
Post to topic: `/button/global`
```
{
    "type": "GLOBAL_SNORE"
}
```

**Set all LEDs to toggle Blink:**
Post to topic: `/button/global`
```
{
    "type": "GLOBAL_BLINK"
}
```

**Set all LEDs to 70% brightness level**
Post to topic: `/button/global`
```
{
    "type": "GLOBAL_NUMERIC",
    "value": 70
}
```
**Set brightness level to the defined intensity DAYTIME for all LEDs:**
Post to topic: `/button/global`
```
{
    "type": "GLOBAL_NAMED",
    "value": "DAYTIME"
}
```
