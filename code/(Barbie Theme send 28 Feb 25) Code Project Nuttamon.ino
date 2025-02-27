#include "esp_camera.h"
#include <WiFi.h>
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "fb_gfx.h"
#include "esp_http_server.h"

#include <Wire.h>
#include <Adafruit_AMG88xx.h>

// ==============================
// ตั้งค่า AMG88xx
// ==============================
Adafruit_AMG88xx amg;

// ==============================
// ตั้งค่า WiFi
// ==============================
const char* ssid = "Pony";
const char* password = "knuttamon0002";

// ==============================
// ตั้งค่าขา GPIO กล้อง
// ==============================
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  15
#define SIOD_GPIO_NUM  4
#define SIOC_GPIO_NUM  5

#define Y9_GPIO_NUM    16
#define Y8_GPIO_NUM    17
#define Y7_GPIO_NUM    18
#define Y6_GPIO_NUM    12
#define Y5_GPIO_NUM    10
#define Y4_GPIO_NUM    8
#define Y3_GPIO_NUM    9
#define Y2_GPIO_NUM    11
#define VSYNC_GPIO_NUM 6
#define HREF_GPIO_NUM  7
#define PCLK_GPIO_NUM  13

// ==============================
// ตั้งค่าขา GPIO มอเตอร์
// ==============================
#define MOTOR_1_PIN_1  39
#define MOTOR_1_PIN_2  40
#define MOTOR_2_PIN_1  42
#define MOTOR_2_PIN_2  41

// ==============================
// สำหรับสตรีม MJPEG
// ==============================
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY     = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART         = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// ==============================
// เซิร์ฟเวอร์ 3 ตัว (แยกพอร์ต)
// ==============================
httpd_handle_t control_httpd = NULL;  // Server A (port 80)
httpd_handle_t cam_httpd     = NULL;  // Server B (port 81)
httpd_handle_t sensor_httpd  = NULL;  // Server C (port 82)

