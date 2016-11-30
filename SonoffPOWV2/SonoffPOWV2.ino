/* This sketch connects to the iopappstore and loads the assigned firmware down. The assignment is done on the server based on the MAC address of the board

    On the server, you need PHP script "iotappstore.php" and the bin files are in the .\bin folder

    This work is based on the ESPhttpUpdate examples

    To add new constants in WiFiManager search for "NEW CONSTANTS" and insert them according the "boardName" example

  Copyright (c) [2016] [Andreas Spiess]

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#define VERSION "V1.1"
#define FIRMWARE "SonoffPOW "VERSION

#define SERIALDEBUG         // Serial is used to present debugging messages 
#define REMOTEDEBUGGING     // telnet is used to present


//#define SONOFFSWITCH
#define SONOFFPOW

// Devicedefinitions

#ifdef SONOFFSWITCH
#define DEVICE 1
#endif

#ifdef SONOFFPOW
#define DEVICE 2
#endif


//#define BOOTSTATISTICS    // send bootstatistics to Sparkfun


#include <credentials.h>
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <ESP8266httpUpdate.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include "RemoteDebug.h"        //https://github.com/JoaoLopesF/RemoteDebug
#include <WiFiManager.h>        //https://github.com/kentaylor/WiFiManager
#include <PubSubClient.h>   // https://github.com/knolleary/pubsubclient/releases/tag/v2.6
#include <Ticker.h>
#include <power.h>
#include <string.h>


extern "C" {
#include "user_interface.h" // this is for the RTC memory read/write functions
}



// -------- PIN DEFINITIONS ------------------


#ifdef ARDUINO_ESP8266_ESP01           // Generic ESP's 
#define D0 16
#define D1 5
#define D2 4
#define D3 0
#define D4 2
#define D5 14
#define D6 12
#define D7 13
#define D8 15
#endif

#if DEVICE==1   // SONOFFSWITCH
#define LEDgreen D7
#define RELAYPIN D6
#define GPIO0 D3
#endif

#if DEVICE==2   // SONOFFPOW
#define LEDgreen D8
#define RELAYPIN D6
#define GPIO0 D3
#endif


//---------- CODE DEFINITIONS ----------
#define MAXDEVICES 5
#define STRUCT_CHAR_ARRAY_SIZE 50  // length of config variables
#define SERVICENAME "SONOFF"  // name of the MDNS service used in this group of ESPs
#define DELAYSEC 10*60   // 7 minutes
#define MAX_WIFI_RETRIES 50

#define RTCMEMBEGIN 68
#define MAGICBYTE 85


// -------- SERVICES --------------

WiFiServer server(80);
Ticker blink;
ESP8266PowerClass powerMeter;

// remoteDebug
#ifdef REMOTEDEBUGGING
RemoteDebug Debug;
#endif

#ifdef TLS
WiFiClientSecure  wifiClient;
#else
WiFiClient        wifiClient;
#endif
PubSubClient      mqttClient(wifiClient);


//--------- ENUMS AND STRUCTURES  -------------------

enum CMD {
  CMD_NOT_DEFINED,
  CMD_PIR_STATE_CHANGED,
  CMD_BUTTON_STATE_CHANGED,
};

typedef struct {
  char boardName[STRUCT_CHAR_ARRAY_SIZE];
  char IOTappStore1[STRUCT_CHAR_ARRAY_SIZE];
  char IOTappStorePHP1[STRUCT_CHAR_ARRAY_SIZE];
  char IOTappStore2[STRUCT_CHAR_ARRAY_SIZE];
  char IOTappStorePHP2[STRUCT_CHAR_ARRAY_SIZE];
  // insert NEW CONSTANTS according boardname example HERE!
  char mqttUser[STRUCT_CHAR_ARRAY_SIZE];
  char mqttPassword[STRUCT_CHAR_ARRAY_SIZE];
  char mqttServer[STRUCT_CHAR_ARRAY_SIZE];
  char mqttPort[6];
  char custom_text[STRUCT_CHAR_ARRAY_SIZE];
  //-----------------------------------------------------
  char magicBytes[4];
} strConfig;

strConfig config = {
  "SonoffPOW1",
  "192.168.0.200",
  "/iotappstore/iotappstorev20.php",
  "iotappstory.org",
  "ota/esp8266-v1.php",
  ADAFRUIT_MQTT_USERNAME,
  ADAFRUIT_MQTT_KEY,
  ADAFRUIT_SERVER,
  ADAFRUITSERVERPORT,
  "",
  "CFG"  // Magic Bytes
};

typedef struct {
  byte markerFlag;
  int bootTimes;
} rtcMemDef __attribute__((aligned(4)));
rtcMemDef rtcMem;

//---------- VARIABLES ----------

String boardName, IOTappStore1, IOTappStorePHP1, IOTappStore2, IOTappStorePHP2, custom_text, mqttUser, mqttPassword, mqttServer, mqttPort; // add NEW CONSTANTS according boardname example

unsigned long entry;

char boardMode = 'N';  // Normal operation or Configuration mode?

volatile unsigned long buttonEntry, buttonTime;
volatile bool buttonChanged = false;

unsigned long infoEntry;

// MQTT
char              MQTT_CLIENT_ID[7]                                 = {0};
const char*       MQTT_POW_RELAYS_POSITION   = "/powposition";
const char*       MQTT_POW_COMMAND = "/powcommand";
const char*       MQTT_POW_POWER = "/powpower";
const char*       MQTT_POW_CURRENT = "/powcurrent";
const char*       MQTT_SWITCH_ON_PAYLOAD                            = "ON";
const char*       MQTT_SWITCH_OFF_PAYLOAD                           = "OFF";

String topicRelayPosition;
String topicPowCommand;
String topicPowPower;
String topicPowCurrent;

unsigned long previousMillis = 0;
const long MQTT_INTERVAL = 1600;

volatile uint8_t cmd = CMD_NOT_DEFINED;

uint8_t           relayState                                        = HIGH;  // HIGH: closed switch
uint8_t           buttonState                                       = HIGH; // HIGH: opened switch
uint8_t           currentButtonState                                = buttonState;
long              buttonStartPressed                                = 0;
long              buttonDurationPressed                             = 0;
uint8_t           pirState                                          = LOW;
uint8_t           currentPirState                                   = pirState;



//---------- FUNCTIONS ----------
void loopWiFiManager(void);
void readFullConfiguration(void);
bool readRTCmem(void);
void  printRTCmem(void);
void setRelayState(bool);

//---------- OTHER .H FILES ----------
#include <ESP_Helpers.h>
#include "WiFiManager_Helpers.h"
#include <SparkfunReport.h>


///////////////////////////////////////////////////////////////////////////
//   Adafruit IO with SSL/TLS
///////////////////////////////////////////////////////////////////////////
/*
  Function called to verify the fingerprint of the MQTT server certificate
*/
#ifdef TLS
void verifyFingerprint() {
  DEBUG_PRINT(F("INFO: Connecting to "));
  DEBUG_PRINTLN(config.mqttServer);

  if (!wifiClient.connect(config.mqttServer, atoi(config.mqttPort))) {
    DEBUG_PRINTLN(F("ERROR: Connection failed. Halting execution"));
    espRestart("No connection");
  }

  if (wifiClient.verify(fingerprint, config.mqttServer)) {
    DEBUG_PRINTLN(F("INFO: Connection secure"));
  } else {
    DEBUG_PRINTLN(F("ERROR: Connection insecure! Halting execution"));
    espRestart("No Connection");
  }
}
#endif



