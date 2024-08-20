#include <Arduino.h>
#include <NewPing.h>
#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <PubSubClient.h>

/*Needed libs:
  * PubSubClient
  * Adafruit NeoPixel
  * AsyncTCP
  * ESP Async WebServer
  * NewPing
*/  

const char* ssid = NULL;
const char* password = NULL;
AsyncWebServer server(80);

WiFiClient wifiClientThingie;
PubSubClient mqttClient(wifiClientThingie);

const byte eePromSize = 128;

const byte stripPin = 8;
const byte triggerPin = 9;
const byte echoPin = 10;
const byte maxDistance = 75 /*inches*/ * 2.54; //In CM, everything else inches

const uint32_t red = 0xFF0000;
const uint32_t yellow = 0xFFFF00;
const uint32_t green = 0x00FF00;
const uint32_t black = 0x000000;

const int ledCount = 30;

//Bits
const byte BIT_ON = 1 << 7;
const byte BIT_STOP = 1 << 6;
const byte BIT_SLOW = 1 << 5;
const byte BIT_GO = 1 << 4;

const byte MASK_SUBSTANTIVE = BIT_STOP | BIT_SLOW | BIT_GO;

enum ParkingState {
  OFF_FARAWAY,
  WELCOMING = BIT_ON | BIT_GO | 1,
  KEEP_GOING = BIT_ON | BIT_GO | 2,

  SLOW_DOWN_1 = BIT_ON | BIT_SLOW | 1,
  SLOW_DOWN_2 = BIT_ON | BIT_SLOW | 2,

  STOP_1 = BIT_ON | BIT_STOP | 1,
  STOP_2 = BIT_ON | BIT_STOP | 2,
  STOP_3 = BIT_ON | BIT_STOP | 3,

  OFF_LONG_HERE
};

enum class DistanceThresholds : byte {
  stop3 = 5,
  stop2 = 10,
  stop1 = 15,

  slow2 = 20,
  slow1 = 25,

  keepGoing = 50,
};

enum class DistanceThresholdMemLocation : byte {
  stop3 = 0,
  stop2 = 1,
  stop1 = 2,
  slow2 = 3,
  slow1 = 4,
  keepGoing = 5,
};

struct DeviceConfiguration{
  byte stop3;
  byte stop2;
  byte stop1;
  byte slow2;
  byte slow1;
  byte keepGoing;
  bool autoTurnOff;
  char hostName[24];
  bool enableMQTT;
  char mqttServer[64];
  unsigned short mqttPort;
};

const DeviceConfiguration Defaults = {
  5, 10, 15, 20, 25, 50,
  true,
  "device-name-here",
  false,
  "",
  18830,
};

DeviceConfiguration settings;

const char index_html[] PROGMEM = R"rawliteral(
  <!DOCTYPE HTML>
  <html><head>
    <title>Parking Sensor Web Page</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
  </head
  <body>
  <p>Parking Sensor Web</p>
  <p>Device: %PLACEHOLDER_DEVICE_NAME%</p>
  <p>Current Reading: %PLACEHOLDER_CURRENT_DISTANCE% inches</p>
  <p>&nbsp;</p>
  <form method="POST" action="/" enctype="multipart/form-data">
  <p>Network Name: <input id="network_name" maxlength="32" name="network_name" type="text" value="%PLACEHOLDER_DEVICE_NAME%" /></p>
  <p>Stop 3: <input id="stop_3" maxlength="3" name="stop_3" type="text" value="%PLACEHOLDER_THRESHOLD_STOP_3%" /></p>
  <p>Stop 2: <input id="stop_2" maxlength="3" name="stop_2" type="text" value="%PLACEHOLDER_THRESHOLD_STOP_2%" /></p>
  <p>Stop 1: <input id="stop_1" maxlength="3" name="stop_1" type="text" value="%PLACEHOLDER_THRESHOLD_STOP_1%" /></p>
  <p>Yellow 2: <input id="slow_2" maxlength="3" name="slow_2" type="text" value="%PLACEHOLDER_THRESHOLD_SLOW_2%" /></p>
  <p>Yellow 1: <input id="slow_1" maxlength="3" name="slow_1" type="text" value="%PLACEHOLDER_THRESHOLD_SLOW_1%" /></p>
  <p>Keep Going: <input id="keep_going" maxlength="3" name="keep_going" type="text" value="%PLACEHOLDER_THRESHOLD_GO%" /></p>
  <p>Auto turn off LEDs: <input id="autoTurnOffLEDs" name="autoTurnOffLEDs" type="checkbox" %PLACEHOLDER_AUTO_TURN_OFF% /></p>
  <p>Enable MQTT Reporting: <input id="enableMQTT" name="enableMQTT" type="checkbox" %PLACEHOLDER_MQTT_ENABLE% /></p>
  <p>MQTT Server: <input id="mqtt_server" maxlength="64" name="mqtt_server" type="text" value="%PLACEHOLDER_MQTT_SERVER%" /></p>
  <p>MQTT Port: <input id="mqtt_port" maxlength="5" name="mqtt_port" type="text" value="%PLACEHOLDER_MQTT_PORT%" /></p>
  <p><input id="submitButton" type="submit" value="Save Config &amp; Reboot" /></p>
  </form>
  </body></html>
)rawliteral";

