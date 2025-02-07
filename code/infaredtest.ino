#include <Wire.h>
#include <Adafruit_AMG88xx.h>

Adafruit_AMG88xx amg;

void setup() {
  Serial.begin(115200);

  // ใช้ SDA กับ SCL ที่ GPIO 13 และ GPIO 14
  Wire.begin(13, 14);

  if (!amg.begin(0x68)) {
    Serial.println("Could not find a valid AMG88xx sensor, check wiring!");
    while (1);
  }

  Serial.println("AMG88xx sensor found!");
}

void loop() {
  float pixels[AMG88xx_PIXEL_ARRAY_SIZE];
  
  // อ่านข้อมูลจากเซ็นเซอร์
  amg.readPixels(pixels);

  // แสดงข้อมูลอุณหภูมิของแต่ละพิกเซล
  for (uint16_t i = 0; i < AMG88xx_PIXEL_ARRAY_SIZE; i++) {
    if (i % 8 == 0) Serial.println(); // ขึ้นบรรทัดใหม่ทุก 8 พิกเซล
    Serial.print(pixels[i], 2);
    Serial.print(" ");
  }
Serial.print("\n------------------------------");
  delay(500); // หน่วงเวลา 500ms ก่อนอ่านข้อมูลใหม่
}