// ==============================
// HTML หน้าเว็บหลัก (สไตล์บาร์บี้ + พื้นหลังจาก wallpapercave)
// ==============================
//
// เพิ่มฟีเจอร์: "Temperature Data" ชิดซ้าย + กล่องเตือนเมื่อ avg >= 37
//
static const char PROGMEM INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>ESP32-CAM Barbie Style</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    /* รีเซ็ตพื้นฐาน */
    * {
      margin: 0; 
      padding: 0; 
      box-sizing: border-box;
    }

    body {
      font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
      text-align: center;
      /* ใช้รูปพื้นหลังจาก wallpapercave.com */
      background: url("https://wallpapercave.com/wp/wp2645719.jpg") no-repeat center center fixed;
      background-size: cover;
      color: #333;
      padding: 20px;
    }

    .container {
      max-width: 600px;
      margin: 0 auto;
      /* พื้นหลังชมพูอ่อน ๆ + ขอบ double */
      background: #fff0f6;
      border-radius: 16px;
      padding: 20px;
      border: 4px double #ff88c2;
      box-shadow: 0 4px 8px rgba(255,102,170,0.2);
      text-align: center; /* โดยรวม center ไว้ก่อน */
    }
    h1 {
      margin-bottom: 10px;
      font-size: 2rem;
      color: #ff48a2;
      text-shadow: 1px 1px 2px rgba(255, 90, 150, 0.4);
    }
    p {
      margin-bottom: 15px;
      font-size: 1rem;
      color: #666;
    }
    #photo {
      border: 3px solid #ff88c2;
      border-radius: 8px;
      max-width: 100%;
      height: auto;
      margin-bottom: 20px;
    }
    .button {
      background-color: #ff7eb9;
      border: 2px solid #ff48a2;
      color: #fff;
      padding: 12px 24px;
      text-align: center;
      display: inline-block;
      font-size: 1rem;
      margin: 6px 3px;
      cursor: pointer;
      user-select: none;
      border-radius: 8px;
      transition: background-color 0.3s ease, transform 0.2s ease;
    }
    .button:hover {
      background-color: #ff48a2;
      transform: scale(1.05);
    }
    /* ปุ่ม STOP ใช้สีชมพูออกแดง */
    .button.stop {
      background-color: #ff4f7a;
      border-color: #ff295f;
    }
    .button.stop:hover {
      background-color: #ff295f;
      transform: scale(1.05);
    }

    table.control-table {
      margin: 0 auto 20px auto;
      text-align: center;
    }
    table.control-table td {
      padding: 8px;
    }

    /* Title + WarningBox ชิดซ้าย */
    #tempTitle {
      display: flex;
      align-items: center;
      justify-content: flex-start; /* ให้ชิดซ้าย */
      gap: 10px;
      margin-bottom: 10px;
    }
    #tempTitle h2 {
      margin: 0;
      font-size: 1.4rem;
      color: #ff48a2;
    }
    #warningBox {
      display: none; /* เริ่มซ่อนก่อน */
      background-color: #ffcdd2; /* ชมพูอ่อน */
      color: #b71c1c; /* แดงเข้ม */
      padding: 6px 12px;
      border-radius: 8px;
      font-weight: bold;
      box-shadow: 0 2px 4px rgba(0,0,0,0.1);
      white-space: nowrap; /* กันตัดบรรทัด */
    }

    /* ตารางเซ็นเซอร์ */
    #sensorData table {
      border-collapse: collapse;
      margin: 0 auto;
      box-shadow: 0 2px 4px rgba(255,102,170, 0.2);
      background-color: #fff;
    }
    #sensorData table td {
      padding: 6px;
      width: 50px;
      text-align: center;
      border: 1px solid #ffd1e8;
      font-weight: bold;
      color: #fff; /* ตัวหนังสือขาวเมื่อพื้นหลังเป็นสีเข้ม */
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>ESP32-CAM Barbie Style</h1>
    <p>Device IP: %%IP_ADDR%%</p>

    <!-- ภาพสตรีมกล้อง MJPEG -->
    <img src="" id="photo">

    <!-- ตารางปุ่มควบคุม -->
    <table class="control-table">
      <tr>
        <td colspan="3">
          <button class="button" onmousedown="sendCmd('forward');"  ontouchstart="sendCmd('forward');"
                  onmouseup="sendCmd('stop');"    ontouchend="sendCmd('stop');">
            Forward
          </button>
        </td>
      </tr>
      <tr>
        <td>
          <button class="button" onmousedown="sendCmd('left');" ontouchstart="sendCmd('left');"
                  onmouseup="sendCmd('stop');"  ontouchend="sendCmd('stop');">
            Left
          </button>
        </td>
        <td>
          <button class="button stop" onmousedown="sendCmd('stop');" ontouchstart="sendCmd('stop');">
            Stop
          </button>
        </td>
        <td>
          <button class="button" onmousedown="sendCmd('right');" ontouchstart="sendCmd('right');"
                  onmouseup="sendCmd('stop');" ontouchend="sendCmd('stop');">
            Right
          </button>
        </td>
      </tr>
      <tr>
        <td colspan="3">
          <button class="button" onmousedown="sendCmd('backward');" ontouchstart="sendCmd('backward');"
                  onmouseup="sendCmd('stop');"    ontouchend="sendCmd('stop');">
            Backward
          </button>
        </td>
      </tr>
    </table>

    <!-- Title + WarningBox ชิดซ้าย -->
    <div id="tempTitle">
      <h2>Temperature Data</h2>
      <div id="warningBox">อุณหภูมิสูงขึ้น อาจเจอสิ่งมีชีวิต!!</div>
    </div>
    <div id="sensorData">Waiting for data...</div>
  </div>

  <script>
    // ฟังก์ชันส่งคำสั่งควบคุมรถ
    function sendCmd(cmd) {
      fetch("/action?go=" + cmd).catch(err => console.error(err));
    }

    // ฟังก์ชันกำหนดสีพื้นหลังตามอุณหภูมิ
    // (ต่ำกว่า 20 -> น้ำเงิน, 20-30 -> เขียว, มากกว่า 30 -> แดง)
    function getColorByTemp(temp) {
      if (temp < 20) return "blue";
      if (temp < 30) return "green";
      return "red";
    }

    window.onload = () => {
      const ip = location.hostname;
      // สตรีมกล้อง MJPEG จาก port 81
      document.getElementById("photo").src = `http://${ip}:81/stream`;

      // สตรีมเซนเซอร์ SSE จาก port 82
      const sensorURL = `http://${ip}:82/sensorStream`;
      const sensorSource = new EventSource(sensorURL);

      sensorSource.onmessage = function(event) {
        let dataArray = JSON.parse(event.data);

        // คำนวณอุณหภูมิเฉลี่ย
        let sum = 0;
        for (let v of dataArray) sum += v;
        let avg = sum / dataArray.length;

        // ถ้า avg >= 37 => โชว์ warningBox
        if (avg >= 37) {
          document.getElementById("warningBox").style.display = "inline-block";
        } else {
          document.getElementById("warningBox").style.display = "none";
        }

        // สร้างตาราง 8x8
        let table = "<table>";
        for (let i = 0; i < dataArray.length; i++) {
          if (i % 8 === 0) table += "<tr>";
          let tempVal = dataArray[i];
          let color   = getColorByTemp(tempVal);
          table += `<td style="background-color:${color};">${tempVal.toFixed(2)}°C</td>`;
          if (i % 8 === 7) table += "</tr>";
        }
        table += "</table>";
        document.getElementById("sensorData").innerHTML = table;
      };

      sensorSource.onerror = function(err) {
        console.error("SSE error:", err);
      };
    };
  </script>