void setupWifi();
void setState(ParkingState state);
void setupLed();
void displayState();
void turnAllOff();
void animateWelcome();
String processor(const String& var);
DeviceConfiguration readConfig();

Adafruit_NeoPixel strip = Adafruit_NeoPixel(ledCount, stripPin, NEO_GRB + NEO_KHZ800);
bool welcomeState[ledCount];
ParkingState currentState;
NewPing sonar(triggerPin, echoPin, maxDistance);
int welcomeDelayCounter = 0;
unsigned long lastSubstantialChangeTime = millis();
unsigned long ledTimeout = 5 * 60 * 1000; //5 minutes

String processor(const String& var)
{ 
  if(var == "PLACEHOLDER_DEVICE_NAME"){
    return WiFi.getHostname();
  } else if( var == "PLACEHOLDER_CURRENT_DISTANCE") {
    long distance = sonar.ping_in();
    return String(distance);
  } else if(var == "PLACEHOLDER_THRESHOLD_STOP_3"){
    return String(settings.stop3);
  } else if(var == "PLACEHOLDER_THRESHOLD_STOP_2"){
    return String(settings.stop2);
  } else if(var == "PLACEHOLDER_THRESHOLD_STOP_1"){
    return String(settings.stop1);
  } else if(var == "PLACEHOLDER_THRESHOLD_SLOW_2"){
    return String(settings.slow2);
  } else if(var == "PLACEHOLDER_THRESHOLD_SLOW_1"){
    return String(settings.slow1);
  } else if(var == "PLACEHOLDER_THRESHOLD_GO"){
    return String(settings.keepGoing);
  } else if(var == "PLACEHOLDER_AUTO_TURN_OFF"){
    if( settings.autoTurnOff){
      return String("Checked");
    } else {
      return String();
    }
  } else if(var == "PLACEHOLDER_MQTT_ENABLE"){
    if( settings.enableMQTT){
      return String("Checked");
    } else {
      return String();
    }
  } else if(var == "PLACEHOLDER_MQTT_SERVER"){
    return String(settings.mqttServer);
  } else if(var == "PLACEHOLDER_MQTT_PORT"){
    return String(settings.mqttPort);
  }
  return String();
}

byte convertStringToByte(const String& inputString) {
    byte result = 0;
    for (int i = 0; i < inputString.length(); ++i) {
        char digitChar = inputString[i];
        if (isdigit(digitChar)) {
            result = result * 10 + (digitChar - '0');
        }
    }
    return result;
}

//TODO: simplify these both
unsigned short convertStringToUShort(const String& inputString) {
    int result = 0;
    for (int i = 0; i < inputString.length(); ++i) {
        char digitChar = inputString[i];
        if (isdigit(digitChar)) {
            result = result * 10 + (digitChar - '0');
        }
    }
    return result;
}

