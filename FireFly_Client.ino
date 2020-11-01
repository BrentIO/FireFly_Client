#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include "Structures.h"
#include <FS.h>   // Include the SPIFFS library, download from https://github.com/esp8266/arduino-esp8266fs-plugin

const char* FIRMWARE_VERSION = "1.0";
const char* PRODUCT_NAME = "FireFly Switch";

/* These are defined by the physical layout of the board by GPIO Number */
#define PORT_A  5
#define PORT_B 4
#define PORT_C 14
#define PORT_D 12
#define PORT_E 13
#define PORT_F 15
#define FACTORY_RESET 10

/* Define the LED style, where blink is binary 0/100%; snore is 0-100-0% */
#define STYLE_NORMAL 0
#define STYLE_BLINK 1
#define STYLE_SNORE 2
#define STYLE_ROTATE 3

#define EEPROM_DATA_START 6 //Position where the actual EEPROM data begins; Prior to this is used to store provisioning data
#define HEALTH_UPDATE_EVERY_MS 1680000 //Every 28 minutes
#define LED_BLINK_MS 750

unsigned long healthUpdateLastHandled = 0;
unsigned long blinkLastHandled = 0;

struct structSettings settings;

struct structLED leds[6]; //Board Maximum
int countOfLEDs = 0; //Essentially, the length of leds[].

WiFiClient espClient; //WiFi client on the ESP8266
PubSubClient mqttClient(espClient); //MQTT client binding to the ESP8266 WiFi
ESP8266WebServer webServer(80); //Web server client to handle provisioning
ESP8266HTTPUpdateServer httpUpdater; //HTTP server client to handle OTA updates


void setup() {

  Serial.begin(115200);

  //Set pin FACTORY_RESET to pullup
  pinMode(FACTORY_RESET, INPUT_PULLUP);

  //Determine if the FACTORY_RESET has been pulled low, if so, blow away the EEPROM
  if (digitalRead(FACTORY_RESET) == LOW) {

    if (settings.deviceIsProvisioned == true) {

      EEPROMWrite("false", 0);
      settings.deviceIsProvisioned = false;
    }

  }

  //Set the device name from the MAC address
  settings.deviceName = WiFi.macAddress();
  settings.deviceName.toUpperCase();
  settings.deviceName.replace(":", "");

  //Set the hostname
  WiFi.hostname("FireFly-" + settings.deviceName);

  //Check the provisioning status
  checkDeviceProvisioned();

  //Set the device's current mode depending on if it is provisioned or not
  if (settings.deviceIsProvisioned == false) {

    //Configure for provisioning mode
    setupProvisioningMode();

  } else {

    setupNormalMode();

  }

}


void loop() {

  if(settings.deviceIsProvisioned == false){
  
    loopProvisioningMode();
  
  }else{
  
    loopNormalMode();
  
  }
}


void setupNormalMode(){

  //Read the data from EEPROM
  //readEEPROMToRAM();

  //Attempt to connect to the WiFi Network as a client
  WiFi.mode(WIFI_STA);

  delay(500);

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(settings.network.ssidName.c_str(), settings.network.wpaKey.c_str());
  }

  //Print output while waiting for WiFi to connect
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  //Configure the mqtt Client
  mqttClient.setServer(settings.mqttServer.serverName.c_str(), settings.mqttServer.port);
  mqttClient.setCallback(mqttCallback);

  //Subscribe to the required MQTT topics
  mqttClient.subscribe(settings.mqttServer.controlTopic.c_str());
  mqttClient.subscribe(settings.mqttServer.clientTopic.c_str());
  mqttClient.subscribe(settings.mqttServer.eventTopic.c_str());

  //Setup the LEDs for use
  setupLEDs();
  
}


void blinkLEDs() {

  //Find any LED's that should be blinking
  for (int i = 0; i < countOfLEDs; i++) {

    if (leds[i].style == STYLE_BLINK) {

      if (leds[i].illumination == 0) {

        //Turn on the LED to the pre-last value
        setBrightness(&leds[i], leds[i].styleData);

      } else {

        //Turn off the LED
        setBrightness(&leds[i], "OFF");

      }
    }
  }

  //Set the blink handler
  blinkLastHandled = millis();
}