</body>
</html>
)rawliteral";

// =======================================================
// ฟังก์ชันสร้างหน้า HTML โดยแทน IP ลงไปใน "%%IP_ADDR%%"
// =======================================================
String makeIndexPage() {
  String html = INDEX_HTML;
  html.replace("%%IP_ADDR%%", WiFi.localIP().toString());
  return html;
}

// ==============================
// HANDLER: หน้าเว็บหลัก (port 80)
// ==============================
static esp_err_t index_handler(httpd_req_t *req)
{
  String content = makeIndexPage();
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  return httpd_resp_send(req, content.c_str(), content.length());
}

// ==============================
// HANDLER: ควบคุมมอเตอร์ (port 80)
// ==============================
static esp_err_t cmd_handler(httpd_req_t *req)
{
  Serial.println("[CMD] Motor request");
  char *buf;
  size_t buf_len;
  char variable[32] = {0};

  buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    buf = (char *)malloc(buf_len);
    if(!buf) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
      if (httpd_query_key_value(buf, "go", variable, sizeof(variable)) == ESP_OK) {
        // variable = forward, left, right, backward, stop
      } else {
        free(buf);
        httpd_resp_send_404(req);
        return ESP_FAIL;
      }
    } else {
      free(buf);
      httpd_resp_send_404(req);
      return ESP_FAIL;
    }
    free(buf);
  } else {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }

  // ตัดสินใจตามคำสั่ง
  if (!strcmp(variable, "forward")) {
    Serial.println("Forward");
    digitalWrite(MOTOR_1_PIN_1, HIGH);
    digitalWrite(MOTOR_1_PIN_2, LOW);
    digitalWrite(MOTOR_2_PIN_1, HIGH);
    digitalWrite(MOTOR_2_PIN_2, LOW);
  } else if (!strcmp(variable, "left")) {
    Serial.println("Left");
    digitalWrite(MOTOR_1_PIN_1, LOW);
    digitalWrite(MOTOR_1_PIN_2, HIGH);
    digitalWrite(MOTOR_2_PIN_1, HIGH);
    digitalWrite(MOTOR_2_PIN_2, LOW);
  } else if (!strcmp(variable, "right")) {
    Serial.println("Right");
    digitalWrite(MOTOR_1_PIN_1, HIGH);
    digitalWrite(MOTOR_1_PIN_2, LOW);
    digitalWrite(MOTOR_2_PIN_1, LOW);
    digitalWrite(MOTOR_2_PIN_2, HIGH);
  } else if (!strcmp(variable, "backward")) {
    Serial.println("Backward");
    digitalWrite(MOTOR_1_PIN_1, LOW);
    digitalWrite(MOTOR_1_PIN_2, HIGH);
    digitalWrite(MOTOR_2_PIN_1, LOW);
    digitalWrite(MOTOR_2_PIN_2, HIGH);
  } else if (!strcmp(variable, "stop")) {
    Serial.println("Stop");
    digitalWrite(MOTOR_1_PIN_1, LOW);
    digitalWrite(MOTOR_1_PIN_2, LOW);
    digitalWrite(MOTOR_2_PIN_1, LOW);
    digitalWrite(MOTOR_2_PIN_2, LOW);
  } else {
    Serial.println("[CMD] Unknown command");
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

// ==============================
// HANDLER: สตรีมกล้อง (port 81)
// ==============================
static esp_err_t stream_handler(httpd_req_t *req)
{
  esp_err_t res;
  camera_fb_t *fb   = NULL;
  size_t _jpg_buf_len;
  uint8_t *_jpg_buf = NULL;
  char part_buf[64];

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) return res;

  while(true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("[STREAM] Camera capture failed");
      res = ESP_FAIL;
    } else {
      if (fb->format != PIXFORMAT_JPEG) {
        bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
        esp_camera_fb_return(fb);
        fb = NULL;
        if (!jpeg_converted) {
          Serial.println("[STREAM] JPEG compression failed");
          res = ESP_FAIL;
        }
      } else {
        _jpg_buf_len = fb->len;
        _jpg_buf     = fb->buf;
      }
    }

    if (res == ESP_OK) {
      size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }

    if (fb) {
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if (_jpg_buf) {
      free(_jpg_buf);
      _jpg_buf = NULL;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
    if (res != ESP_OK) break;
  }

  return res;
}

// ==============================
// HANDLER: สตรีมเซนเซอร์ SSE (port 82)
// ==============================
static esp_err_t sensor_stream_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "text/event-stream; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_set_hdr(req, "Connection", "keep-alive");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  float pixels[AMG88xx_PIXEL_ARRAY_SIZE];

  while(true) {
    amg.readPixels(pixels);

    // สร้าง JSON
    String json = "[";
    for (int i = 0; i < AMG88xx_PIXEL_ARRAY_SIZE; i++) {
      json += String(pixels[i], 2);
      if (i < AMG88xx_PIXEL_ARRAY_SIZE - 1) json += ",";
    }
    json += "]";

    // ส่งเป็น SSE
    String sseMsg = "data: " + json + "\n\n";
    esp_err_t res = httpd_resp_send_chunk(req, sseMsg.c_str(), sseMsg.length());
    if (res != ESP_OK) {
      // ถ้า client ปิด connection
      break;
    }

    // หน่วง 1 วินาที
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  httpd_resp_send_chunk(req, NULL, 0);
  return ESP_OK;
}

// ==============================
// สร้างเซิร์ฟเวอร์ 3 ตัว
// ==============================
void startServers()
{
  // Server A (port 80) - หน้าเว็บ + ควบคุมมอเตอร์
  {
    httpd_config_t configA = HTTPD_DEFAULT_CONFIG();
    configA.server_port    = 80;
    configA.task_priority  = tskIDLE_PRIORITY + 3; // Priority สูง
    if (httpd_start(&control_httpd, &configA) == ESP_OK)
    {
      // หน้าเว็บ
      httpd_uri_t index_uri = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = index_handler,
        .user_ctx = NULL
      };
      httpd_register_uri_handler(control_httpd, &index_uri);

      // ควบคุมมอเตอร์
      httpd_uri_t cmd_uri = {
        .uri      = "/action",
        .method   = HTTP_GET,
        .handler  = cmd_handler,
        .user_ctx = NULL
      };
      httpd_register_uri_handler(control_httpd, &cmd_uri);
    }
  }

  // Server B (port 81) - สตรีมกล้อง
  {
    httpd_config_t configB = HTTPD_DEFAULT_CONFIG();
    configB.server_port    = 81;
    configB.ctrl_port      = 83;
    configB.task_priority  = tskIDLE_PRIORITY + 2;
    if (httpd_start(&cam_httpd, &configB) == ESP_OK)
    {
      httpd_uri_t stream_uri = {
        .uri      = "/stream",
        .method   = HTTP_GET,
        .handler  = stream_handler,
        .user_ctx = NULL
      };
      httpd_register_uri_handler(cam_httpd, &stream_uri);
    }
  }

  // Server C (port 82) - สตรีมเซนเซอร์ SSE
  {
    httpd_config_t configC = HTTPD_DEFAULT_CONFIG();
    configC.server_port    = 82;
    configC.ctrl_port      = 84;
    configC.task_priority  = tskIDLE_PRIORITY + 1;
    if (httpd_start(&sensor_httpd, &configC) == ESP_OK)
    {
      httpd_uri_t sensor_uri = {
        .uri      = "/sensorStream",
        .method   = HTTP_GET,
        .handler  = sensor_stream_handler,
        .user_ctx = NULL
      };
      httpd_register_uri_handler(sensor_httpd, &sensor_uri);
    }
  }
}

