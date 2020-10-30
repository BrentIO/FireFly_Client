const unsigned int MAXIMUM_DEFINED_BRIGHTNESS = 8;

/*******************************************************************************/
/*        Structures used within this program in section below                 */
/*******************************************************************************/

typedef struct structSettings {
  bool deviceIsProvisioned;
  String friendlyName; //Max length: 32
  String deviceName; //Max length: 16

  struct structNetwork {
    String ssidName; //Max length: 32
    String wpaKey; //Max length: 32
  } network;

  struct structMqttServer {
    String serverName; //Max length: 32
    int port;
    String username; //Max length: 32
    String password; //Max length: 32
    String controlTopic; //Max length: 32
    String clientTopic; //Max length: 32
    String eventTopic; //Max length: 32
  } mqttServer;

};

typedef struct structBrightness {
  String name; //Max length: 20
  int illumination;
};

typedef struct structLED {
  int pin;
  String name; //Max length: 20
  String color; //Max length: 20
  int illumination;
  int style;
  int styleData;
  struct structBrightness definedBrightness[MAXIMUM_DEFINED_BRIGHTNESS];
};


/*******************************************************************************/
/*        Simplified structures used for EEPROM I/O in section below           */
/*******************************************************************************/

typedef struct _structSettings{
    char friendlyName[33]; //Max length: 32
    char deviceName[17]; //Max length: 16

    struct _structNetwork{
      char ssidName[33]; //Max length: 32
      char wpaKey[33]; //Max length: 32
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

typedef struct _structBrightness {
  char name[21]; //Max length: 20
  unsigned int illumination;
};

typedef struct _structLED {
  unsigned int pin;
  char name[21]; //Max Length: 20
  char color[21]; //Max Length: 20
  int countOfDefinedBrightness;
  struct _structBrightness definedBrightness[MAXIMUM_DEFINED_BRIGHTNESS];
};