void snoreLEDs() {

  //Find any LED's that should be blinking
  for (int i = 0; i < countOfLEDs; i++) {

    if (leds[i].style == STYLE_SNORE) {

      //Determine if we are increasing (not zero) or decreasing (zero)
      if (leds[i].styleData != 0) {

        //Increment the illumination
        leds[i].illumination++;

        //If we are now greater than PWMRANGE, switch the direction for the next iteration
        if (leds[i].illumination > PWMRANGE) {
          leds[i].styleData = 0;
        }

        //Set the new value
        setBrightness(&leds[i], leds[i].illumination, true);

      } else {

        //Decrement the illumination
        leds[i].illumination--;

        //If we are now less than 10, switch the direction for the next iteration; Less than 10 makes the LED flash off
        if (leds[i].illumination < 10) {
          leds[i].styleData = 1;
        }

        //Set the new value
        setBrightness(&leds[i], leds[i].illumination, true);

      }
    }
  }
}


void rotateLEDs(){

  //Only run this function every 1 second
  if(millis() - blinkLastHandled < 200){
    return;
  }

  //Find any LED's that should be on
  for (int i = 0; i < countOfLEDs; i++) {

    if(leds[i].illumination == PWMRANGE){

      //Turn off this LED
      setBrightness(&leds[i], int(PWMRANGE/16));

      if((i+1) == countOfLEDs){

        //This is LED 5, turn on LED 0
        setBrightness(&leds[0], PWMRANGE);

      }else{
        //Turn on the next LED
        setBrightness(&leds[(i+1)], PWMRANGE);

      }

      //Update the last handled
      blinkLastHandled = millis();
      return;
    }

  }
   
}


void mqttCallback(char* topic, byte* payload, unsigned int length) {
  /* This function will handle all incoming requests for subscribed topics */

  String receivedPayload = "";

  //Assemble the payload data
  for (int i = 0; i < length; i++) {
    receivedPayload = String(receivedPayload + (char)payload[i]);
  }

  //Clean the payload received
  receivedPayload.trim();

  //Pass the payload off for further processing
  handleMQTTMessageReceived(String(topic), receivedPayload);

}


void publishMQTT(String topic, String payload) {
  /*
     Sends the requested payload to the requested topic, and also broadcasts the message on the serial line.
  */

  //Clean the strings
  topic.trim();
  payload.trim();

  mqttClient.publish(String(topic).c_str(), String(payload).c_str());

}


void handleMQTTMessageReceived(String topic, String payload) {
  /*
    Handles incoming messages from MQTT.  Payloads must exactly match.
    Output is echoed on telnet and MQTT /client/# topic.
  */

  //Trim the payload
  payload.trim();

  //See if the payload requests a restart
  if (topic == settings.mqttServer.controlTopic + "/restart" && payload != "") {

    turnOffAllLEDs();

    publishMQTT(settings.mqttServer.clientTopic + "/status", F("Restarting"));

    delay(500);

    ESP.restart();

    return;

  }

  //See if the payload requests a health check
  if (topic == settings.mqttServer.controlTopic + "/status") {

    handleHealth();

    return;
  }

  //See if the payload requests a firmware update check
  if (topic == settings.mqttServer.controlTopic + "/firmwareUpdate") {

    checkFirmwareUpgrade("TODO:GET_FROM_BODY");

    return;
  }

  //See if the payload requests a bootstrap update check
  if (topic == settings.mqttServer.controlTopic + "/bootstrapUpdate") {

    //checkBootstrapUpgrade();

    return;
  }

  //See if the topic was an event
  if (topic.startsWith(settings.mqttServer.eventTopic) == true) {

    if (topic.endsWith("/global") == true) {
      handleGlobalEventTopic(topic, payload);
    }
    else {
      handleEventTopic(topic, payload);
    }
  }

}


void handleEventTopic(String topic, String payload) {

  String messageActor;

  messageActor = topic.substring(topic.lastIndexOf("/") + 1);

  //Find the button
  for (int i = 0; i < countOfLEDs; i++) {

    if (leds[i].name == messageActor) {

      //See if we are opening or closing
      if (payload == "ON") {

        //Remember the current illumination value
        leds[i].styleData = leds[i].illumination;

        //Set the illumination to OFF
        setBrightness(&leds[i], "OFF");
      }

      if (payload == "OFF") {

        if (leds[i].styleData != 0) {

          //Restore the previous value
          setBrightness(&leds[i], leds[i].styleData);

        } else {

          //Use the default
          setBrightness(&leds[i], "DEFAULT");

        }
      }

      if (payload == "MINIMUM" || payload == "MAXIMUM") {

        //Remember the current illumination value
        leds[i].styleData = leds[i].illumination;

        setBrightness(&leds[i], F("OFF"));

        delay(100);

        //Use the default
        setBrightness(&leds[i], F("MAXIMUM"));

        delay(100);

        setBrightness(&leds[i], F("OFF"));

        delay(100);

        //Use the default
        setBrightness(&leds[i], F("MAXIMUM"));

        delay(100);

        setBrightness(&leds[i], F("OFF"));

        delay(100);

        //Use the default
        setBrightness(&leds[i], F("MAXIMUM"));

        delay(100);

      }
    }
  }

}