///////////////////////////////////////////////////////////////////////////
//   MQTT
///////////////////////////////////////////////////////////////////////////
/*
   Function called when a MQTT message arrived
   @param p_topic   The topic of the MQTT message
   @param p_payload The payload of the MQTT message
   @param p_length  The length of the payload
*/
void callback(char* p_topic, byte* p_payload, unsigned int p_length) {
  // handle the MQTT topic of the received message
  DEBUG_PRINT("Message received ");
  DEBUG_PRINTLN(p_topic);  DEBUG_PRINTLN(topicPowCommand);
  String _payload = "";
  for ( int ii = 0; ii < p_length; ii++) _payload += (char)p_payload[ii];
  DEBUG_PRINTLN(_payload);
  if (String(topicPowCommand).equals(p_topic)) {
    DEBUG_PRINTLN("String ok");
    if (_payload.indexOf(MQTT_SWITCH_ON_PAYLOAD) >= 0) {
      DEBUG_PRINTLN("High");
      setRelayState(HIGH);
    } else if (_payload.indexOf(MQTT_SWITCH_OFF_PAYLOAD) >= 0) {
      DEBUG_PRINTLN("Low");
      setRelayState(LOW);
    }
  }
}

/*
  Function called to publish the state of the Sonoff relay
*/
bool publishMeasurement(String topic, double value) {
  bool result = false;
  char _value[10];
  char _topic[50];
  topic.toCharArray(_topic, 50);

  DEBUG_PRINT("Value ");
  DEBUG_PRINTLN(value);
  dtostrf(value, 6, 2, _value);
  DEBUG_PRINT("_Value ");
  DEBUG_PRINTLN(_value);

  if ( mqttClient.publish(_topic, _value, true)) {

    DEBUG_PRINT(F("INFO: MQTT message publish succeeded. Topic: "));
    DEBUG_PRINT(topic);
    DEBUG_PRINT(F(". Payload: "));
    DEBUG_PRINTLN(_value);
  } else {
    DEBUG_PRINTLN(F("ERROR: MQTT relayState message publish failed"));
  }
}


