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
  String alias;
  int color;
  int illumination;
  int style;
  int styleData;
  int countOfDefinedIntensity;

  struct structIntensity {
    String alias;
    int illumination;
  } definedIntensity[8];

};