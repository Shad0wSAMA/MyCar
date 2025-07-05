#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <MPU9250_asukiaaa.h>
#include <ESPAsyncWebServer.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

MPU9250_asukiaaa mySensor;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

const char* ssid = "Test1";
const char* password = "12345678";

float current_angle = 0;
int last_gz = 0;
unsigned long last_time = 0;

const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>虚拟罗盘</title>
  <style>
    body { font-family: sans-serif; text-align: center; margin-top: 40px; }
    canvas { border: 1px solid black; margin-bottom: 20px; }

    .control-grid {
      display: grid;
      grid-template-areas:
        ".    up    ."
        "left stop right"
        ".   down   .";
      grid-gap: 10px;
      justify-content: center;
      align-items: center;
    }

    .control-grid button {
      font-size: 20px;
      padding: 20px 30px;
      min-width: 100px;
      min-height: 60px;
    }

    .up { grid-area: up; }
    .down { grid-area: down; }
    .left { grid-area: left; }
    .right { grid-area: right; }
    .stop { grid-area: stop; background-color: red; color: white; }
  </style>
</head>
<body>
  <h1>ESP32 虚拟罗盘</h1>
  <canvas id="compass" width="200" height="200"></canvas>
  <p>角度: <span id="angle">0</span>°</p>

  <div class="control-grid">
    <button class="up" onclick="sendCommand('FORWARD')">↑ 前进</button>
    <button class="left" onclick="sendCommand('LEFT')">← 左转</button>
    <button class="stop" onclick="sendCommand('STOP')">● 停止</button>
    <button class="right" onclick="sendCommand('RIGHT')">→ 右转</button>
    <button class="down" onclick="sendCommand('BACKWARD')">↓ 后退</button>
  </div>

  <p><button onclick="calibrate()">校准为0°</button></p>

  <script>
    const canvas = document.getElementById('compass');
    const ctx = canvas.getContext('2d');
    const angleLabel = document.getElementById('angle');

    function drawCompass(angle) {
      ctx.clearRect(0, 0, 200, 200);
      ctx.save();
      ctx.translate(100, 100);
      ctx.rotate(angle * Math.PI / 180);
      ctx.beginPath();
      ctx.moveTo(0, -80);
      ctx.lineTo(0, 10);
      ctx.strokeStyle = "red";
      ctx.lineWidth = 4;
      ctx.stroke();
      ctx.restore();
    }

    const ws = new WebSocket(`ws://${location.host}/ws`);
    ws.onmessage = (event) => {
      const angle = parseFloat(event.data) % 360;
      angleLabel.textContent = angle.toFixed(1);
      drawCompass(angle);
    };

    function sendCommand(cmd) {
      ws.send(cmd);
    }

    function calibrate() {
      ws.send("CALIBRATE");
    }
  </script>
</body>
</html>
)rawliteral";

void notifyClients() {
  String msg = String(current_angle);
  ws.textAll(msg);
}

void moveForward() {
  ledcWrite(0, 200); // AIA
  ledcWrite(1, 0);   // AIB
  ledcWrite(2, 200); // BIA
  ledcWrite(3, 0);   // BIB
}

void moveBackward() {
  ledcWrite(0, 0);
  ledcWrite(1, 200);
  ledcWrite(2, 0);
  ledcWrite(3, 200);
}

void turnLeft() {
  ledcWrite(0, 0);   // A 停
  ledcWrite(1, 200);
  ledcWrite(2, 200); // B 转
  ledcWrite(3, 0);
}

void turnRight() {
  ledcWrite(0, 200); // A 转
  ledcWrite(1, 0);
  ledcWrite(2, 0);   // B 停
  ledcWrite(3, 200);
}

void stopMotors() {
  ledcWrite(0, 0);
  ledcWrite(1, 0);
  ledcWrite(2, 0);
  ledcWrite(3, 0);
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  Wire.begin();
  mySensor.setWire(&Wire);
  mySensor.beginGyro();

  ledcSetup(0, 1000, 8); ledcAttachPin(5, 0);   // AIA
  ledcSetup(1, 1000, 8); ledcAttachPin(17, 1);  // AIB
  ledcSetup(2, 1000, 8); ledcAttachPin(4, 2);   // BIA
  ledcSetup(3, 1000, 8); ledcAttachPin(16, 3);  // BIB

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected. IP: " + WiFi.localIP().toString());

  // WebSocket处理器
  ws.onEvent([](AsyncWebSocket * server, AsyncWebSocketClient * client,
                AwsEventType type, void * arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_DATA) {
    AwsFrameInfo* info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      String msg = "";
      for (size_t i = 0; i < len; i++) {
        msg += (char)data[i];
      }
      msg.trim();
      if (msg == "CALIBRATE") {
        current_angle = 0;
        Serial.println("校准请求已处理，角度归零");
      } else if (msg == "FORWARD") {
        Serial.println("前进");
        moveForward();
      } else if (msg == "BACKWARD") {
        Serial.println("后退");
        moveBackward();
      } else if (msg == "LEFT") {
        Serial.println("左转");
        turnLeft();
      } else if (msg == "RIGHT") {
        Serial.println("右转");
        turnRight();
      } else if (msg == "STOP") {
        Serial.println("停止");
        stopMotors();
      }

      
    }
  }
  });
  server.addHandler(&ws);

  // 静态网页
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", HTML_PAGE);
  });

  server.begin();
}

void loop() {
  static unsigned long gyro_interval = 10;
  static unsigned long last_update = 0;
  static unsigned long web_update_interval = 100;
  static float last_sent_angle = 0;
  static const float ANGLE_THRESHOLD = 1.0;

  if (millis() - last_time >= gyro_interval) {
    last_time = millis();

    mySensor.gyroUpdate();
    float gz = mySensor.gyroZ() + 1.5;
    gz = 0.2 * last_gz + 0.8 * gz;
    last_gz = gz;
    if (abs(gz) < 0.1) gz = 0;
    current_angle += gyro_interval * gz / 1000.0;
  }
  if (millis() - last_update >= web_update_interval) {
    if (abs(current_angle - last_sent_angle) > ANGLE_THRESHOLD) {
      last_sent_angle = current_angle;
      last_update = millis();
      notifyClients();
    }
}
}