bool subscribeCommand() {
  char buf[50];
  topicPowCommand.toCharArray(buf, 50);

  if (mqttClient.connected()) {
    if (mqttClient.subscribe(buf, 1)) {
      DEBUG_PRINT(F("INFO: Sending the MQTT subscribe succeeded. Topic: "));
      DEBUG_PRINTLN(buf);
    } else {
      DEBUG_PRINT(F("ERROR: Subscribtion failed. Topic: "));
      DEBUG_PRINTLN(buf);
      return false;
    }
  }
  return true;
}

//  Function called to connect/reconnect to the MQTT broker


bool reconnect() {
  if (mqttClient.connect(config.boardName, config.mqttUser, config.mqttPassword)) {
    DEBUG_PRINTLN(F("INFO: The client is successfully connected to the MQTT broker"));
    if (subscribeCommand()) DEBUG_PRINTLN(F("Successfully Subscribed: "));
    else DEBUG_PRINTLN(F("Not able to subscribe: "));
    return true;
  } else {
    DEBUG_PRINTLN(F("ERROR: The connection to the MQTT broker failed"));
    DEBUG_PRINT(F("Username: "));
    DEBUG_PRINTLN(config.mqttUser);
    DEBUG_PRINT(F("Password: "));
    DEBUG_PRINTLN(config.mqttPassword);
    DEBUG_PRINT(F("Broker: "));
    DEBUG_PRINTLN(config.mqttServer);
    return false;
  }
}


//----------- Sonoff switch -------------------

void setRelayState(bool relayState) {
  bool result;
  char _topic[50];
  topicRelayPosition.toCharArray(_topic, 50);
  DEBUG_PRINTLN(relayState);
  if (relayState == HIGH) {
    result = mqttClient.publish(_topic, MQTT_SWITCH_ON_PAYLOAD, true);
    digitalWrite(RELAYPIN, ON);
  }
  else {
    result = mqttClient.publish(_topic, MQTT_SWITCH_OFF_PAYLOAD, true);
    digitalWrite(RELAYPIN, OFF);
  }
  if (result) {
    DEBUG_PRINT(F("INFO: MQTT message publish succeeded. Topic: "));
    DEBUG_PRINT(_topic);
    DEBUG_PRINT(F(". Payload: "));
    DEBUG_PRINTLN(relayState);
  } else {
    DEBUG_PRINTLN(F("ERROR: MQTT relayState message publish failed"));
  }
}


//----------------------- SETUP ---------------------