void setup() {
  Serial.begin(115200);

  Serial.println("setup()");

  EEPROM.begin(eePromSize);
  settings = readConfig();
  setupWifi();
  setupLed();

   server.on("/", HTTP_POST, [](AsyncWebServerRequest *request)
   {
      bool rebootRequired = false;
      bool autoTurnOffEncountered = false;
      bool enableMQTTEncountered = false;
      int params = request->params();

      for(int i=0;i<params;i++){
        const AsyncWebParameter* p = request->getParam(i);

        if( p->name() == "stop_3")
          settings.stop3 = convertStringToByte(p->value());
        else if( p->name() == "stop_2")
          settings.stop2 = convertStringToByte(p->value());
        else if( p->name() == "stop_1")
          settings.stop1 = convertStringToByte(p->value());
        else if( p->name() == "slow_2")
          settings.slow2 = convertStringToByte(p->value());
        else if( p->name() == "slow_1")
          settings.slow1 = convertStringToByte(p->value());
        else if( p->name() == "keep_going")
          settings.keepGoing = convertStringToByte(p->value());
        else if( p->name() == "network_name"){
          if( WiFi.getHostname() != p->value()){
            char* source = (char*)(p->value().c_str());
            strncpy(settings.hostName, source, strlen(source));
            settings.hostName[strlen(source)] = '\0'; //Do I really need to null terminate incomming strings?
            Serial.println("Set name:");
            Serial.println(settings.hostName);
            rebootRequired = true;
          }
        }
        else if (p->name() == "autoTurnOffLEDs"){
          if( p->value() == "on"){
            settings.autoTurnOff = true;
          } 
          //If we don't see the variable as part of submission, we can assume it's set to off,
          //and will set it if we haven't seen it later, because we haven't set:
          autoTurnOffEncountered = true;
        }
        else if( p->name() == "enableMQTT"){
          if( p->value() == "on"){
            settings.enableMQTT = true;
	    rebootRequired = true;
          } 
          //If we don't see the variable as part of submission, we can assume it's set to off,
          //and will set it if we haven't seen it later, because we haven't set:
          enableMQTTEncountered = true;
        }
        else if( p->name() == "mqtt_server"){
          if( WiFi.getHostname() != p->value()){
            char* source = (char*)(p->value().c_str());
            strncpy(settings.mqttServer, source, strlen(source));
            settings.mqttServer[strlen(source)] = '\0'; //Do I really need to null terminate incomming strings?

            Serial.println("Set new MQTT Server host name:");
            Serial.println(settings.mqttServer);
            rebootRequired = true;
          }
        }
        else if( p->name() == "mqtt_port"){
          settings.mqttPort = convertStringToUShort(p->value());
          rebootRequired = true;
        } else {
          Serial.println("Unused values:");
          Serial.println(p->name());
          Serial.println(p->value());
        }
      }

      if( autoTurnOffEncountered == false){
        settings.autoTurnOff = false;
      }
      if( enableMQTTEncountered == false){
        settings.enableMQTT = false;
        rebootRequired = true;
      }

      EEPROM.put( 0, settings );
      EEPROM.commit();

      request->send_P(200, "text/html", index_html, processor);

      if( rebootRequired){
        ESP.restart();
      }
   });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html, processor);
  });
 
  server.begin();
}


void reconnectToMQTT() {
  int retryCount = 0;
  // Loop until we're reconnected
  while (!mqttClient.connected() && retryCount <= 10) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (mqttClient.connect(settings.hostName)) {
      Serial.println("MQTT connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 250 ms");
      // Wait 1/4 second before retrying
      delay(250);
      retryCount++;
    }
  }
}

void setupWifi() {
  if( ssid == NULL && password == NULL){
    Serial.println("Skipping wifi & server config");  
    return;
  }
  
  Serial.print("Attempting hostname: ");
  Serial.println(settings.hostName);
  
  WiFi.setHostname(settings.hostName);
  WiFi.begin(ssid, password);
  int attemptCount = 0;

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    attemptCount++;

    if( attemptCount > 10){
      Serial.println("Failed to connect to wifi.");
      return;
    }
  }
  
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Hostname: ");
  Serial.println(WiFi.getHostname());

  if( settings.enableMQTT){
    IPAddress serverIp;
    if( serverIp.fromString(settings.mqttServer)){
      mqttClient.setServer(serverIp, settings.mqttPort);
    } else {
      mqttClient.setServer(settings.mqttServer, settings.mqttPort);
    }
    reconnectToMQTT();
  }
  server.begin();
}

void setupLed(){
  strip.begin();
  strip.setBrightness(40);
  strip.fill(strip.Color(255, 0, 255), 0, 10);
  strip.show();  // Initialize all pixels to 'off'

  //Init welcome state pattern
  for (int x = 0; x < ledCount; x++) {
    if (x % 3 == 0) {
      welcomeState[x] = true;
    }
  }
}

