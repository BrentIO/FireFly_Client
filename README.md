# FireFly Client
#### About Project FireFly
Project FireFly is a low-voltage lighting control system consisting of momentary LED push buttons.  There are two types of devices: the controller and the client.

The FireFly Controller emits 5VDC on a pull-up resistor and detects a button press when the pin senses `LOW`.  The Controller may make a number of actions based on the button press event, including outputting voltage on another pin to a relay and/or broadcasting the event over MQTT.

The FireFly Client listens for MQTT messages from the Controller and uses those messages to determine when to change the luminance level of the button that was pressed, providing the user with feedback that the button was pressed.  FireFly Client can also change brightness levels based on MQTT messages to become more dim or brighter, such as at night or during the day.  Also supported are blink modes and snoring mode.

This project was inspired by [Jonathan Oxer's SuperHouseTV](https://www.superhouse.tv/25-arduino-home-automation-light-switch-controller/).

## Hardware
FireFly Client uses a NodeMCU over WiFi and supports up to 6 buttons being controlled.  An additional pin is used for resetting the controller to defaults.
|FireFly Client Port|Arduino Pin|
|--|--|
|A|D1|
|B|D2|
|C|D3<sup>1</sup>|
|D|D5|
|E|D6|
|F|D7|
|Reset|SK <sup>2</sup>|

<sup>1</sup> This pin is high on boot and may cause the LED to be slightly illuminated before the controller is fully loaded.
<sup>2</sup> To reset to provisioning mode, short SK to ground.

## Reset to Defaults
Grounding pin SK will cause the EEPROM to be marked as not provisioned and enable the user to re-enter provisioning mode.

## Installation
Compile the source.  External libraries required are ArduinoJSON v6, which is used to parse JSON files, and PubSubClient, which is used to send and receive MQTT messages.

It is beneficial to connect your NodeMCU for serial output using 115200 baud.  There are a number of debug messages will assist you to understand what the device is doing and its current state.

After installation, the NodeMCU will boot into a provisioning state, having created its own WiFi hotspot based on the MAC address of the device.  When you connect to the `FireFly-XXXXXXXXXX` SSID, you will be assigned a DHCP address by FireFly Client.  Point your web browser to `192.168.1.1` to begin provisioning.

## Provisioning
Before the device can be used, but after the source has been compiled and binary uploaded to the NodeMCU, it must be provisioned.  

The process of provisioning requires three pieces of data to be input: the SSID of the WiFi access point you wish to use, the WPA key for the access point, and a URL to retrieve the bootstrap file.  You do not need to use the same access point for production as you do to bootstrap the device.

If the device is unable to connect to the WiFi network specified, or if the bootstrap file is not able to be retrieved, the device will automatically return to provisioning mode after timing out.

## Bootstrapping
The bootstrap is simply a JSON document that outlines the settings for the FireFly Client to use, including the WiFi SSID and key, MQTT server information, and definitions of the buttons assigned to this client.  Upon a successful bootstrap retrieval it will be written to the NodeMCU's EEPROM.

There is a definite limit to the number of times the NodeMCU EEPROM can be written to, and **you should minimize the number of times the version number changes in your bootstrap file to avoid unnecessary writing to the EEPROM.**

The bootstrap can be hosted on any local or remote HTTP server.

You should **always minify your JSON document**.  Documents up to 4000 bytes are supported.

When preferred, you can use the string token ```$DEVICENAME$``` to refer to the device's name.  The device name is the MAC address of the device without any punctuation (A1:B2:C3:d4:E5:F6 would be A1B2C3D4E5F6).

You can specify up to 6 LEDs in the bootstrap.

Supported LED port names are:
* PORT_A
* PORT_B
* PORT_C
* PORT_D
* PORT_E
* PORT_F

Supported colors are:
* Blue
* Green
* Yellow
* Red
* White

You may specify up to 8 defined intensities within the bootstrap, keeping in mind that `ON` and `OFF` are reserved and you do not need to include them here.  You may name the intensity anything you wish, however, `default` must always be included with a value set.

Example bootstrap payload:
```
{
    "version": 1,
    "name": "Living Room Stairway",
    "bootstrap": {
        "url": "http://server/FireFly/$DEVICENAME$.json",
        "refreshMilliseconds": 3600000
    },
    "firmware": {
        "url": "http://server/FireFly/firmware/$DEVICENAME$.json",
        "refreshMilliseconds": 3600000
    },
    "network": {
        "ssid": "mySSIDame",
        "key": "P@$$WORD"
    },
    "mqtt": {
        "serverName": "myMQTTServerName",
        "port": 1883,
        "username": "$DEVICENAME$",
        "password": "$DEVICENAME$",
        "topics": {
            "management": "/manage/$DEVICENAME$",
            "client": "/client/$DEVICENAME$",
            "event": "/buttons/"
        }
    },
    "buttons": [
        {
            "port": "PORT_A",
            "name": "LIVING_ROOM_1",
            "led": {
                "color": "YELLOW",
                "intensity": [
                    {
                        "name": "dayTime",
                        "value": 65
                    },
                    {
                        "name": "default",
                        "value": 50
                    },
                    {
                        "name": "nightTime",
                        "value": 30
                    }
                ]
            }
        },
        {
            "port": "PORT_B",
            "name": "LIVING_ROOM_2",
            "led": {
                "color": "RED",
                "intensity": [
                    {
                        "name": "dayTime",
                        "value": 65
                    },
                    {
                        "name": "default",
                        "value": 50
                    },
                    {
                        "name": "nightTime",
                        "value": 30
                    }
                ]
            }
        },
        {
            "port": "PORT_C",
            "name": "LIVING_ROOM_3",
            "led": {
                "color": "WHITE",
                "intensity": [
                    {
                        "name": "dayTime",
                        "value": 65
                    },
                    {
                        "name": "default",
                        "value": 50
                    },
                    {
                        "name": "nightTime",
                        "value": 30
                    }
                ]
            }
        },
        {
            "port": "PORT_D",
            "name": "LIVING_ROOM_4",
            "led": {
                "color": "GREEN",
                "intensity": [
                    {
                        "name": "dayTime",
                        "value": 65
                    },
                    {
                        "name": "default",
                        "value": 50
                    },
                    {
                        "name": "nightTime",
                        "value": 30
                    }
                ]
            }
        },
        {
            "port": "PORT_E",
            "name": "LIVING_ROOM_5",
            "led": {
                "color": "BLUE",
                "intensity": [
                    {
                        "name": "dayTime",
                        "value": 65
                    },
                    {
                        "name": "default",
                        "value": 50
                    },
                    {
                        "name": "nightTime",
                        "value": 30
                    }
                ]
            }
        },
        {
            "port": "PORT_F",
            "name": "LIVING_ROOM_6",
            "led": {
                "color": "BLUE",
                "intensity": [
                    {
                        "name": "dayTime",
                        "value": 65
                    },
                    {
                        "name": "default",
                        "value": 50
                    },
                    {
                        "name": "nightTime",
                        "value": 30
                    }
                ]
            }
        }
    ]
}
```

## Firmware Upgrades
OTA firmware updates are supported.  Firmware requires two files: a JSON document named with the device's MAC address which provides the version number of the firmware available for the device, and a URL for the location of the firmware binary file.
Example firmware JSON:
http://server/FireFly/firmware/A1B2C3D4E5F6.json
```
{
    "name": "Living Room Stairway",
    "version": 2,
    "url": "http://server/FireFly/firmware/2.bin"
}
```

## Health and Status
FireFly will send both solicited and unsolicited health status messages on the `clientTopic`.  See MQTT Topics for more information.

## MQTT Topics
### Requests
FireFly will accept requests for certain actions over the `management` topic specified in the bootstrap, for example `/manage/A1B2C3D4E5F6/status` will request the device's status to be sent.
`/status` will return the devices' status, typically `OK`.
`/reboot` will cause the device to reboot if there is a *non-empty payload* sent with the message.
`/firmwareUpdate` will check the firmware URL for a new firmware update, and if one is found, will automatically perform an OTA firmware update.
`/bootstrapUpdate` will check the bootstrap URL for a new bootstrap to update, and if one is found, will automatically install the new bootstrap.
### Responses
FireFly will respond to requests over the `client` topic specified in the bootstrap, for example `/client/A1B2C3D4E5F6/status` will contain the device's status.
### Example Control Payloads
FireFly will accept requests for certain actions over the `event` topic specified in the bootstrap.
FireFly supports a number of message types to provide you with maximum control of each LED by button name, color, or globally for all buttons.  For brightness, either the percentage brightness or an enumerated name defined in the bootstrap can be used.  Reserved illumination names are `ON`, which set the LED brightness to 100%, and `OFF`, which will set the LED brightness to 0%.


**Button Pressed Event for LIVING_ROOM_1:**
Post to topic: `/buttons/LIVING_ROOM_1`
```
{
    "type": "EVENT",
    "value": "CLOSED",
    "actor": "LIVING_ROOM_1"
}
```


**Button Released Event for LIVING_ROOM_1:**
Post to topic: `/buttons/LIVING_ROOM_1`
```
{
    "type": "EVENT",
    "value": "OPEN",
    "actor": "LIVING_ROOM_1"
}
```


**Set brightness level 70% for LIVING_ROOM_1:**
Post to topic: `/buttons/LIVING_ROOM_1`
```
{
    "type": "BUTTON_NUMERIC",
    "value": 70,
    "actor": "LIVING_ROOM_1"
}
```


**Set brightness level to the defined intensity DAYTIME for LIVING_ROOM_1:**
Post to topic: `/buttons/LIVING_ROOM_1`
```
{
    "type": "BUTTON_NAMED",
    "value": "DAYTIME",
    "actor": "LIVING_ROOM_1"
}
```

**Set LIVING_ROOM_1 to toggle Snore:**
Post to topic: `/buttons/LIVING_ROOM_1`
```
{
    "type": "BUTTON_SNORE",
    "actor": "LIVING_ROOM_1"
}
```

**Set LIVING_ROOM_1 to toggle Blink:**
Post to topic: `/buttons/LIVING_ROOM_1`
```
{
    "type": "BUTTON_BLINK",
    "actor": "LIVING_ROOM_1"
}
```

**Set brightness level to 70% for all GREEN LEDs:**
Post to topic: `/buttons/global`
```
{
    "type": "COLOR_NUMERIC",
    "value": 70,
    "actor": "GREEN"
}
```

**Set brightness level to the defined intensity DAYTIME for all GREEN LEDs:**
Post to topic: `/buttons/global`
```
{
    "type": "COLOR_NUMERIC",
    "value": "DAYTIME",
    "actor": "GREEN"
}
```

**Set all BLUE LEDs to toggle Snore:**
Post to topic: `/buttons/global`
```
{
    "type": "COLOR_SNORE",
    "actor": "BLUE"
}
```

**Set BLUE LEDs to toggle Blink:**
Post to topic: `/buttons/global`
```
{
    "type": "COLOR_BLINK",
    "actor": "BLUE"
}
```

**Set all LEDs to toggle Snore:**
Post to topic: `/buttons/global`
```
{
    "type": "GLOBAL_SNORE"
}
```

**Set all LEDs to toggle Blink:**
Post to topic: `/buttons/global`
```
{
    "type": "GLOBAL_BLINK"
}
```

**Set all LEDs to 70% brightness level**
Post to topic: `/buttons/global`
```
{
    "type": "GLOBAL_NUMERIC",
    "value": 70
}
```
**Set brightness level to the defined intensity DAYTIME for all LEDs:**
Post to topic: `/buttons/global`
```
{
    "type": "GLOBAL_NAMED",
    "value": "DAYTIME"
}
```
