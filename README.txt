**การตั้งค่าบอร์ด Arduino (จากภาพ Setting Board.png)**

- **Board**: ESP32S3 Dev Module
- **Upload Speed**: 115200
- **USB Mode**: Hardware CDC and JTAG
- **USB CDC On Boot**: Enabled
- **USB Firmware MSC On Boot**: Disabled
- **USB DFU On Boot**: Disabled
- **Upload Mode**: UART0 / Hardware CDC
- **CPU Frequency**: 240MHz (WiFi)
- **Flash Mode**: QIO 80MHz
- **Flash Size**: 4MB (32Mb)
- **Partition Scheme**: Default 4MB with SPIFFS (1.2MB APP/1.5MB SPIFFS)
- **Core Debug Level**: None
- **PSRAM**: OPI PSRAM
- **Arduino Runs On**: Core 1
- **Events Run On**: Core 1
- **Erase All Flash Before Sketch Upload**: Disabled
- **JTAG Adapter**: ESP USB Bridge
- **Zigbee Mode**: Disabled
- **Port**: COM4 (แล้วแต่เครื่อง)

**คำอธิบายการทำงานของโค้ด**

## **ภาพรวมของโค้ด**
โค้ดนี้เป็นหุ่นยนต์ที่สามารถเคลื่อนที่ได้ 4 ทิศทาง (เดินหน้า, ถอยหลัง, เลี้ยวซ้าย, เลี้ยวขวา) และสามารถสตรีมวิดีโอจากกล้องได้ผ่านเครือข่าย Wi-Fi นอกจากนี้ยังมีการเชื่อมต่อกับเซ็นเซอร์ตรวจจับอุณหภูมิ AMG88xx เพื่อนำค่าที่อ่านได้มาแสดงผลผ่านหน้าเว็บอินเตอร์เฟซ

## **โครงสร้างของโค้ด**
โค้ดแบ่งเป็น 3 ส่วนหลัก ได้แก่:
1. **การตั้งค่าและกำหนดค่าขา GPIO** - กำหนดค่าขาอินพุตและเอาต์พุตของ ESP32-CAM สำหรับการควบคุมกล้องและมอเตอร์
2. **การตั้งค่าเซิร์ฟเวอร์ HTTP** - ใช้เพื่อให้บริการหน้าเว็บ HTML ที่สามารถควบคุมการเคลื่อนที่ของหุ่นยนต์และแสดงข้อมูลจากเซ็นเซอร์
3. **ฟังก์ชันควบคุมการทำงาน** - มีฟังก์ชันสำหรับรับคำสั่งจากเว็บอินเตอร์เฟซเพื่อควบคุมมอเตอร์ และอ่านค่าจากเซ็นเซอร์อุณหภูมิ

---

## **การอธิบายโค้ดทีละส่วน**

### **1. การเรียกใช้ไลบรารีที่จำเป็น**

#include "esp_camera.h"
#include <WiFi.h>
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "fb_gfx.h"
#include "esp_http_server.h"
#include <Wire.h>
#include <Adafruit_AMG88xx.h>

**คำอธิบาย:**
- 'esp_camera.h' ใช้สำหรับควบคุมกล้องบน ESP32-CAM
- 'WiFi.h' ใช้สำหรับเชื่อมต่อกับเครือข่าย Wi-Fi
- 'esp_http_server.h' ใช้ในการสร้างเว็บเซิร์ฟเวอร์บน ESP32
- 'Wire.h' ใช้สำหรับการสื่อสารผ่าน I2C กับเซ็นเซอร์อุณหภูมิ
- 'Adafruit_AMG88xx.h' เป็นไลบรารีสำหรับใช้งานเซ็นเซอร์ AMG88xx

---

### **2. การตั้งค่าขา GPIO และเครือข่าย Wi-Fi**

const char *ssid = "Pony";
const char *password = "knuttamon0002";

**คำอธิบาย:**
- 'ssid' และ 'password' เป็นค่าที่ใช้เชื่อมต่อกับเครือข่าย Wi-Fi ที่กำหนด


