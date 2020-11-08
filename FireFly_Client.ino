#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <FS.h>   // Include the SPIFFS library, download from https://github.com/esp8266/arduino-esp8266fs-plugin
#include "Structures.h"

const char* FIRMWARE_VERSION = "1.0";
const char* PRODUCT_NAME = "FireFly Switch";

/* These are defined by the physical layout of the board by GPIO Number */
#define PORT_A 5
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

#define EEPROM_SIZE 4096 //Number of bytes to use on the EEPROM
#define EEPROM_DATA_START 6 //Position where the actual EEPROM data begins; Prior to this is used to store provisioning data
#define HEALTH_UPDATE_EVERY_MS 1680000 //Every 28 minutes
#define LED_BLINK_MS 750
#define LED_COUNT_MAXIMUM 6 //Board maximum

unsigned long healthUpdateLastHandled = 0;
unsigned long blinkLastHandled = 0;

struct structSettings settings;

struct structLED leds[LED_COUNT_MAXIMUM]; //Board Maximum
int countOfLEDs = 0; //Essentially, the length of leds[].

WiFiClient espClient; //WiFi client on the ESP8266
PubSubClient mqttClient(espClient); //MQTT client binding to the ESP8266 WiFi
ESP8266WebServer webServer(80); //Web server client to handle provisioning
ESP8266HTTPUpdateServer httpUpdater; //HTTP server client to handle OTA updates

String errorMessage = "";

/******************************************* Setup and Loops *******************************************/


void setup() {

  EEPROM.begin(EEPROM_SIZE);

  //Set pin FACTORY_RESET to pullup
  pinMode(FACTORY_RESET, INPUT_PULLUP);

  //Determine if the FACTORY_RESET has been pulled low, if so, blow away the EEPROM
  if (digitalRead(FACTORY_RESET) == LOW) {

    //Destroy the EEPROM
    destroyEEPROM();

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

  /*
    This version of setup will be run when the device has previously been provisioned.
  */

  //Read the data from EEPROM
  readNetworkFromEEPROM();
  readLEDsFromEEPROM();

  //Attempt to connect to the WiFi Network as a client
  WiFi.mode(WIFI_STA);

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(settings.network.ssidName.c_str(), settings.network.wpaKey.c_str());
  }

  //Print output while waiting for WiFi to connect
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
  }

  //Configure the mqtt Client
  mqttClient.setServer(settings.mqttServer.serverName.c_str(), settings.mqttServer.port);
  mqttClient.setCallback(mqttCallback);

  //Setup the LEDs for use
  setupLEDs();
  
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


