#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include "DFRobot_DHT20.h"
#include <ESPmDNS.h>
#include <U8g2lib.h>

const char* STA_SSID = "NOME_WIFI";
const char* STA_PASS = "PASSWORD_WIFI";

const char* AP_SSID = "CubeSat_AP";

WebServer server(80);
DFRobot_DHT20 dht20;
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0);

SemaphoreHandle_t mutex;

struct Data {
  float t = NAN;
  float h = NAN;
} data;

void taskSensor(void*);
void taskDisplay(void*);
void taskWeb(void*);
void startWiFi();
void handleData();
void handleRoot();

void setup() {
  Serial.begin(115200);
  Wire.begin();
  display.begin();

  mutex = xSemaphoreCreateMutex();

  dht20.begin();
  startWiFi();

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();

  xTaskCreate(taskSensor, "sensor", 4096, NULL, 2, NULL);
  xTaskCreate(taskDisplay, "display", 4096, NULL, 1, NULL);
  xTaskCreate(taskWeb, "web", 4096, NULL, 1, NULL);
}

void loop() {
  vTaskDelay(portMAX_DELAY);
}

void taskSensor(void*) {
  while (true) {
    float t = dht20.getTemperature();
    float h = dht20.getHumidity() * 100;

    if (!isnan(t) && !isnan(h)) {
      xSemaphoreTake(mutex, portMAX_DELAY);
      data.t = t;
      data.h = h;
      xSemaphoreGive(mutex);

      Serial.printf("T: %.1f  H: %.1f\n", t, h);
    }

    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

void taskDisplay(void*) {
  while (true) {
    Data d;

    xSemaphoreTake(mutex, portMAX_DELAY);
    d = data;
    xSemaphoreGive(mutex);

    display.clearBuffer();
    display.setFont(u8g2_font_6x10_tr);

    display.drawStr(0, 10, WiFi.getMode() == WIFI_AP ? "AP MODE" : "STA MODE");

    char buf[32];
    sprintf(buf, "T: %.1f C", d.t);
    display.drawStr(0, 30, buf);

    sprintf(buf, "H: %.1f %%", d.h);
    display.drawStr(0, 50, buf);

    display.sendBuffer();

    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

void taskWeb(void*) {
  while (true) {
    server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void startWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(STA_SSID, STA_PASS);

  Serial.print("Connessione WiFi");

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(500);
    Serial.print(".");
  }

    if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnesso WiFi STA");
    Serial.println(WiFi.localIP());

    if (MDNS.begin("cubesat")) {
      Serial.println("mDNS avviato: http://cubesat.local");
    }
  } else {
    Serial.println("\nAvvio Access Point...");

    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID);

    IPAddress IP(192,168,4,1);
    IPAddress GW(192,168,4,1);
    IPAddress MASK(255,255,255,0);
    WiFi.softAPConfig(IP, GW, MASK);

    Serial.println(WiFi.softAPIP());

    if (MDNS.begin("cubesat")) {
      Serial.println("mDNS avviato: http://cubesat.local");
    }
  }
}

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>CubeSat Dashboard</title>
  <style>
    body {
      font-family: Arial;
      background: #0f172a;
      color: #e2e8f0;
      text-align: center;
      margin: 0;
      padding: 0;
    }
    .card {
      background: #1e293b;
      margin: 20px auto;
      padding: 20px;
      border-radius: 16px;
      width: 90%;
      max-width: 320px;
      box-shadow: 0 10px 20px rgba(0,0,0,0.3);
    }
    h1 {
      margin-top: 20px;
      font-size: 22px;
    }
    .value {
      font-size: 32px;
      margin: 10px 0;
      color: #38bdf8;
    }
    .label {
      font-size: 14px;
      color: #94a3b8;
    }
    .btn {
      display: inline-block;
      margin-top: 15px;
      padding: 10px 15px;
      background: #38bdf8;
      color: #0f172a;
      border-radius: 10px;
      text-decoration: none;
      font-weight: bold;
    }
  </style>
</head>
<body>

  <h1>CubeSat Dashboard</h1>

  <div class="card">
    <div class="label">Temperatura</div>
    <div class="value" id="t">-- °C</div>

    <div class="label">Umidità</div>
    <div class="value" id="h">-- %</div>

    <a class="btn" href="/data">JSON DATA</a>
  </div>

<script>
async function update() {
  const res = await fetch('/data');
  const data = await res.json();

  document.getElementById('t').innerText = data.t.toFixed(1) + " °C";
  document.getElementById('h').innerText = data.h.toFixed(1) + " %";
}

setInterval(update, 2000);
update();
</script>

</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

void handleData() {
  Data d;

  xSemaphoreTake(mutex, portMAX_DELAY);
  d = data;
  xSemaphoreGive(mutex);

  String json = "{";
  json += "\"t\":" + String(d.t) + ",";
  json += "\"h\":" + String(d.h);
  json += "}";

  server.send(200, "application/json", json);
}