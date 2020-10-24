/*******************************************************************************/
/*        Structures used within this program in section below                 */
/*******************************************************************************/

typedef struct structSettings {
  bool deviceIsProvisioned;
  String friendlyName;
  String deviceName;

  struct structNetwork {
    String machineMacAddress;
    String ssidName;
    String wpaKey;
  } network;

  struct structMqttServer {
    String serverName;
    int port;
    String username;
    String password;
    String controlTopic;
    String clientTopic;
    String eventTopic;
  } mqttServer;

  struct structFirmware {
    String url;
    int version;
    unsigned long refreshMilliseconds;
    unsigned long lastHandled;
  } firmware;

  struct structBootstrap {
    String url;
    int version;
    unsigned long refreshMilliseconds;
    unsigned long lastHandled;
  } bootstrap;

};

typedef struct structLED {
  int pin;
  char name[24];
  char color[20];
  int illumination;
  int style;
  int styleData;
  int countOfDefinedBrightness;

  struct structBrightness {
    char name[20];
    int illumination;
  } definedBrightness[8];

};

/*******************************************************************************/
/*        Simplified structures used for EEPROM I/O in section below           */
/*******************************************************************************/

typedef struct _structSettings{
    char friendlyName[33]; //Max length: 32
    char deviceName[17]; //Max length: 16

    struct _structNetwork{
      byte macAddress[6];
      IPAddress ip;
      IPAddress dns;
      IPAddress gateway;
      IPAddress subnet;
    } network;

    struct _structMqttServer{
      char serverName[33]; //Max length: 32
      unsigned int port;
      char username[33]; //Max length: 32
      char password[33]; //Max length: 32
      char clientTopic[33]; //Max length: 32
      char controlTopic[33]; //Max length: 32
      char eventTopic[33]; //Max length: 32
    } mqttServer;

};