void handleGlobalEventTopic(String topic, String payload) {
  /* Handles all of the business logic */

  //Create a jsonDocument object to deserialize into
  DynamicJsonDocument jsonDoc(EEPROMLength);

  //Deserialize the response
  auto jsonParseError = deserializeJson(jsonDoc, payload);

  if (jsonParseError) {
    return;
  }

  String messageType;
  String messageValue;
  String messageActor;

  //Get the type and value from the payload
  messageType = jsonDoc["type"].as<String>();
  messageValue = jsonDoc["value"].as<String>();
  messageActor = jsonDoc["actor"].as<String>();


  //Clean the data
  messageType.toUpperCase();
  messageValue.toUpperCase();
  messageActor.toUpperCase();
  messageType.trim();
  messageValue.trim();
  messageActor.trim();


  if (messageType == "EVENT") {

    //Find the button
    for (int i = 0; i < countOfLEDs; i++) {

      if (leds[i].name == messageActor) {

        //See if we are opening or closing
        if (messageValue == "CLOSED") {

          //Remember the current illumination value
          leds[i].styleData = leds[i].illumination;

          //Set the illumination to OFF
          setBrightness(&leds[i], F("OFF"));
        }

        if (messageValue == "OPEN") {

          if (leds[i].styleData != 0) {

            //Restore the previous value
            setBrightness(&leds[i], leds[i].styleData);

          } else {

            //Use the default
            setBrightness(&leds[i], F("DEFAULT"));

          }
        }
      }
    }
  }

  if (messageType == "BUTTON_NUMERIC") {

    //Find the button
    for (int i = 0; i < countOfLEDs; i++) {

      if (leds[i].name == messageActor) {

        //If the LED is currently snoring, make it stop
        if (leds[i].style == STYLE_SNORE) {
          leds[i].style = STYLE_NORMAL;
        }

        //Set the illumination to the numeric value
        setBrightness(&leds[i], calculateBrightness((int)jsonDoc["value"]));
      }

      //Set the ON brightness to the received message when blinking
      if (leds[i].style == STYLE_BLINK) {
        leds[i].styleData = calculateBrightness((int)jsonDoc["value"]);
      }
    }
  }

  if (messageType == "BUTTON_NAMED") {

    //Find the button
    for (int i = 0; i < countOfLEDs; i++) {

      if (leds[i].name == messageActor) {

        //If the LED is currently snoring, make it stop
        if (leds[i].style == STYLE_SNORE) {
          leds[i].style = STYLE_NORMAL;
        }

        //Set the illumination to the numeric value
        setBrightness(&leds[i], (String)messageValue);
      }

      //Set the ON brightness to the received message when blinking
      if (leds[i].style == STYLE_BLINK) {
        leds[i].styleData = calculateBrightness((int)jsonDoc["value"]);
      }
    }
  }

  if (messageType == "BUTTON_BLINK") {

    //Find the button
    for (int i = 0; i < countOfLEDs; i++) {

      if (leds[i].name == messageActor) {

        //Toggle Blink
        if (leds[i].style != STYLE_BLINK) {

          //Store the existing illuminatoin value
          leds[i].styleData = leds[i].illumination;
          leds[i].style = STYLE_BLINK;

        } else {

          leds[i].style = STYLE_NORMAL;

          //Set the LED to the original brightness
          setBrightness(&leds[i], leds[i].styleData);

        }
      }
    }
  }

  if (messageType == "BUTTON_SNORE") {

    //Find the button
    for (int i = 0; i < countOfLEDs; i++) {

      if (leds[i].name == messageActor) {

        //Toggle Snore
        if (leds[i].style != STYLE_SNORE) {

          leds[i].style = STYLE_SNORE;

        } else {

          leds[i].style = STYLE_NORMAL;

          //Set the brightness to the default
          setBrightness(&leds[i], F("DEFAULT"));

        }
      }
    }
  }

  if (messageType == "COLOR_NUMERIC") {

    //Find the button
    for (int i = 0; i < countOfLEDs; i++) {

      if (leds[i].color == messageActor) {

        //If the LED is currently snoring, make it stop
        if (leds[i].style == STYLE_SNORE) {
          leds[i].style = STYLE_NORMAL;
        }

        //Set the illumination to the numeric value
        setBrightness(&leds[i], calculateBrightness((int)jsonDoc["value"]));
      }

      //Set the ON brightness to the received message when blinking
      if (leds[i].style == STYLE_BLINK) {
        leds[i].styleData = calculateBrightness((int)jsonDoc["value"]);
      }
    }
  }

  if (messageType == "COLOR_NAMED") {

    //Find the button
    for (int i = 0; i < countOfLEDs; i++) {

      if (leds[i].color == messageActor) {

        //If the LED is currently snoring, make it stop
        if (leds[i].style == STYLE_SNORE) {
          leds[i].style = STYLE_NORMAL;
        }

        //Set the illumination to the numeric value
        setBrightness(&leds[i], (String)messageValue);
      }

      //Set the ON brightness to the received message when blinking
      if (leds[i].style == STYLE_BLINK) {
        leds[i].styleData = calculateBrightness((int)jsonDoc["value"]);
      }
    }
  }

  if (messageType == "COLOR_BLINK") {

    //Find the button
    for (int i = 0; i < countOfLEDs; i++) {

      if (leds[i].color == messageActor) {

        //Toggle Blink
        if (leds[i].style != STYLE_BLINK) {

          //Store the existing illuminatoin value
          leds[i].styleData = leds[i].illumination;

          leds[i].style = STYLE_BLINK;


        } else {

          leds[i].style = STYLE_NORMAL;

          //Set the LED to the original brightness
          setBrightness(&leds[i], leds[i].styleData);

        }
      }
    }
  }

  if (messageType == "COLOR_SNORE") {

    //Find the button
    for (int i = 0; i < countOfLEDs; i++) {

      if (leds[i].color == messageActor) {

        //Toggle Snore
        if (leds[i].style != STYLE_SNORE) {

          leds[i].style = STYLE_SNORE;

        } else {

          leds[i].style = STYLE_NORMAL;

          //Set the brightness to the default
          setBrightness(&leds[i], F("DEFAULT"));

        }
      }
    }
  }

  if (messageType == "GLOBAL_NUMERIC") {

    //Iterate through the buttons
    for (int i = 0; i < countOfLEDs; i++) {

      //If the LED is currently snoring, make it stop
      if (leds[i].style == STYLE_SNORE) {
        leds[i].style = STYLE_NORMAL;
      }

      //Set the illumination to the numeric value
      setBrightness(&leds[i], calculateBrightness((int)jsonDoc["value"]));

      //Set the ON brightness to the received message when blinking
      if (leds[i].style == STYLE_BLINK) {
        leds[i].styleData = calculateBrightness((int)jsonDoc["value"]);
      }
    }
  }

  if (messageType == "GLOBAL_NAMED") {
    //Iterate through the buttons
    for (int i = 0; i < countOfLEDs; i++) {

      //If the LED is currently snoring, make it stop
      if (leds[i].style == STYLE_SNORE) {
        leds[i].style = STYLE_NORMAL;
      }

      //Set the illumination to the numeric value
      setBrightness(&leds[i], (String)messageValue);

      //Set the ON brightness to the received message when blinking
      if (leds[i].style == STYLE_BLINK) {
        leds[i].styleData = calculateBrightness((int)jsonDoc["value"]);
      }
    }

  }

  if (messageType == "GLOBAL_BLINK") {

    //Find the button
    for (int i = 0; i < countOfLEDs; i++) {

      //Toggle Blink
      if (leds[i].style != STYLE_BLINK) {

        //Store the existing illuminatoin value
        leds[i].styleData = leds[i].illumination;

        leds[i].style = STYLE_BLINK;

      } else {

        leds[i].style = STYLE_NORMAL;

        //Set the LED to the original brightness
        setBrightness(&leds[i], leds[i].styleData);
      }
    }
  }

  if (messageType == "GLOBAL_SNORE") {

    //Find the button
    for (int i = 0; i < countOfLEDs; i++) {

      //Toggle Snore
      if (leds[i].style != STYLE_SNORE) {

        leds[i].style = STYLE_SNORE;

      } else {

        leds[i].style = STYLE_NORMAL;

        //Set the brightness to the default
        setBrightness(&leds[i], F("DEFAULT"));

      }
    }
  }
}


