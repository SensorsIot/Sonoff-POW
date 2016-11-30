#include "power.h"
ESP8266PowerClass power_dev;
unsigned long previousMillis = 0;
const long interval = 2000;
void setup() {
    Serial.begin(74880);
    Serial1.begin(115200);
    pinMode(D6,OUTPUT);
    digitalWrite(D6,HIGH);
    power_dev.enableMeasurePower();
    power_dev.selectMeasureCurrentOrValtage(VOLTAGE);
    power_dev.startMeasure();
   
    
    
}

void loop() {
    unsigned long currentMillis = millis();
    if(currentMillis - previousMillis >= interval) {
        previousMillis = currentMillis;   
        Serial1.println(power_dev.getPower());
        Serial1.println(power_dev.getCurrent());
        Serial1.println(power_dev.getVoltage());
    }
   // yield();
}