// ==============================
// setup()
// ==============================
void setup()
{
  Serial.begin(115200);

  // ตั้งขา OUTPUT มอเตอร์
  pinMode(MOTOR_1_PIN_1, OUTPUT);
  pinMode(MOTOR_1_PIN_2, OUTPUT);
  pinMode(MOTOR_2_PIN_1, OUTPUT);
  pinMode(MOTOR_2_PIN_2, OUTPUT);

  // ตั้งค่ากล้อง
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size   = FRAMESIZE_VGA;
    config.jpeg_quality = 10;
    config.fb_count     = 2;
  } else {
    config.frame_size   = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count     = 1;
  }

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Camera init failed!");
    return;
  }

  // เชื่อมต่อ WiFi
  WiFi.begin(ssid, password);
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi Connection Failed!");
    return;
  }
  Serial.println("\nWiFi connected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  // เริ่มเซ็นเซอร์ AMG88xx
  Wire.begin(47, 14);
  if(!amg.begin(0x68)) {
    Serial.println("AMG88xx not detected!");
    while(1) { delay(100); }
  }

  // สตาร์ทเซิร์ฟเวอร์ 3 ตัว
  startServers();

  Serial.println("All servers started...");
}

// ==============================
// loop()
// ==============================
void loop()
{
  // ไม่ต้องทำอะไรใน loop เพราะทุกอย่างอยู่ใน Handler
}