void loopNormalMode(){
  /*
    This version of the loop will be called when the device has previously been provisioned.
  */

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
  

void loopProvisioningMode(){

  /*
    This version of the loop handles web server requests and causes the LED's to rotate in sequential order, indcating the device needs provisioning.
  */

  //Setup a client handler
  webServer.handleClient();

  rotateLEDs();
  
}


/******************************************* LED Modes *******************************************/

void setupLEDs(void) {

  /* 
    Sets the pins and default brightness
  */

  for (int i = 0; i < countOfLEDs; i++) {

    //Set the pin for output
    pinMode(leds[i].pin, OUTPUT);

    setBrightness(&leds[i], F("DEFAULT"));

  }
}


void blinkLEDs() {

  /*
    Global function that blinks any LED's that have been set to blink.
  */

  //Find any LED's that should be blinking
  for (int i = 0; i < countOfLEDs; i++) {

    if (leds[i].style == STYLE_BLINK) {

      if (leds[i].brightness == 0) {

        //Turn on the LED to the pre-last value
        setBrightness(&leds[i], leds[i].styleData);

      } else {

        //Turn off the LED
        setBrightness(&leds[i], F("OFF"));

      }
    }
  }

  //Set the blink handler
  blinkLastHandled = millis();
}


void snoreLEDs() {

  /*
    Global function that snores any LED's that have been set to snore.
  */

  //Find any LED's that should be snoring
  for (int i = 0; i < countOfLEDs; i++) {

    if (leds[i].style == STYLE_SNORE) {

      //Determine if we are increasing (not zero) or decreasing (zero)
      if (leds[i].styleData != 0) {

        //Increment the brightness
        leds[i].brightness++;

        //If we are now greater than PWMRANGE, switch the direction for the next iteration
        if (leds[i].brightness > PWMRANGE) {
          leds[i].styleData = 0;
        }

        //Set the new value
        setBrightness(&leds[i], leds[i].brightness);

      } else {

        //Decrement the brightness
        leds[i].brightness--;

        //If we are now less than 10, switch the direction for the next iteration; A value of less than 10 makes the LED flash off, so count it as 0.
        if (leds[i].brightness < 10) {
          leds[i].styleData = 1;
        }

        //Set the new brightness value
        setBrightness(&leds[i], leds[i].brightness);

      }
    }
  }
}


void rotateLEDs(){

  /*
    A global function that rotates between each LED in the count and causes it to flash in sequential order, on or off.
  */

  //Only run this function every 1 second
  if(millis() - blinkLastHandled < 200){
    return;
  }

  //Find any LED's that should be on
  for (int i = 0; i < countOfLEDs; i++) {

    if(leds[i].brightness == PWMRANGE){

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


void setBrightness(structLED *ptrLed, int brightness) {

  /*
    Sets the LED's brightness to a given brightness number
  */

  ptrLed->brightness = brightness;

  analogWrite(ptrLed->pin, ptrLed->brightness);
}


void setBrightness(structLED *ptrLed, String intensity) {

  /*
    Sets the LED's brightness to a given named intensity
  */

  //Clean up the string
  intensity.toUpperCase();
  intensity.trim();

  //Find the intensity requested, if it exists
  for (int i = 0; i < ptrLed->countOfIntensities; i++) {

    if (ptrLed->intensities[i].name == intensity) {

      ptrLed->brightness = calculateBrightness(ptrLed->intensities[i].brightness);

    }
  }

  //Also consider binary ON (100) and OFF (0)
  if (intensity == F("OFF")) {
    ptrLed->brightness = calculateBrightness(0);
  }

  if (intensity == F("ON")) {
    ptrLed->brightness = calculateBrightness(100);
  }

  //Set the brightness, which will also do nothing if the intensity isn't defined
  analogWrite(ptrLed->pin, ptrLed->brightness);

}


void turnOffAllLEDs() {

  /*
    Turns off all LED's
  */

  //Turn off the LEDs
  for (int i = 0; i < countOfLEDs; i++) {

    //Remember the current brightness setting, except blinking which could be temporarily off
    if (leds[i].style != STYLE_BLINK) {

      leds[i].styleData = leds[i].brightness;

    }
    //Turn off the LED
    setBrightness(&leds[i], F("OFF"));
  }

}


void restoreAllLEDs() {

  /*
    Restores all LED's to their previous brightness level.
  */

  //Turn off the LEDs
  for (int i = 0; i < countOfLEDs; i++) {

    //Set the brightness to the previous value
    setBrightness(&leds[i], leds[i].styleData);
  }

}


/******************************************* MQTT *******************************************/

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  /*
    Handles all incoming requests for subscribed topics
  */

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


void handleMQTTMessageReceived(String topic, String payload) {
  /*
    Handles incoming messages from MQTT.  Payloads must exactly match.
  */

  //See if the payload requests a restart
  if (topic == settings.mqttServer.clientTopic + "/restart" && payload != "") {

    turnOffAllLEDs();

    publishMQTT(settings.mqttServer.clientTopic + "/status", F("RESTARTING"));

    delay(500);

    ESP.restart();

    return;

  }

  //See if the payload requests a health check
  if (topic == settings.mqttServer.clientTopic + "/health/get") {

    handleHealth();

    return;
  }

  //See if the payload requests a firmware update check
  if (topic == settings.mqttServer.clientTopic + "/firmware/set") {

    checkFirmwareUpgrade(payload);

    return;
  }

  //See if the payload requests a bootstrap update check
  if (topic == settings.mqttServer.clientTopic + "/bootstrap/set") {

    publishMQTT(settings.mqttServer.clientTopic + "/status", F("BOOTSTRAPPING"));

    if(retrieveBootstrap(payload) == true){

      //Publish that the bootstrap failed
      publishMQTT(settings.mqttServer.clientTopic + "/status", F("BOOTSTRAP_FAILED"));
      publishMQTT(settings.mqttServer.clientTopic + "/errorMessage", errorMessage);

    }else{

      //Success, restart to pick up changes
      turnOffAllLEDs();

      publishMQTT(settings.mqttServer.clientTopic + "/status", F("RESTARTING"));

      delay(500);

      ESP.restart();

    }

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


void publishMQTT(String topic, String payload) {
  /*
     Sends the requested payload to the requested topic.
  */

  //Clean the strings
  topic.trim();
  payload.trim();

  mqttClient.publish(String(topic).c_str(), String(payload).c_str());

}


void handleEventTopic(String topic, String payload) {

  String messageActor;

  messageActor = topic.substring(topic.lastIndexOf("/") + 1);

  //Find the button
  for (int i = 0; i < countOfLEDs; i++) {

    if (leds[i].name == messageActor) {

      //See if this is a button push
      if (payload == F("ON")) {

        //Remember the current brightness value
        leds[i].styleData = leds[i].brightness;

        //Set the brightness to OFF while the button is being pushed
        setBrightness(&leds[i], F("OFF"));
      }

      //See if this is a button release
      if (payload == F("OFF")) {

        //Button has been released, turn the LED back on to its previous level if we know it, otherwise use the default
        if (leds[i].styleData != 0) {

          //Restore the previous value
          setBrightness(&leds[i], leds[i].styleData);

        } else {

          //Use the default
          setBrightness(&leds[i], F("DEFAULT"));

        }
      }

      //See if the controller is telling us the button has been pressed too many times
      if (payload == F("MINIMUM") || payload == F("MAXIMUM")) {

        //Remember the current brightness value
        leds[i].styleData = leds[i].brightness;

        //Flash the LED off then to maximum brightness three times quickly
        for(int flashCount = 0; flashCount < 3; flashCount++){

          setBrightness(&leds[i], F("OFF"));

          delay(100);

          //Turn the LED to maximum brightness briefly
          setBrightness(&leds[i], F("MAXIMUM"));

          delay(100);

        }
      }
    }
  }
}


void handleGlobalEventTopic(String topic, String payload) {
  /*
    Handles all of the business logic
  */

  //Supports 3 values up to 32 characters https://arduinojson.org/v6/assistant/
  const size_t capacity = JSON_OBJECT_SIZE(3) + 130;

  //Create a jsonDocument object to deserialize into
  DynamicJsonDocument jsonDoc(capacity);

  //Deserialize the response
  DeserializationError jsonError = deserializeJson(jsonDoc, payload);

  //Exit on parse error
  if (jsonError) {

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

          //Remember the current brightness value
          leds[i].styleData = leds[i].brightness;

          //Set the brightness to OFF
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

  if (messageType == "SWITCH_NAMED") {

    //Ensure this is the requested switch
    if(messageActor != settings.name){
      return;
    }

    //Iterate each button
    for (int i = 0; i < countOfLEDs; i++) {

        //Ensure the LED style is normal
        if (leds[i].style != STYLE_NORMAL) {
          leds[i].style = STYLE_NORMAL;
        }

        //Remember the current brightness value
        leds[i].styleData = leds[i].brightness;

        //Set the brightness to the named value
        setBrightness(&leds[i], (String)messageValue);
    }
  }

  if (messageType == "BUTTON_NUMERIC") {

    //Find the button
    for (int i = 0; i < countOfLEDs; i++) {

      if (leds[i].name == messageActor) {

        //Ensure the LED style is normal
        if (leds[i].style != STYLE_NORMAL) {
          leds[i].style = STYLE_NORMAL;
        }

        //Set the brightness to the numeric value
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

        //Ensure the LED style is normal
        if (leds[i].style != STYLE_NORMAL) {
          leds[i].style = STYLE_NORMAL;
        }

        //Set the brightness to the numeric value
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
          leds[i].styleData = leds[i].brightness;
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

        //Set the brightness to the numeric value
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

        //Ensure the LED style is normal
        if (leds[i].style != STYLE_NORMAL) {
          leds[i].style = STYLE_NORMAL;
        }

        //Set the brightness to the numeric value
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
          leds[i].styleData = leds[i].brightness;

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

      //Ensure the LED style is normal
      if (leds[i].style != STYLE_NORMAL) {
        leds[i].style = STYLE_NORMAL;
      }

      //Set the brightness to the numeric value
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

      //Ensure the LED style is normal
      if (leds[i].style != STYLE_NORMAL) {
        leds[i].style = STYLE_NORMAL;
      }

      //Set the brightness to the numeric value
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
        leds[i].styleData = leds[i].brightness;

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

  String statusTopic = settings.mqttServer.clientTopic + "/status";

  // Loop until we're reconnected
  while (!mqttClient.connected()) {

    // Attempt to connect
    if (mqttClient.connect(settings.deviceName.c_str(), settings.mqttServer.username.c_str(), settings.mqttServer.password.c_str(), statusTopic.c_str(), 0, true, "OFFLINE")) {

      restoreAllLEDs();

      // Publish an announcement on the client topic
      publishMQTT(statusTopic, F("STARTED"));

      //Send the health event
      handleHealth();

      //Create a subscription for the various management actions we can take
      createSubscription(settings.mqttServer.clientTopic + "/restart");
      createSubscription(settings.mqttServer.clientTopic + "/health/get");
      createSubscription(settings.mqttServer.clientTopic + "/firmware/set");
      createSubscription(settings.mqttServer.clientTopic + "/bootstrap/set");

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
  /*
    A wrapper for the mqtt subscribe method
  */

  //Remove any duplicated slashes
  topic.replace("//", "/");

  //Make sure we didn't end our subscription with a trailing /
  if (topic.substring(topic.length() - 1) == "/") {

    topic = topic.substring(0, topic.length() - 2);

  }

  //Subscribe to the topic
  mqttClient.subscribe(topic.c_str());

}


void handleHealth() {

  //Publish the uptime
  publishMQTT(settings.mqttServer.clientTopic + "/uptime", String(millis()));

  //Publish the OK status
  publishMQTT(settings.mqttServer.clientTopic + "/status", F("ONLINE"));
  publishMQTT(settings.mqttServer.clientTopic + "/errorMessage", "");

  //Publish the IP Address
  publishMQTT(settings.mqttServer.clientTopic + "/ip", WiFi.localIP().toString());

  //Publish the Firmware version
  publishMQTT(settings.mqttServer.clientTopic + "/firmware", (String)FIRMWARE_VERSION);

  //Publish the device name
  publishMQTT(settings.mqttServer.clientTopic + "/deviceName", settings.deviceName);

  //Publish the device name
  publishMQTT(settings.mqttServer.clientTopic + "/name", settings.name);

  //Update that we handled health
  healthUpdateLastHandled = millis();

}


/******************************************* Web *******************************************/

String getContentType(String filename) {
  /*
    Gets the file content type based on filename
  */

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

  /*
    Handles known http GET requests, returns 404 for all others
  */

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

  //Return any error messages version if requested
  if(uri == "/api/errorMessage"){
    webServer.send(200,F("application/json"),"{\"errorMessage\":\"" + errorMessage + "\"}");
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
  /*
    Handles submission of provisioning forms
  */

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

  //Attempt to provision and bootstrap
  attemptProvision(SSID, wpaKey, bootstrapURL);

}


/******************************************* Bootstrapping *******************************************/

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


void attemptProvision(String SSID, String wpaKey, String url) {
  /*
    Attempts to connect to the specified WiFi network and retrieve a bootstrap from the specified URL
  */

  //Turn off all of the LED's
  turnOffAllLEDs();

  //Kill the soft AP
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true);

  //Set the WiFi to station mode
  WiFi.mode(WIFI_STA);

  WiFi.begin(SSID.c_str(), wpaKey.c_str());

  //Remember the start time so we can have a timeout
  unsigned long timeStartConnect = millis();

  //Print output while waiting for WiFi to connect
  while (WiFi.status() != WL_CONNECTED) {
    
    delay(200);

    //Set a timer for 30 seconds as the timeout
    if (millis() - timeStartConnect > 30000) {
      
      //Disconnect the WiFi
      WiFi.disconnect();

      //Set an error message
      errorMessage = F("Unable to attach to WiFi");

      //Restart provisioning mode
      setupProvisioningMode();

      //Exit this
      return;

    }
  }

  if (retrieveBootstrap(url) == true) {

    //Succcess, Restart to get the configuration from EEPROM properly
    ESP.restart();

  } else {

    //Something didn't go right, restart provisioning mode
    setupProvisioningMode();

    //Exit this
    return;

  }

}


boolean retrieveBootstrap(String url) {
  /*
    Retrieves a bootstrap from the given URL and parses it to JSON.  If successful, it will store the data to the EEPROM.
  */

  HTTPClient httpClient;

  //Supports names of 20 characters, average size of other settings at 32 characters https://arduinojson.org/v6/assistant/
  const size_t capacity = 6*JSON_ARRAY_SIZE(4) + JSON_ARRAY_SIZE(6) + 32*JSON_OBJECT_SIZE(2) + 6*JSON_OBJECT_SIZE(3) + JSON_OBJECT_SIZE(4) + JSON_OBJECT_SIZE(5) + 1290;

  //Attempt to connect to the URL
  httpClient.begin(url);

  int responseCode = httpClient.GET();

  //See if we get an HTTP 200/OK response
  if (responseCode == 200 ) {

    //Create a JSON doc to use
    DynamicJsonDocument jsonDoc(capacity);

    // Parse JSON object
    DeserializationError jsonError = deserializeJson(jsonDoc, httpClient.getStream());

    if (jsonError) {
      httpClient.end();
      errorMessage = "Error parsing bootstrap: " + (String)jsonError.c_str();
      return false;
    }

    httpClient.end();

    //Success
    errorMessage = "";

    //Populate the settings from the bootstrap
    bootstrap(jsonDoc);

    //Write the settings to the EEPROM
    writeNetworkToEEPROM();

    //Write the LED's to the EEPROM
    writeLEDsToEEPROM();

    return true;

  }else{

    errorMessage = "Bootstrap server response code was unexpected (" + (String)responseCode + ")";

    //Kill the client, we didn't get 200 back
    httpClient.end();
    return false;
  }

}


void bootstrap(const DynamicJsonDocument& jsonDoc){

  //Set the network data
  settings.name = jsonDoc["name"].as<String>();
  settings.network.ssidName = jsonDoc["network"]["ssid"].as<String>();
  settings.network.wpaKey = jsonDoc["network"]["key"].as<String>();

  //Set the MQTT data
  settings.mqttServer.serverName = jsonDoc["mqtt"]["serverName"].as<String>();
  settings.mqttServer.port = jsonDoc["mqtt"]["port"].as<int>();
  settings.mqttServer.username = jsonDoc["mqtt"]["username"].as<String>();
  settings.mqttServer.password = jsonDoc["mqtt"]["password"].as<String>();
  settings.mqttServer.clientTopic = jsonDoc["mqtt"]["topics"]["client"].as<String>();
  settings.mqttServer.eventTopic = jsonDoc["mqtt"]["topics"]["event"].as<String>();

  //Replace the placeholder values, if they exist
  settings.mqttServer.username.replace("$DEVICENAME$", settings.deviceName);
  settings.mqttServer.password.replace("$DEVICENAME$", settings.deviceName);
  settings.mqttServer.clientTopic.replace("$DEVICENAME$", settings.deviceName);
  settings.mqttServer.eventTopic.replace("$DEVICENAME$", settings.deviceName);

  //Destroy the list of LED's
  countOfLEDs = jsonDoc["buttons"].size();

  //Cycle through each LED in the JSON doc
  for(int i = 0; i < countOfLEDs; i++){

    leds[i].pin = enumPorts(jsonDoc["buttons"][i]["port"].as<String>());
    leds[i].name = jsonDoc["buttons"][i]["name"].as<String>();
    leds[i].color = jsonDoc["buttons"][i]["led"]["color"].as<String>();
    leds[i].countOfIntensities = jsonDoc["buttons"][i]["led"]["intensities"].size();

    //Get each intensity from the JSON doc
    for(int j=0; j < leds[i].countOfIntensities; j++){
      leds[i].intensities[j].name = jsonDoc["buttons"][i]["led"]["intensities"][j]["name"].as<String>();
      leds[i].intensities[j].brightness = jsonDoc["buttons"][i]["led"]["intensities"][j]["brightness"].as<int>();
    }
  }
}


/******************************************* EEPROM *******************************************/

void destroyEEPROM(){

  /*
    Clears all data in the EEPROM and resets the provisioning.
  */

  for (unsigned short i = 0 ; i < EEPROM.length() ; i++) {

    EEPROM.write(i, 0);
  }

  EEPROM.commit();

}


void writeNetworkToEEPROM(){
  /* 
    Writes the settings out to the EEPROM, which starts at position EEPROM_DATA_START.
  */

  //Create temporary structures
  struct _structSettings _settings;

  strcpy(_settings.name, settings.name.substring(0, 20).c_str());

  strcpy(_settings.network.ssidName, settings.network.ssidName.substring(0, 32).c_str());
  strcpy(_settings.network.wpaKey, settings.network.wpaKey.substring(0, 32).c_str());

  strcpy(_settings.mqttServer.serverName, settings.mqttServer.serverName.substring(0, 32).c_str());
  _settings.mqttServer.port = settings.mqttServer.port;
  strcpy(_settings.mqttServer.username, settings.mqttServer.username.substring(0, 32).c_str());
  strcpy(_settings.mqttServer.password, settings.mqttServer.password.substring(0, 32).c_str());
  strcpy(_settings.mqttServer.clientTopic, settings.mqttServer.clientTopic.substring(0, 32).c_str());
  strcpy(_settings.mqttServer.eventTopic, settings.mqttServer.eventTopic.substring(0, 32).c_str());

  //Put the data in the EEPROM
  EEPROM.put(EEPROM_DATA_START, _settings);

  //Set the device as provisioned
  EEPROM.put(0, "TRUE");

  EEPROM.commit();

}


void readNetworkFromEEPROM(){
  /* 
    Reads the settings from EEPROM, which starts at position EEPROM_DATA_START.
  */

  struct _structSettings _settings;

  //Get the data from the EEPROM
  EEPROM.get(EEPROM_DATA_START, _settings);

  settings.name = _settings.name;

  settings.network.ssidName = _settings.network.ssidName;
  settings.network.wpaKey = _settings.network.wpaKey;

  settings.mqttServer.serverName = _settings.mqttServer.serverName;
  settings.mqttServer.port = _settings.mqttServer.port;
  settings.mqttServer.username = _settings.mqttServer.username;
  settings.mqttServer.password = _settings.mqttServer.password;
  settings.mqttServer.clientTopic = _settings.mqttServer.clientTopic;
  settings.mqttServer.eventTopic = _settings.mqttServer.eventTopic;

}


void writeLEDsToEEPROM(){
    /* 
      Writes the LEDs to the EEPROM, which starts at position after the network.
    */

    unsigned int eepromPosition = sizeof(_structSettings) + EEPROM_DATA_START;

    //Write the LED count
    EEPROM.put(eepromPosition, countOfLEDs);

    //Set the new EEPROM position to after the count of LED's
    eepromPosition += sizeof(countOfLEDs);

    //Create an array of temporary LED objects
    struct _structLED _tmpLED[LED_COUNT_MAXIMUM];

    //Build the temporary array data so it can be written to the EEPROM in a block
    for(unsigned int i = 0; i < countOfLEDs; i++){

      //Build the final object with the data from the EEPROM
      _tmpLED[i].pin = leds[i].pin;
      strcpy(_tmpLED[i].name, leds[i].name.substring(0, 20).c_str());
      strcpy(_tmpLED[i].color, leds[i].color.substring(0, 20).c_str());
      _tmpLED[i].countOfIntensities = leds[i].countOfIntensities;

      //Add each intensity
      for(unsigned int j = 0; j < leds[i].countOfIntensities; j++){
        strcpy(_tmpLED[i].intensities[j].name, leds[i].intensities[j].name.substring(0, 20).c_str());
        _tmpLED[i].intensities[j].brightness = leds[i].intensities[j].brightness;
      }
    }

    //Write the array of temp LED's to the EEPROM
    EEPROM.put(eepromPosition, _tmpLED);

    EEPROM.commit();

}


void readLEDsFromEEPROM(){
  /* 
    Reads the LEDs from the EEPROM, which starts at position after the network.
  */

  unsigned int eepromPosition = sizeof(_structSettings) + EEPROM_DATA_START;

  //Get the LED count
  EEPROM.get(eepromPosition, countOfLEDs);

  //Set the new EEPROM position to after the count of LED's
  eepromPosition += sizeof(countOfLEDs);

  //Create an array of temporary LED objects
  struct _structLED _tmpLED[LED_COUNT_MAXIMUM];

  //Read the temporary object into RAM
  EEPROM.get(eepromPosition, _tmpLED);

  //Iterate each temporary object and move it to the working object
  for(unsigned int i = 0; i < countOfLEDs; i++){

    //Build the final object with the data from the EEPROM
    leds[i].pin = _tmpLED[i].pin;
    leds[i].name = _tmpLED[i].name;
    leds[i].color = _tmpLED[i].color;
    leds[i].countOfIntensities = _tmpLED[i].countOfIntensities;

    //Add each intensity
    for(unsigned int j = 0; j < leds[i].countOfIntensities; j++){
      leds[i].intensities[j].name = _tmpLED[i].intensities[j].name;
      leds[i].intensities[j].brightness = _tmpLED[i].intensities[j].brightness;
    }
  }
}


/******************************************* Helpers *******************************************/

int enumPorts(String portName) {
  /*
    Converts a port string to a port number.
  */

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
  /*
    Converts a port number to a port string.
  */

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


int calculateBrightness(int brightnessPercentage) {
  /*
    Calculates the PMW value based on the requested brightness percentage provided.
  */

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


/******************************************* Firmware *******************************************/

void checkFirmwareUpgrade(String url) {
  /*
    Retrieves a new firmware from the given URL
  */

  publishMQTT(settings.mqttServer.clientTopic + "/status", F("UPGRADING"));

  //Attempt to retrieve the firmware and perform the update
  t_httpUpdate_return firmwareUpdateResponse = ESPhttpUpdate.update(url);

  switch (firmwareUpdateResponse) {
    case HTTP_UPDATE_FAILED:
      publishMQTT(settings.mqttServer.clientTopic + "/status", F("UPDATE_FAILED"));
      break;

    case HTTP_UPDATE_NO_UPDATES:
      publishMQTT(settings.mqttServer.clientTopic + "/status", F("ONLINE"));
      publishMQTT(settings.mqttServer.clientTopic + "/errorMessage", "");
      break;
  }
}
