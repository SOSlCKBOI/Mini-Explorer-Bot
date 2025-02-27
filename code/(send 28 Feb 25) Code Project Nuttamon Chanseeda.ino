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
const char *ssid = "Pony";
const char *password = "knuttamon0002";

// ==============================
// ตั้งค่าขา GPIO กล้อง
// ==============================
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 15
#define SIOD_GPIO_NUM 4
#define SIOC_GPIO_NUM 5

#define Y9_GPIO_NUM 16
#define Y8_GPIO_NUM 17
#define Y7_GPIO_NUM 18
#define Y6_GPIO_NUM 12
#define Y5_GPIO_NUM 10
#define Y4_GPIO_NUM 8
#define Y3_GPIO_NUM 9
#define Y2_GPIO_NUM 11
#define VSYNC_GPIO_NUM 6
#define HREF_GPIO_NUM 7
#define PCLK_GPIO_NUM 13

// ==============================
// ตั้งค่าขา GPIO มอเตอร์
// ==============================
#define MOTOR_1_PIN_1 39
#define MOTOR_1_PIN_2 40
#define MOTOR_2_PIN_1 42
#define MOTOR_2_PIN_2 41

// ==============================
// สำหรับสตรีม MJPEG
// ==============================
#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// ==============================
// เซิร์ฟเวอร์ 3 ตัว
// ==============================
httpd_handle_t control_httpd = NULL;  // Server A (port 80)
httpd_handle_t cam_httpd = NULL;      // Server B (port 81)
httpd_handle_t sensor_httpd = NULL;   // Server C (port 82)

// ==============================
// HTML หน้าเว็บหลัก
// ==============================
//
// สังเกตว่าจะเรียกกล้อง :81/stream และเซนเซอร์ :82/sensorStream
//
static const char PROGMEM INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8" />
  <title>ESP32-CAM Robot</title>
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <style>
    body { font-family: Arial; text-align: center; margin:0px auto; padding-top: 30px;}
    table { margin-left: auto; margin-right: auto; }
    td { padding: 8px; }
    .button {
      background-color: #2f4468;
      border: none;
      color: white;
      padding: 10px 20px;
      text-align: center;
      text-decoration: none;
      display: inline-block;
      font-size: 18px;
      margin: 6px 3px;
      cursor: pointer;
      user-select: none;
    }
    img {
      width: auto;
      max-width: 100%;
      height: auto;
    }
  </style>