void setup() {
  Serial.begin(115200);
  for (int i = 0; i < 5; i++) DEBUG_PRINTLN("");
  DEBUG_PRINTLN("Start "FIRMWARE);

  // ----------- PINS ----------------
  pinMode(GPIO0, INPUT_PULLUP);  // GPIO0 as input for Config mode selection
  pinMode(RELAYPIN, OUTPUT);
#ifdef LEDred
  pinMode(LEDred, OUTPUT);
#endif
#ifdef LEDgreen
  pinMode(LEDgreen, OUTPUT);
#endif
  LEDswitch(Both);

  // ------------- INTERRUPTS ----------------------------
  attachInterrupt(GPIO0, ISRbuttonStateChanged, CHANGE);

  //------------- LED and DISPLAYS ------------------------

  LEDswitch(GreenBlink);


  // --------- BOOT STATISTICS ------------------------
  // read and increase boot statistics (optional)
  readRTCmem();
  rtcMem.bootTimes++;
  writeRTCmem();
  printRTCmem();


  //---------- BOARD MODE -----------------------------

  system_rtc_mem_read(RTCMEMBEGIN + 100, &boardMode, 1);   // Read the "boardMode" flag RTC memory to decide, if to go to config
  if (boardMode == 'C') configESP();  // go into Configuration Mode

  DEBUG_PRINTLN("------------- Normal Mode -------------------");

  // --------- START WIFI --------------------------
  readFullConfiguration();
  WiFi.mode(WIFI_STA);
  WiFi.begin();
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < MAX_WIFI_RETRIES) {
    delay(500);
    DEBUG_PRINT(".");
    retries++;
  }
  if (retries >= MAX_WIFI_RETRIES || WiFi.psk() == "") {
    DEBUG_PRINTLN("NoConn");
    if ( WiFi.psk() == "") espRestart('C', "No Connection...");
    else espRestart('N', "No Connection...");
  } else {

    DEBUG_PRINTLN("");
    DEBUG_PRINTLN("WiFi connected");
    getMACaddress();
    printMacAddress();
    DEBUG_PRINT("IP Address: ");
    DEBUG_PRINTLN(WiFi.localIP());
    //    iotAppstory();

#ifdef REMOTEDEBUGGING
    remoteDebugSetup();
    Debug.println(config.boardName);
#endif

    // ------------------ mDNS ------------------------------
    // Register host name in WiFi and mDNS
    String hostNameWifi = boardName;   // boardName is device name
    hostNameWifi.concat(".local");
    WiFi.hostname(hostNameWifi);
    if (MDNS.begin(config.boardName)) {
      DEBUG_PRINT("* MDNS responder started. http://");
      DEBUG_PRINTLN(hostNameWifi);
      MDNS.addService("SERVICENAME", "tcp", 8080);
    }



    // ----------- SPECIFIC SETUP CODE ----------------------------


    //------------------  MQTT -------------------------------

    // get the Chip ID of the switch and use it as the MQTT client ID
    sprintf(MQTT_CLIENT_ID, "%06X", ESP.getChipId());
    DEBUG_PRINT(F("INFO: MQTT client ID/Hostname: "));
    DEBUG_PRINTLN(MQTT_CLIENT_ID);

    // set the state topic: <Chip ID>/switch/state
    //  sprintf(MQTT_POW_RELAYS_POSITION, "%06X/switch/state", ESP.getChipId());
    DEBUG_PRINT(F("INFO: MQTT state topic: "));
    DEBUG_PRINTLN(MQTT_POW_RELAYS_POSITION);

    // set the command topic: <Chip ID>/switch/switch
    // sprintf(MQTT_POW_COMMAND, "%06X/switch/switch", ESP.getChipId());
    DEBUG_PRINT(F("INFO: MQTT command topic: "));
    DEBUG_PRINTLN(MQTT_POW_COMMAND);

    // insert NEW CONSTANTS according boardname example
    // custom_text = String(config.custom_text);
    mqttUser = String(config.mqttUser);
    mqttPassword = String(config.mqttPassword);
    mqttServer = String(config.mqttServer);
    mqttPort = String(config.mqttPort);

    // Topic definition
    topicRelayPosition = mqttUser + "/f" + MQTT_POW_RELAYS_POSITION + "-" + boardName;
    topicPowCommand = mqttUser + "/f" + MQTT_POW_COMMAND + "-" + boardName;
    topicPowPower = mqttUser + "/f" + MQTT_POW_POWER + "-" + boardName;
    topicPowCurrent = mqttUser + "/f" + MQTT_POW_CURRENT + "-" + boardName;
    DEBUG_PRINTLN(topicRelayPosition);
    DEBUG_PRINTLN(topicPowCommand);
    DEBUG_PRINTLN(topicPowPower);
    DEBUG_PRINTLN(topicPowCurrent);

#ifdef TLS
    // check the fingerprint of io.adafruit.com's SSL cert
    verifyFingerprint();
#endif

    // configure MQTT
    mqttClient.setServer(config.mqttServer, atoi(config.mqttPort));
    mqttClient.setCallback(callback);


    // ------------- POWER METER ---------------------
    powerMeter.setPowerParam(12.65801022, 0.0);
    powerMeter.setCurrentParam(19.52, -85.9);
    powerMeter.setVoltageParam(0.45039823, 0.0);

    powerMeter.enableMeasurePower();
    powerMeter.selectMeasureCurrentOrVoltage(CURRENT);  // select voltage measurement
    powerMeter.startMeasure();


    // ----------- END SPECIFIC SETUP CODE ----------------------------
  }  // End WiFi necessary
  LEDswitch(None);

