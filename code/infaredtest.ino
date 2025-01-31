#include <Wire.h>
#include <Adafruit_AMG88xx.h>

Adafruit_AMG88xx amg;

void setup() {
    Serial.begin(9600);
    Serial.println(F("AMG88xx test"));

    bool status;
    
    // default settings
    status = amg.begin();
    if (!status) {
        Serial.println("Could not find a valid AMG88xx sensor, check wiring!");
        while (1);
    }
    
    Serial.println("-- Thermistor Test --");

    Serial.println();

    delay(100); // let sensor boot up
}


void loop() { 
    Serial.print("Thermistor Temperature = ");
    Serial.print(amg.readThermistor());
    Serial.println(" *C");
  
    Serial.println();

    //delay a second
    delay(1000);
}
