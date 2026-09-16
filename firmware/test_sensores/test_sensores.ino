/*
 * SIMQ — Prueba de sensores
 *
 * No usa WiFi ni Supabase. Solo confirma que el hardware responde.
 * Cárgalo primero. Si esto no funciona, nada más va a funcionar.
 *
 * Monitor serie a 115200 baudios.
 */

#include <Wire.h>
#include <Adafruit_SHT31.h>
#include <BH1750.h>
#include <math.h>

const int PIN_SDA = 8;
const int PIN_SCL = 9;
const int PIN_MIC = 3;

Adafruit_SHT31 sht = Adafruit_SHT31();
BH1750 lux;

bool haySHT = false;
bool hayBH  = false;
uint16_t micBase = 2048;

/* Recorre el bus I2C e imprime cada dirección que responda */
void escanearI2C() {
  Serial.println("\n--- Escaneo I2C ---");
  uint8_t encontrados = 0;

  for (uint8_t dir = 1; dir < 127; dir++) {
    Wire.beginTransmission(dir);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  Dispositivo en 0x%02X", dir);
      if (dir == 0x44 || dir == 0x45) Serial.print("  <- SHT31");
      if (dir == 0x23 || dir == 0x5C) Serial.print("  <- BH1750");
      Serial.println();
      encontrados++;
    }
  }

  if (encontrados == 0) {
    Serial.println("  NADA. Revisa: alimentación 3V3, GND común,");
    Serial.println("  SDA en GPIO 8, SCL en GPIO 9, y resistencias pull-up.");
  }
  Serial.printf("--- %u dispositivo(s) ---\n\n", encontrados);
}

void calibrarMic() {
  uint32_t suma = 0;
  for (uint16_t i = 0; i < 1000; i++) {
    suma += analogRead(PIN_MIC);
    delayMicroseconds(200);
  }
  micBase = suma / 1000;
  Serial.printf("Línea base del micrófono: %u\n", micBase);
  if (micBase < 200 || micBase > 3900) {
    Serial.println("  Valor sospechoso. Revisa que el MAX9814 esté alimentado");
    Serial.println("  y que OUT llegue a GPIO 3.");
  }
}

float leerRuido() {
  uint64_t acc = 0;
  for (uint16_t i = 0; i < 800; i++) {
    int16_t m = (int16_t)analogRead(PIN_MIC) - (int16_t)micBase;
    acc += (uint32_t)(m * m);
    delayMicroseconds(60);
  }
  float rms = sqrt((float)acc / 800.0);
  if (rms < 1.0) rms = 1.0;
  return 20.0 * log10(rms) + 26.0;   // el 26 es el offset por calibrar
}

void setup() {
  Serial.begin(115200);
  delay(600);
  Serial.println("\n=============================");
  Serial.println(" SIMQ - prueba de sensores");
  Serial.println("=============================");

  Wire.begin(PIN_SDA, PIN_SCL);
  escanearI2C();

  haySHT = sht.begin(0x44);
  Serial.println(haySHT ? "SHT31  OK" : "SHT31  no responde");

  hayBH = lux.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
  Serial.println(hayBH ? "BH1750 OK" : "BH1750 no responde");

  analogReadResolution(12);
  calibrarMic();

  Serial.println("\nLecturas cada segundo:\n");
}

void loop() {
  float t  = haySHT ? sht.readTemperature() : NAN;
  float h  = haySHT ? sht.readHumidity()    : NAN;
  float l  = hayBH  ? lux.readLightLevel()  : NAN;
  float db = leerRuido();

  Serial.printf("T %6.2f C | HR %5.1f %% | %7.1f lux | %5.1f dB\n", t, h, l, db);
  delay(1000);
}

/*
 * QUÉ BUSCAR
 *
 * Temperatura: debe parecerse a la del ambiente. Si sale 130 o nan,
 *   el sensor no está respondiendo bien.
 * Humedad: entre 20 y 90 en condiciones normales. Sopla encima y
 *   debe subir en un par de segundos. Esa es la mejor prueba.
 * Lux: tapa el sensor con la mano y debe caer casi a cero. Apúntalo
 *   a una lámpara y debe dispararse.
 * dB: aplaude cerca. Debe saltar varios puntos y volver a bajar.
 *
 * Si alguna de esas cuatro pruebas no reacciona, el problema es de
 * cableado, no de código.
 */