DeviceConfiguration readConfig() {
  DeviceConfiguration config;
  EEPROM.get( 0, config );

  //TODO: Determine which state is default/empty
  if( (config.stop3 == 0 && config.stop2 == 0 && config.stop1 == 0) ||
      (config.stop3 == 255 && config.stop2 == 255 && config.stop1 == 255)){
    Serial.println("Unhappy, using defaults.");
    return Defaults;
  }

  return config;
}

void loop() {
  long distance = sonar.ping_in();
  if (distance == 0) {
    distance = maxDistance;
  }
  
  if (settings.stop3 != 0 && distance < settings.stop3) {
    setState(ParkingState::STOP_3);
  } else if (settings.stop2 != 0 && distance < settings.stop2) {
    setState(ParkingState::STOP_2);
  } else if (settings.stop1 != 0 && distance < settings.stop1) {
    setState(ParkingState::STOP_1);
  } else if (settings.slow2 != 0 && distance < settings.slow2) {
    setState(ParkingState::SLOW_DOWN_2);
  } else if (settings.slow1 != 0 && distance < settings.slow1) {
    setState(ParkingState::SLOW_DOWN_1);
  } else if (settings.keepGoing != 0 && distance < settings.keepGoing) {
    setState(ParkingState::KEEP_GOING);
  } else {
    setState(ParkingState::WELCOMING);
  }

  if (currentState == ParkingState::WELCOMING) {
    if (welcomeDelayCounter == 0) {
      displayState();
    }
    welcomeDelayCounter++;
    if (welcomeDelayCounter >= 4) {
      welcomeDelayCounter = 0;
      delay(50);
    } else {
      delay(100);
    }
  } else {
    displayState();
    delay(50);
  }
}


void setState(ParkingState state) {
  bool substantiveChange = false;

  if ((currentState & MASK_SUBSTANTIVE) != (state & MASK_SUBSTANTIVE)) {
    substantiveChange = true;
    welcomeDelayCounter = 0;  //Reset
    lastSubstantialChangeTime = millis();

    if( settings.enableMQTT){
      char topicName[100];
      //topic string for vehicle presence 
      sprintf(topicName, "home/garage/%s/status/vehicle-present", settings.hostName);
      
      reconnectToMQTT(); //If not already
      if( (state & BIT_STOP) == BIT_STOP){
        mqttClient.publish(topicName, "true");
      } else{
        mqttClient.publish(topicName, "false");
      }
    }
  }

  currentState = state;
}

void displayState() {
  unsigned long timeSinceLastChange = millis() - lastSubstantialChangeTime;
  if (settings.autoTurnOff && timeSinceLastChange > ledTimeout) {
    turnAllOff();
    return;
  }

  if (currentState == ParkingState::STOP_3) {
    strip.fill(red, 0, ledCount);
    strip.show();
  } else if (currentState == ParkingState::STOP_2) {
    strip.fill(black, 0, 5);
    strip.fill(red, 5, 20);
    strip.fill(black, 25, 5);
    strip.show();
  } else if (currentState == ParkingState::STOP_1) {
    strip.fill(black, 0, 10);
    strip.fill(red, 10, 10);
    strip.fill(black, 20, 10);
    strip.show();
  } else if (currentState == ParkingState::SLOW_DOWN_2) {
    strip.fill(black, 0, 11);
    strip.fill(yellow, 11, 8);
    strip.fill(black, 19, 11);
    strip.show();
  } else if (currentState == ParkingState::SLOW_DOWN_1) {
    strip.fill(black, 0, 12);
    strip.fill(yellow, 12, 6);
    strip.fill(black, 18, 12);
    strip.show();
  } else if (currentState == ParkingState::KEEP_GOING) {
    strip.fill(green, 0, 30);
    strip.show();
  } else if (currentState == ParkingState::WELCOMING) {
    animateWelcome();
  } else if (currentState & BIT_ON == 0) {
    strip.fill(black, 0, 30);
    strip.show();
  }
}

void animateWelcome() {
  bool first = welcomeState[0];

  for (int x = 0; x < ledCount - 1; x++) {
    welcomeState[x] = welcomeState[x + 1];
    uint32_t color = welcomeState[x] ? green : black;
    strip.setPixelColor(x, color);
  }

  welcomeState[ledCount-1] = first;
  uint32_t color = welcomeState[ledCount-1] ? green : black;
  strip.setPixelColor(ledCount-1, color);
  strip.show();
}

void turnAllOff() {
  strip.fill(black, 0, ledCount);
  strip.show();
}