#define MOTOR_1_PIN_1 39
#define MOTOR_1_PIN_2 40
#define MOTOR_2_PIN_1 42
#define MOTOR_2_PIN_2 41

**คำอธิบาย:**
- กำหนดขา GPIO ที่ใช้สำหรับควบคุมมอเตอร์ของหุ่นยนต์

---

### **3. การสร้างเว็บเซิร์ฟเวอร์และการจัดการคำขอ HTTP**


httpd_handle_t camera_httpd = NULL;
httpd_handle_t stream_httpd = NULL;

**คำอธิบาย:**
- 'camera_httpd' ใช้เป็นตัวแปรเก็บเซิร์ฟเวอร์ของกล้อง
- 'stream_httpd' ใช้สำหรับการสตรีมวิดีโอจากกล้อง


static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, (const char *)INDEX_HTML, strlen(INDEX_HTML));
}

**คำอธิบาย:**
- ฟังก์ชัน 'index_handler' ใช้ส่งไฟล์ HTML ไปยังเบราว์เซอร์เมื่อมีการเรียกเข้ามาที่ '/'

---

### **4. การควบคุมมอเตอร์จากคำสั่ง HTTP**

static esp_err_t cmd_handler(httpd_req_t *req)
{
    char variable[32] = {0};
    if (httpd_query_key_value(buf, "go", variable, sizeof(variable)) == ESP_OK)
    {
        if (!strcmp(variable, "forward"))
        {
            digitalWrite(MOTOR_1_PIN_1, 1);
            digitalWrite(MOTOR_1_PIN_2, 0);
            digitalWrite(MOTOR_2_PIN_1, 1);
            digitalWrite(MOTOR_2_PIN_2, 0);
        }
        else if (!strcmp(variable, "stop"))
        {
            digitalWrite(MOTOR_1_PIN_1, 0);
            digitalWrite(MOTOR_1_PIN_2, 0);
            digitalWrite(MOTOR_2_PIN_1, 0);
            digitalWrite(MOTOR_2_PIN_2, 0);
        }
    }
    return httpd_resp_send(req, NULL, 0);
}

**คำอธิบาย:**
- ฟังก์ชัน 'cmd_handler' ใช้รับค่าคำสั่งการเคลื่อนที่ของหุ่นยนต์จากเว็บอินเตอร์เฟซและเปลี่ยนสถานะของมอเตอร์ตามที่ได้รับคำสั่ง

---

### **5. การอ่านค่าอุณหภูมิจากเซ็นเซอร์ AMG88xx และส่งข้อมูลไปยังเว็บ**

static esp_err_t sensor_handler(httpd_req_t *req)
{
    float pixels[AMG88xx_PIXEL_ARRAY_SIZE];
    amg.readPixels(pixels);
    String json = "[";
    for (int i = 0; i < AMG88xx_PIXEL_ARRAY_SIZE; i++)
    {
        json += String(pixels[i], 2);
        if (i < AMG88xx_PIXEL_ARRAY_SIZE - 1)
        {
            json += ",";
        }
    }
    json += "]";
    return httpd_resp_send(req, json.c_str(), json.length());
}

**คำอธิบาย:**
- ฟังก์ชัน 'sensor_handler' อ่านค่าจากเซ็นเซอร์ AMG88xx แล้วส่งข้อมูลเป็น JSON ให้กับหน้าเว็บเพื่อนำไปแสดงผล

---

### **6. การเริ่มต้นระบบ**

void setup()
{
    pinMode(MOTOR_1_PIN_1, OUTPUT);
    pinMode(MOTOR_1_PIN_2, OUTPUT);
    pinMode(MOTOR_2_PIN_1, OUTPUT);
    pinMode(MOTOR_2_PIN_2, OUTPUT);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    startCameraServer();
}

**คำอธิบาย:**
- กำหนดค่าเริ่มต้นของมอเตอร์และเชื่อมต่อ Wi-Fi
- เรียกใช้ 'startCameraServer();' เพื่อเริ่มเซิร์ฟเวอร์

