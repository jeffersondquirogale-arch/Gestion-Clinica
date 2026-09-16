/*
 * SIMQ — Monitoreo ambiental de sala de cirugía
 * Placa: ESP32-C3 mini
 *
 * Sensores:
 *   SHT31   I2C 0x44   temperatura y humedad
 *   BH1750  I2C 0x23   iluminación
 *   MAX9814 ADC        ruido
 *
 * Publica una lectura por segundo en la tabla `lecturas` de Supabase.
 *
 * Librerías necesarias:
 *   Adafruit SHT31 Library
 *   BH1750 (Christopher Laws)
 *   ArduinoJson
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_SHT31.h>
#include <BH1750.h>
#include <ArduinoJson.h>
#include <math.h>

/* ==================== CONFIGURACIÓN ==================== */

const char* WIFI_SSID     = "IPHONE";
const char* WIFI_PASS     = "Tarayjack1226";

const char* SUPABASE_URL  = "https://vpdtereegwgifeeomtsx.supabase.co";
const char* SUPABASE_KEY  = "sb_publishable_mWh5yJUPuomCYHLY2Q-ezw_wHVadiIh";
const char* DEVICE_ID     = "esp32-quirofano-01";

/* Pines */
const int PIN_SDA  = 8;
const int PIN_SCL  = 9;
const int PIN_MIC  = 3;   // ADC1

/* Muestreo */
const uint32_t INTERVALO_MS   = 1000;   // una publicación por segundo
const uint16_t MIC_MUESTRAS   = 800;    // muestras por ventana de ruido
const float    DB_OFFSET      = 26.0;   // AJUSTAR contra un sonómetro

/* ======================================================= */

Adafruit_SHT31 sht = Adafruit_SHT31();
BH1750 lux;

bool haySHT = false;
bool hayBH  = false;

uint32_t ultimoEnvio = 0;
uint16_t micLineaBase = 2048;   // se calcula en el arranque

void conectarWiFi() {
  Serial.print("Conectando a ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint8_t intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 40) {
    delay(500);
    Serial.print(".");
    intentos++;
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Conectado. IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Sin WiFi. Se reintenta en el bucle principal.");
  }
}

/* Calcula el punto medio del micrófono con la sala en silencio */
void calibrarLineaBase() {
  uint32_t suma = 0;
  for (uint16_t i = 0; i < 1000; i++) {
    suma += analogRead(PIN_MIC);
    delayMicroseconds(200);
  }
  micLineaBase = suma / 1000;
  Serial.print("Línea base del micrófono: ");
  Serial.println(micLineaBase);
}

/*
 * Ventana de ~50 ms. Se calcula el RMS de la señal centrada y se
 * convierte a escala logarítmica. DB_OFFSET traslada esa escala a
 * dB SPL y debe ajustarse contra un sonómetro calibrado.
 */
float leerRuido() {
  uint64_t acumulado = 0;

  for (uint16_t i = 0; i < MIC_MUESTRAS; i++) {
    int16_t m = (int16_t)analogRead(PIN_MIC) - (int16_t)micLineaBase;
    acumulado += (uint32_t)(m * m);
    delayMicroseconds(60);
  }

  float rms = sqrt((float)acumulado / MIC_MUESTRAS);
  if (rms < 1.0) rms = 1.0;

  float db = 20.0 * log10(rms) + DB_OFFSET;
  if (db < 0)   db = 0;
  if (db > 130) db = 130;
  return db;
}

bool publicar(float temp, float hum, float ilum, float ruido) {
  if (WiFi.status() != WL_CONNECTED) return false;

  String endpoint = String(SUPABASE_URL) + "/rest/v1/lecturas";

  StaticJsonDocument<256> doc;
  doc["device_id"] = DEVICE_ID;
  if (!isnan(temp)) doc["temp"]  = round(temp  * 100) / 100.0;
  if (!isnan(hum))  doc["hum"]   = round(hum   * 100) / 100.0;
  if (!isnan(ilum)) doc["lux"]   = round(ilum  * 10)  / 10.0;
  doc["ruido"] = round(ruido * 10) / 10.0;

  String cuerpo;
  serializeJson(doc, cuerpo);

  HTTPClient http;
  http.begin(endpoint);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Prefer", "return=minimal");

  int codigo = http.POST(cuerpo);
  http.end();

  if (codigo < 200 || codigo >= 300) {
    Serial.print("POST falló, código ");
    Serial.println(codigo);
    return false;
  }
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println("\nSIMQ — iniciando");

  Wire.begin(PIN_SDA, PIN_SCL);

  haySHT = sht.begin(0x44);
  Serial.println(haySHT ? "SHT31 detectado" : "SHT31 NO detectado");

  hayBH = lux.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
  Serial.println(hayBH ? "BH1750 detectado" : "BH1750 NO detectado");

  analogReadResolution(12);
  calibrarLineaBase();

  conectarWiFi();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    conectarWiFi();
    delay(1000);
    return;
  }

  if (millis() - ultimoEnvio < INTERVALO_MS) return;
  ultimoEnvio = millis();

  float temp = haySHT ? sht.readTemperature() : NAN;
  float hum  = haySHT ? sht.readHumidity()    : NAN;
  float ilum = hayBH  ? lux.readLightLevel()  : NAN;
  float db   = leerRuido();

  Serial.printf("T %.2f C | HR %.1f %% | %.0f lux | %.1f dB\n",
                temp, hum, ilum, db);

  publicar(temp, hum, ilum, db);
}

/*
 * PENDIENTE
 *
 * 1. Buffer offline. Si el WiFi cae, hoy la lectura se pierde. Guardar en
 *    un arreglo circular en RAM (o en microSD) y reenviar al reconectar.
 * 2. Calibración de DB_OFFSET contra sonómetro.
 * 3. Ponderación A para el ruido. El valor actual es sin ponderar.
 * 4. Reintento con espera creciente en vez de descartar el POST fallido.
 */
