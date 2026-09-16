/*
 * SIMQ — Servidor local
 *
 * La ESP32 se conecta al WiFi y publica sus lecturas en
 *   http://<IP>/datos     ->  JSON con las cuatro variables
 *   http://<IP>/          ->  página mínima de verificación
 *
 * No requiere Supabase. Sirve para ver el sensor en el dashboard
 * de inmediato, con la interfaz abierta en el mismo computador.
 *
 * Librerías: Adafruit SHT31, BH1750, ArduinoJson
 */

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_SHT31.h>
#include <BH1750.h>
#include <ArduinoJson.h>
#include <math.h>

/* ================= CONFIGURACIÓN ================= */
const char* WIFI_SSID = "TU_RED";
const char* WIFI_PASS = "TU_CLAVE";

const int PIN_SDA = 8;
const int PIN_SCL = 9;
const int PIN_MIC = 3;

const float DB_OFFSET = 26.0;   // ajustar contra sonómetro
/* ================================================= */

WebServer servidor(80);
Adafruit_SHT31 sht = Adafruit_SHT31();
BH1750 lux;

bool haySHT = false, hayBH = false;
uint16_t micBase = 2048;

float ultTemp = NAN, ultHum = NAN, ultLux = NAN, ultDb = 0;
uint32_t ultimaLectura = 0;

float leerRuido() {
  uint64_t acc = 0;
  for (uint16_t i = 0; i < 800; i++) {
    int16_t m = (int16_t)analogRead(PIN_MIC) - (int16_t)micBase;
    acc += (uint32_t)(m * m);
    delayMicroseconds(60);
  }
  float rms = sqrt((float)acc / 800.0);
  if (rms < 1.0) rms = 1.0;
  float db = 20.0 * log10(rms) + DB_OFFSET;
  return db < 0 ? 0 : (db > 130 ? 130 : db);
}

void muestrear() {
  ultTemp = haySHT ? sht.readTemperature() : NAN;
  ultHum  = haySHT ? sht.readHumidity()    : NAN;
  ultLux  = hayBH  ? lux.readLightLevel()  : NAN;
  ultDb   = leerRuido();
}

/* Sin esta cabecera el navegador rechaza la petición */
void cors() {
  servidor.sendHeader("Access-Control-Allow-Origin", "*");
  servidor.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
  servidor.sendHeader("Access-Control-Allow-Headers", "*");
}

void rutaDatos() {
  cors();
  StaticJsonDocument<192> doc;
  doc["device_id"] = "esp32-quirofano-01";
  doc["ts"]        = millis();
  if (!isnan(ultTemp)) doc["temp"] = round(ultTemp * 100) / 100.0;
  if (!isnan(ultHum))  doc["hum"]  = round(ultHum  * 100) / 100.0;
  if (!isnan(ultLux))  doc["lux"]  = round(ultLux  * 10)  / 10.0;
  doc["ruido"] = round(ultDb * 10) / 10.0;

  String salida;
  serializeJson(doc, salida);
  servidor.send(200, "application/json", salida);
}

void rutaRaiz() {
  cors();
  char buf[420];
  snprintf(buf, sizeof(buf),
    "<!doctype html><meta charset=utf-8>"
    "<body style='font-family:system-ui;padding:24px;line-height:1.6'>"
    "<h2>SIMQ &mdash; ESP32 activa</h2>"
    "<p>SHT31: %s<br>BH1750: %s</p>"
    "<p>T %.2f C &middot; HR %.1f %% &middot; %.0f lux &middot; %.1f dB</p>"
    "<p><a href='/datos'>/datos</a></p></body>",
    haySHT ? "OK" : "no responde",
    hayBH  ? "OK" : "no responde",
    ultTemp, ultHum, ultLux, ultDb);
  servidor.send(200, "text/html", buf);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Wire.begin(PIN_SDA, PIN_SCL);
  haySHT = sht.begin(0x44);
  hayBH  = lux.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
  Serial.println(haySHT ? "SHT31  OK" : "SHT31  no responde");
  Serial.println(hayBH  ? "BH1750 OK" : "BH1750 no responde");

  analogReadResolution(12);
  uint32_t suma = 0;
  for (uint16_t i = 0; i < 1000; i++) { suma += analogRead(PIN_MIC); delayMicroseconds(200); }
  micBase = suma / 1000;

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Conectando");
  while (WiFi.status() != WL_CONNECTED) { delay(400); Serial.print("."); }

  Serial.println();
  Serial.println("=====================================");
  Serial.print(  "  IP: ");
  Serial.println(WiFi.localIP());
  Serial.print(  "  Datos: http://");
  Serial.print(WiFi.localIP());
  Serial.println("/datos");
  Serial.println("=====================================");

  servidor.on("/", rutaRaiz);
  servidor.on("/datos", rutaDatos);
  servidor.onNotFound([](){ cors(); servidor.send(404, "text/plain", "no existe"); });
  servidor.begin();

  muestrear();
}

void loop() {
  servidor.handleClient();

  if (millis() - ultimaLectura >= 1000) {
    ultimaLectura = millis();
    muestrear();
  }
}