</head>
<body>
  <h1>ESP32-CAM Robot</h1>
  <p>Device IP: %%IP_ADDR%%</p>

  <img src="" id="photo" />

  <table>
    <tr>
      <td colspan="3" align="center">
        <button class="button" onmousedown="sendCmd('forward');"  ontouchstart="sendCmd('forward');"
                onmouseup="sendCmd('stop');"    ontouchend="sendCmd('stop');">
          Forward
        </button>
      </td>
    </tr>
    <tr>
      <td align="center">
        <button class="button" onmousedown="sendCmd('left');" ontouchstart="sendCmd('left');"
                onmouseup="sendCmd('stop');"  ontouchend="sendCmd('stop');">
          Left
        </button>
      </td>
      <td align="center">
        <button class="button" onmousedown="sendCmd('stop');" ontouchstart="sendCmd('stop');">
          Stop
        </button>
      </td>
      <td align="center">
        <button class="button" onmousedown="sendCmd('right');" ontouchstart="sendCmd('right');"
                onmouseup="sendCmd('stop');" ontouchend="sendCmd('stop');">
          Right
        </button>
      </td>
    </tr>
    <tr>
      <td colspan="3" align="center">
        <button class="button" onmousedown="sendCmd('backward');" ontouchstart="sendCmd('backward');"
                onmouseup="sendCmd('stop');"    ontouchend="sendCmd('stop');">
          Backward
        </button>
      </td>
    </tr>
  </table>

  <h2>Temperature Data</h2>
  <div id="sensorData">Waiting for data...</div>

  <script>
    function sendCmd(cmd) {
      fetch("/action?go=" + cmd).catch(err => console.error(err));
    }

    window.onload = () => {
      // อ่าน IP (hostname) จาก URL ปัจจุบัน
      const ip = location.hostname;
      // สตรีมภาพที่ :81
      document.getElementById("photo").src = `http://${ip}:81/stream`;

      // สตรีมเซนเซอร์ที่ :82
      const sensorURL = `http://${ip}:82/sensorStream`;
      const sensorSource = new EventSource(sensorURL);

      sensorSource.onmessage = function(event) {
        let dataArray = JSON.parse(event.data);
        let table = "<table border='1' style='margin:auto;'>";
        for (let i = 0; i < dataArray.length; i++) {
          if (i % 8 === 0) table += "<tr>";
          table += `<td style="padding:5px;">${dataArray[i].toFixed(2)}°C</td>`;
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

// ==============================
// สร้างหน้า HTML โดยแทน IP ลงไป
// ==============================
String makeIndexPage() {
  String html = INDEX_HTML;
  html.replace("%%IP_ADDR%%", WiFi.localIP().toString());
  return html;
}

// ==============================
// HANDLER: หน้าเว็บหลัก (port 80)
// ==============================
static esp_err_t index_handler(httpd_req_t *req) {
  String content = makeIndexPage();
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  return httpd_resp_send(req, content.c_str(), content.length());
}

// ==============================
// HANDLER: ควบคุมมอเตอร์ (port 80)
// ==============================
static esp_err_t cmd_handler(httpd_req_t *req) {
  Serial.println("[CMD] Motor request");
  char *buf;
  size_t buf_len;
  char variable[32] = { 0 };

  buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    buf = (char *)malloc(buf_len);
    if (!buf) {
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
static esp_err_t stream_handler(httpd_req_t *req) {
  esp_err_t res;
  camera_fb_t *fb = NULL;
  size_t _jpg_buf_len;
  uint8_t *_jpg_buf = NULL;
  char part_buf[64];

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) return res;

  while (true) {
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
        _jpg_buf = fb->buf;
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
static esp_err_t sensor_stream_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/event-stream; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_set_hdr(req, "Connection", "keep-alive");
  // แก้ปัญหา CORS (ถ้าจำเป็น)
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  float pixels[AMG88xx_PIXEL_ARRAY_SIZE];

  while (true) {
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
      // ถ้า client ปิด connection ก็หลุด
      break;
    }

    // หน่วงหน่อย
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  httpd_resp_send_chunk(req, NULL, 0);
  return ESP_OK;
}

// ==============================
// เริ่มต้น Server A,B,C
// ==============================
void startServers() {
  // Server A (port 80) - หน้าเว็บ + ควบคุมมอเตอร์
  {
    httpd_config_t configA = HTTPD_DEFAULT_CONFIG();
    configA.server_port = 80;
    configA.task_priority = tskIDLE_PRIORITY + 3;  // Priority สูง
    if (httpd_start(&control_httpd, &configA) == ESP_OK) {
      // หน้าเว็บ
      httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL
      };
      httpd_register_uri_handler(control_httpd, &index_uri);

      // ควบคุมมอเตอร์
      httpd_uri_t cmd_uri = {
        .uri = "/action",
        .method = HTTP_GET,
        .handler = cmd_handler,
        .user_ctx = NULL
      };
      httpd_register_uri_handler(control_httpd, &cmd_uri);
    }
  }

  // Server B (port 81) - สตรีมกล้อง
  {
    httpd_config_t configB = HTTPD_DEFAULT_CONFIG();
    configB.server_port = 81;
    configB.ctrl_port = 83;
    configB.task_priority = tskIDLE_PRIORITY + 2;  // กลาง
    if (httpd_start(&cam_httpd, &configB) == ESP_OK) {
      // สตรีมกล้อง MJPEG
      httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL
      };
      httpd_register_uri_handler(cam_httpd, &stream_uri);
    }
  }

  // Server C (port 82) - สตรีมเซนเซอร์ SSE
  {
    httpd_config_t configC = HTTPD_DEFAULT_CONFIG();
    configC.server_port = 82;
    configC.ctrl_port = 84;
    configC.task_priority = tskIDLE_PRIORITY + 1;  // ต่ำ
    if (httpd_start(&sensor_httpd, &configC) == ESP_OK) {
      httpd_uri_t sensor_uri = {
        .uri = "/sensorStream",
        .method = HTTP_GET,
        .handler = sensor_stream_handler,
        .user_ctx = NULL
      };
      httpd_register_uri_handler(sensor_httpd, &sensor_uri);
    }
  }
}

// ==============================
// setup()
// ==============================
void setup() {
  Serial.begin(115200);

  // ตั้งขา OUTPUT มอเตอร์
  pinMode(MOTOR_1_PIN_1, OUTPUT);
  pinMode(MOTOR_1_PIN_2, OUTPUT);
  pinMode(MOTOR_2_PIN_1, OUTPUT);
  pinMode(MOTOR_2_PIN_2, OUTPUT);

  // ตั้งค่ากล้อง
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
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
  if (!amg.begin(0x68)) {
    Serial.println("AMG88xx not detected!");
    while (1) { delay(100); }
  }

  // สตาร์ทเซิร์ฟเวอร์ 3 ตัว
  startServers();

  Serial.println("All servers started...");
}

// ==============================
// loop()
// ==============================
void loop() {
  // ไม่ต้องทำอะไร เพราะใช้ HTTPD + SSE
}