#ifdef BOOTSTATISTICS
  sendSparkfun();   // send boot statistics to sparkfun
#endif
  DEBUG_PRINTLN("setup done");
}

//------------------- LOOP -------------------------------------
void loop() {

  //-------- Standard Block ---------------
  if (buttonChanged && buttonTime > 4000) espRestart('C', "Going into Configuration Mode");  // long button press > 4sec
  if (buttonChanged && buttonTime > 1000 && buttonTime < 4000) iotAppstory(); // long button press > 1sec
  buttonChanged = false;
#ifdef REMOTEDEBUGGING
  Debug.handle();
  if (Debug.ative(Debug.INFO) && (millis() - infoEntry) > 5000) {
    Debug.printf("Firmware: %s\r\n", FIRMWARE);
    infoEntry = millis();
  }
#endif
  yield();
  //-------- Standard Block ---------------

  // measure
  double power = powerMeter.getPower();
  double current = powerMeter.getCurrent();
  double frequency = powerMeter.getCurrFrequency();

  // keep the MQTT client connected to the broker
  if (!mqttClient.connected() && millis() - entry > MQTT_INTERVAL) {
    entry = millis();
    reconnect();
  }
  unsigned long currentMillis = millis();
  // Every few seconds
  if (millis() - previousMillis >= MQTT_INTERVAL) {
    previousMillis = millis();
    DEBUG_PRINT("Power ");
    DEBUG_PRINTLN(power);
    DEBUG_PRINT("Current ");
    DEBUG_PRINTLN(current);
    DEBUG_PRINTLN(powerMeter.getVoltage());
#ifdef REMOTEDEBUGGING
    Debug.print("Power: ");
    Debug.print(power);
    Debug.print(" Current: ");
    Debug.print(current);
    Debug.print(" Frequency: ");
    Debug.print(frequency);
    Debug.print(" Voltage: ");
    Debug.println(powerMeter.getVoltage());
#endif
    if (mqttClient.connected()) {
      publishMeasurement(topicPowPower, power);
      publishMeasurement(topicPowCurrent, current);
    }
  }
  mqttClient.loop();
}

//------------------------- END LOOP --------------------------------------------

void readFullConfiguration() {
  readConfig();  // configuration in EEPROM
  // insert NEW CONSTANTS according switchName1 example
  //add all parameters here
  mqttUser = String(config.mqttUser);
  mqttPassword = String(config.mqttPassword);
  mqttServer = (config.mqttServer);
  mqttPort = String(config.mqttPort);
}


bool readRTCmem() {
  bool ret = true;
  system_rtc_mem_read(RTCMEMBEGIN, &rtcMem, sizeof(rtcMem));
  if (rtcMem.markerFlag != MAGICBYTE) {
    rtcMem.markerFlag = MAGICBYTE;
    rtcMem.bootTimes = 0;
    system_rtc_mem_write(RTCMEMBEGIN, &rtcMem, sizeof(rtcMem));
    ret = false;
  }
  return ret;
}

void printRTCmem() {
  DEBUG_PRINTLN("");
  DEBUG_PRINTLN("rtcMem ");
  DEBUG_PRINT("markerFlag ");
  DEBUG_PRINTLN(rtcMem.markerFlag);
  DEBUG_PRINT("bootTimes ");
  DEBUG_PRINTLN(rtcMem.bootTimes);
}