void reconnectMQTT() {

  //Ensure the LEDs are off
  turnOffAllLEDs();

  // Loop until we're reconnected
  while (!mqttClient.connected()) {

    // Attempt to connect
    if (mqttClient.connect(settings.deviceName.c_str(), settings.mqttServer.username.c_str(), settings.mqttServer.password.c_str())) {

      restoreAllLEDs();

      // Publish an announcement on the client topic
      publishMQTT(settings.mqttServer.clientTopic + "/status", F("Started"));

      //Send the health event
      handleHealth();

      //Subscribe to the management topic wildcard
      createSubscription(settings.mqttServer.controlTopic + "/#");

      //Create a subscription to the global button topic
      createSubscription(settings.mqttServer.eventTopic + "/global");

      //Subscribe to each LED button press topic
      for (int i = 0; i < countOfLEDs; i++) {

        createSubscription(settings.mqttServer.eventTopic + leds[i].name);

      }

    } else {

      //Make sure we're connected to WiFi
      if (WiFi.status() != WL_CONNECTED) {
        WiFi.begin(settings.network.ssidName.c_str(), settings.network.wpaKey.c_str());
      }

      //Print output while waiting for WiFi to connect
      while (WiFi.status() != WL_CONNECTED) {
        delay(500);
      }

      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}


boolean createSubscription(String topic) {
  /* A wrapper for the mqtt subscribe method */

  //Remove any duplicated slashes
  topic.replace("//", "/");

  //Make sure we didn't end our subscription with a trailing /
  if (topic.substring(topic.length() - 1) == "/") {

    topic = topic.substring(0, topic.length() - 2);

  }

}


int calculateBrightness(int brightnessPercentage) {

  int returnValue = 0;

  //Calculate the PWM value to send to the pin based on the percentage passed
  returnValue = (int)(((double)brightnessPercentage / 100) * PWMRANGE);

  if (returnValue > PWMRANGE) {
    returnValue = PWMRANGE;
  }

  if (returnValue < 0) {
    returnValue = 0;
  }

  return returnValue;

}


void checkDeviceProvisioned() {
  /*
   * This function will check the EEPROM to see if the device has been provisioned.  If the EEPROM is not "TRUE", it will assume the device has not been provisioned and will
   * set the setting accordingly.
   */

  char eepromData[5];

  //Read the EEPROM starting at memory position 0
  EEPROM.get(0, eepromData);

   if(strcmp("TRUE", eepromData) == 0){

      //The device has been provisioned, set the setting to true
      settings.deviceIsProvisioned = true;
      
   }else{

      //The device has not been provisioned, set the setting to false
      settings.deviceIsProvisioned = false;
    
   }

}


void setupProvisioningMode() {
  /*
     Configures necessary items for when the device has not been provisioned.
  */

  //Set the number of LED's to 6
  countOfLEDs = 6;

  //Set each LED output pin to the port number
  leds[0].pin = PORT_A;
  leds[1].pin = PORT_B;
  leds[2].pin = PORT_C;
  leds[3].pin = PORT_D;
  leds[4].pin = PORT_E;
  leds[5].pin = PORT_F;

  //Set the LED to be an output and set the default brightness level while in provisioning mode
  for(int i=0; i < countOfLEDs; i++){

    pinMode(leds[i].pin, OUTPUT);
    setBrightness(&leds[i], int(PWMRANGE/16));

  }

  //By default, turn off the LED on port A, since the rotation will start there
  setBrightness(&leds[0], PWMRANGE);

  //Set the last handled to now so that the rotation will begin immediately
  blinkLastHandled = millis();

  //Set the default IP address, gateway, and subnet to use when in AP mode
  IPAddress ip(192, 168, 1, 1);
  IPAddress gateway(192, 168, 1, 1);
  IPAddress subnet(255, 255, 255, 0);

  WiFi.softAPConfig(ip, gateway, subnet);

  //Configure the device access point to be "FireFly-" + MAC address
  WiFi.softAP(("FireFly-" + settings.deviceName).c_str());

  //Start spiffs FS, where the html files are stored
  SPIFFS.begin();

  //Setup the web server to handle the POST request to the /bootstrap URL
  webServer.on(F("/bootstrap"), HTTP_POST, wwwHandleSubmit);

 //Setup the webserver to handle all requests
  webServer.onNotFound([]() {

    //Determine the method handler
    switch(webServer.method()){
      
      case HTTP_GET:
      
        //We only allow GET requests
        handleWebGet(webServer.uri());
        
        break;

      default:

        //We don't know how to handle this method, return 400
        webServer.send(400);
        
        break;
    }

  });

  //Set the MDNS name on the broadcast  
  MDNS.begin(settings.deviceName.c_str());

  //Allow uploads of firmware through the web server
  httpUpdater.setup(&webServer);

  MDNS.addService("http", "tcp", 80);

  //Start up the web server
  webServer.begin();

}


void loopProvisioningMode(){

  //Setup a client handler
  webServer.handleClient();

  rotateLEDs();
  
}


void loopNormalMode(){

  //The device has been provisioned, run the normal processes
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }

  mqttClient.loop();

  //Blink the LEDs that need to be blinkLEDs if it's time to do so
  if ((millis() - blinkLastHandled) > LED_BLINK_MS) {
    blinkLEDs();
  }

  //Only snore every 40th clock cycle
  if (millis() % 60 == 0) {
    //Snore the LEDs that need to be snored
    snoreLEDs();
  }

  //See if it is time to send an unsolicited status message
  if ((millis() - healthUpdateLastHandled) > HEALTH_UPDATE_EVERY_MS) {
    handleHealth();
  }
}
  

void setupLEDs(void) {

  /* Sets the pins and default brightness */

  for (int i = 0; i < countOfLEDs; i++) {

    //Set the pin for output
    pinMode(leds[i].pin, OUTPUT);

    setBrightness(&leds[i], F("DEFAULT"));

  }
}


void setBrightness(structLED *ptrLed, int brightness) {

  ptrLed->illumination = brightness;

  analogWrite(ptrLed->pin, ptrLed->illumination);
}


void setBrightness(structLED *ptrLed, String intensity) {

  //Clean up the string
  intensity.toUpperCase();
  intensity.trim();

  //Find the intensity requested
  /*for (int i = 0; i < ptrLed->countOfDefinedBrightness; i++) {

    if (ptrLed->definedBrightness[i].name == intensity) {

      ptrLed->illumination = calculateBrightness(ptrLed->definedBrightness[i].illumination);

    }
  }*/

  //Also consider binary ON (100) and OFF (0)
  if (intensity == "OFF") {
    ptrLed->illumination = calculateBrightness(0);
  }

  if (intensity == "ON") {
    ptrLed->illumination = calculateBrightness(100);
  }

  //Set the brightness, which will also do nothing if the intensity isn't defined
  analogWrite(ptrLed->pin, ptrLed->illumination);

}


void setBrightness(structLED *ptrLed, int illumination, bool isIllumination) {

  ptrLed->illumination = illumination;

  analogWrite(ptrLed->pin, ptrLed->illumination);

}


void destroyEEPROM(){

  for (unsigned short i = 0 ; i < EEPROM.length() ; i++) {
    EEPROM.write(i, 0);
  }

}


void EEPROMWrite(String stringToWrite, int startPosition) {
  /*
     This function writes a string of data to the EEPROM beginning at a specified address.  It is sandwiched between two non-printable ASCII characters (2/STX and 3/ETX)
     to deliniate where the data begins and ends, which is used when reading the data.
  */

  //Sandwich the data with STX and ETX so we can keep track of the data
  stringToWrite = char(02) + stringToWrite + char(03);

  //Set the EEPROM size
  EEPROM.begin(EEPROMLength);

  //Write a start of text and end of text around the actual text
  for (int i = 0; i < stringToWrite.length(); ++i)
  {
    //Write the data to the EEPROM buffer
    EEPROM.write((startPosition + i), stringToWrite[i]);
  }

  //Commit the EEPROM
  EEPROM.commit();

}


String EEPROMRead(int startPosition) {
  /*
     This function reads EEPROM data.  It is sandwiched between two non-printable ASCII characters (2/STX and 3/ETX) to deliniate where the data begins and ends.
     Becasuse the EEPROM length is longer than the data contained, we can only reliably read the data between the two non-printable characters, as there could be junk data left
     after the data string in the EEPROM.  After the end of the data feed is found, we automatically advance to the end to force the function to stop reading (we could have another
     STX later on that we don't want to pick up).
  */

  byte value;
  String strReturnValue = "";
  bool isProcessingData = false;

  EEPROM.begin(EEPROMLength);

  //Start reading the EEPROM where the data begins until the EEPROM length
  for (int i = startPosition; i < EEPROMLength; i++) {

    // read a byte from the current address of the EEPROM
    value = EEPROM.read(i);

    //See what the value of this byte is
    switch (value) {

      case 2:
        //This byte is the beginning of the data, start saving information at this point
        isProcessingData = true;
        break;

      case 3:
        //This byte is the end of the data, stop saving information at this point
        isProcessingData = false;

        //Set the address to the end of the EEPROM so we will break out of the WHILE loop
        i = EEPROMLength;
        break;

      default:

        //If the character is printable AND we are processing a word, store the character to the string
        if (value >= 32 && value <= 127 && isProcessingData == true) {
          strReturnValue = strReturnValue + String(char(value));
        }
    }
  }

  return strReturnValue;

}


void handleHealth() {

  //Publish the uptime
  publishMQTT(settings.mqttServer.clientTopic + "/uptime", String(millis()));

  //Publish the OK status
  publishMQTT(settings.mqttServer.clientTopic + "/status", F("ONLINE"));

  //Publish the IP Address
  publishMQTT(settings.mqttServer.clientTopic + "/ip", WiFi.localIP().toString());

  //Publish the Firmware version
  publishMQTT(settings.mqttServer.clientTopic + "/firmware", FIRMWARE_VERSION);

  //Publish the device name
  publishMQTT(settings.mqttServer.clientTopic + "/deviceName", settings.deviceName);

  //Update that we handled health
  healthUpdateLastHandled = millis();

}


String getContentType(String filename) {

  //Returns the content type for the given file
  if (filename.endsWith(F(".html"))){
    return F("text/html");
  }

  if (filename.endsWith(F(".css"))){
    return F("text/css");
  }

  if (filename.endsWith(F(".js"))){
    return F("application/javascript");
  } 

  if (filename.endsWith(F(".gz"))){
    return F("application/x-gzip");
  } 

  //Not a recognized format, assume plain text
  return F("text/plain");
}


void handleWebGet(String uri){

  //Return the device name if requested
  if(uri == "/api/deviceName"){
    webServer.send(200,F("application/json"),"{\"deviceName\":\"" + settings.deviceName + "\"}");
    return;
  }

  //Return the firmware version if requested
  if(uri == "/api/firmwareVersion"){
    webServer.send(200,F("application/json"),"{\"firmwareVersion\":\"" + (String)FIRMWARE_VERSION + "\"}");
    return;
  }

  //If not file is requested, send index.html
  if (uri.endsWith("/")) uri += "index.html";

  String contentType = getContentType(uri);

  //See if there is a gz version
  if(SPIFFS.exists(uri + ".gz")) {
    uri = uri + ".gz";
  }
  
  //Attempt to get the file from SPIFFS
  if (SPIFFS.exists(uri)) {

    //Read the data from the file and send it to the web client
    File file = SPIFFS.open(uri, "r");
    size_t sent = webServer.streamFile(file, contentType);

    //Close the file
    file.close();

  }else{

    //Not found
    webServer.send(404); 

  }
}


void wwwHandleSubmit() {

  //Make sure we have all of the arguments we need
  if(webServer.hasArg("ssid") == false || webServer.hasArg("wpaKey") == false || webServer.hasArg("bootstrapURL") == false){

    //There was an error on the posting form that not all the fields were sent
    webServer.sendHeader(F("Connection"), F("close"));
    webServer.sendHeader(F("Access-Control-Allow-Origin"), "*");
    webServer.send(406, F("text/plain"), F("Insufficient parameters\r\n"));

    return;

  }

  //Clean up the input
  String SSID = webServer.arg("ssid");
  String wpaKey = webServer.arg("wpaKey");
  String bootstrapURL = webServer.arg("bootstrapURL");

  //Trim each of the inputs
  SSID.trim();
  wpaKey.trim();
  bootstrapURL.trim();

  if(SSID.length() == 0 || wpaKey.length() == 0 || bootstrapURL.length() == 0){

    //One or more fields were empty
    webServer.sendHeader(F("Connection"), F("close"));
    webServer.sendHeader(F("Access-Control-Allow-Origin"), "*");
    webServer.send(406, F("text/plain"), F("Empty parameters\r\n"));

    return;

  }

  webServer.sendHeader(F("Connection"), F("close"));
  webServer.sendHeader(F("Access-Control-Allow-Origin"), "*");
  webServer.send(200, F("text/plain"), F("OK\r\n"));
  webServer.close();

  //Give the system time to send the data back to the caller before attempting to provision
  delay(1000);

  //Attempt to bootstrap
  attemptProvision(SSID, wpaKey, bootstrapURL);

}


boolean retrieveBootstrap(String bootstrapURL) {

  Serial.println("Attempting to retrieve bootstrap");

  HTTPClient httpClient;

  httpClient.begin(bootstrapURL);

  //See if we were successful in retrieving the bootstrap
  if (httpClient.GET() == HTTP_CODE_OK) {

      Serial.println(httpClient.getString());

    //Write the response to the EEPROM
    //EEPROMWrite(httpClient.getString(), EEPROMDataBegin);

    //Set the provision status
    //EEPROMWrite("true", 0);

    httpClient.end();

    //Re-Read the EEPROM into RAM
    //readEEPROMToRAM();

    //return true;
    return false;

  } else {

    Serial.println("Server returned " + httpClient.GET());

    return false;
  }

}

void attemptProvision(String SSID, String wpaKey, String bootstrapURL) {

  Serial.println("Attempting provision\nSSID: [" + SSID + "]\nwpaKey: [" + wpaKey + "]\nBootstrap URL: [" + bootstrapURL + "]");

  //Kill the soft AP
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  delay(500);

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(SSID.c_str(), wpaKey.c_str());
  }

  //Remember the start time
  unsigned long timeStartConnect = millis();

  //Print output while waiting for WiFi to connect
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);

    //Set a timer for 30 seconds to connect
    if (millis() - timeStartConnect > 30000) {

      //Disconnect the WiFi
      WiFi.disconnect();

      //Restart the provisioning server
      setupProvisioningMode();

      Serial.println("Unable to attach to WiFi");

      //Exit this
      return;

    }
  }

  if (retrieveBootstrap(bootstrapURL) == true) {

    turnOffAllLEDs();

    //Ensure the EEPROM has time to finish writing and closing the client
    delay(1000);

    //Restart
    ESP.restart();

  } else {

    //Restart on failure
    ESP.restart();

  }

}


void turnOffAllLEDs() {

  //Turn off the LEDs
  for (int i = 0; i < countOfLEDs; i++) {

    //Remember the current brightness setting, except blinking which could be temporarily off
    if (leds[i].style != STYLE_BLINK) {

      leds[i].styleData = leds[i].illumination;

    }
    //Turn off the LED
    setBrightness(&leds[i], "OFF");
  }

}


void restoreAllLEDs() {

  //Turn off the LEDs
  for (int i = 0; i < countOfLEDs; i++) {

    //Set the illumination to the previous value
    setBrightness(&leds[i], leds[i].styleData);
  }

}


void checkFirmwareUpgrade(String url) {

  //Create an http client to connect to the firmware server
  HTTPClient httpClient;
  httpClient.begin(url);

  int httpCode = httpClient.GET();

  //See if we get an HTTP 200/OK response
  if ( httpCode == 200 ) {

    String jsonServerResponse = httpClient.getString();

    //Create a jsonDocument object to deserialize into
    StaticJsonDocument<EEPROMLength> jsonDoc;

    //Deserialize the response
    auto jsonParseError = deserializeJson(jsonDoc, jsonServerResponse);

    if (jsonParseError) {

      return;
    }

    //Get the firmware version
    String newFirmwareVersion = jsonDoc["version"];
    String firmwareBinURL = jsonDoc["url"];

    //Check if the version requested is not the same as the one loaded (yes, we allow back flashing)
    if ( (String)newFirmwareVersion != FIRMWARE_VERSION ) {
      publishMQTT(settings.mqttServer.clientTopic + "/firmwareUpdate", F("Updating"));
      publishMQTT(settings.mqttServer.clientTopic + "/status", F("Updating Firmware"));

      //Attempt to retrieve the firmware and perform the update
      t_httpUpdate_return firmwareUpdateResponse = ESPhttpUpdate.update(firmwareBinURL);

      switch (firmwareUpdateResponse) {
        case HTTP_UPDATE_FAILED:
          publishMQTT(settings.mqttServer.clientTopic + "/firmwareUpdate", F("Failed"));
          publishMQTT(settings.mqttServer.clientTopic + "/status", F("Firmware Update Failed"));
          publishMQTT(settings.mqttServer.clientTopic + "/firmware", FIRMWARE_VERSION);
          break;

        case HTTP_UPDATE_NO_UPDATES:
          break;
      }
    }
    else {
      publishMQTT(settings.mqttServer.clientTopic + "/firmware", FIRMWARE_VERSION);
    }
  }
  else {
    publishMQTT(settings.mqttServer.clientTopic + "/firmwareUpdate", F("Failed"));
    publishMQTT(settings.mqttServer.clientTopic + "/status", F("Firmware Update Failed"));
    publishMQTT(settings.mqttServer.clientTopic + "/firmware", FIRMWARE_VERSION);
  }

  httpClient.end();
}


int enumPorts(String portName) {

  //Remove case sensitivity
  portName.toUpperCase();

  if (portName == "A") {
    return PORT_A;
  };

  if (portName == "B") {
    return PORT_B;
  };

  if (portName == "C") {
    return PORT_C;
  };

  if (portName == "D") {
    return PORT_D;
  };

  if (portName == "E") {
    return PORT_E;
  };

  if (portName == "F") {
    return PORT_F;
  };

  //Default
  return 0;
}


char* enumPorts(int port) {

  switch (port) {

    case PORT_A:
      return "A";
      break;
    case PORT_B:
      return "B";
      break;
    case PORT_C:
      return "C";
      break;
    case PORT_D:
      return "D";
      break;
    case PORT_E:
      return "E";
      break;
    case PORT_F:
      return "F";
      break;
    default:
      return "UNKNOWN";
      break;
  }
}