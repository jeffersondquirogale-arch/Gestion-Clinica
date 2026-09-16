# SIMQ — Monitoreo ambiental de sala de cirugía

Sistema de registro de condiciones ambientales y trazabilidad de procedimientos
quirúrgicos. Una ESP32-C3 mide temperatura, humedad, iluminación y ruido dentro
del quirófano; una interfaz web muestra los datos en vivo, dispara alertas
cuando alguna variable se sale de rango y genera un reporte descargable al
finalizar la cirugía.

Desarrollado como módulo de **BioDecision / SYNAP**.

---

## Qué resuelve

Las condiciones ambientales del quirófano son un requisito de habilitación, pero
en la práctica se verifican de forma puntual y manual. SIMQ las registra de
forma continua durante todo el procedimiento y las deja asociadas al paciente,
al equipo humano, a los equipos biomédicos y a los insumos usados, en un solo
documento.

---

## Arquitectura

```
  ESP32-C3 mini                  Supabase                    Interfaz web
  ┌──────────────┐            ┌─────────────┐             ┌──────────────┐
  │ SHT31  (I2C) │            │  lecturas   │  REST/RT    │  4 canales   │
  │ BH1750 (I2C) │  HTTPS     │  sesiones   │ ──────────► │  cronómetro  │
  │ MAX9814(ADC) │ ─────────► │  eventos    │             │  alertas     │
  │              │  1 Hz      │  catálogos  │             │  reporte PDF │
  └──────────────┘            └─────────────┘             └──────────────┘
```

La ESP32 no conoce el concepto de sesión: publica una lectura por segundo con
su `device_id` y sigue publicando pase lo que pase. La interfaz es la que marca
inicio y fin; el reporte filtra las lecturas entre esas dos marcas de tiempo.
Si el navegador se cierra a mitad de cirugía, los datos no se pierden.

---

## Hardware

| Componente | Referencia | Interfaz | Mide |
|---|---|---|---|
| Microcontrolador | ESP32-C3 mini | — | — |
| Temperatura y humedad | SHT31 | I2C `0x44` | °C, %HR |
| Iluminación | BH1750 | I2C `0x23` | lux |
| Ruido | MAX9814 | ADC | dB (relativo) |

### Conexiones

| Sensor | Pin sensor | Pin ESP32-C3 |
|---|---|---|
| SHT31 / BH1750 | SDA | GPIO 8 |
| SHT31 / BH1750 | SCL | GPIO 9 |
| SHT31 / BH1750 | VCC | 3V3 |
| SHT31 / BH1750 | GND | GND |
| MAX9814 | OUT | GPIO 3 (ADC1) |
| MAX9814 | VDD | 3V3 |
| MAX9814 | GAIN | libre (60 dB) |

Los dos sensores I2C comparten bus; no hay conflicto de direcciones.

### Calibración del ruido

El MAX9814 entrega nivel relativo, no dB absolutos. El firmware calcula el RMS
de una ventana de 50 ms y lo convierte a escala logarítmica con un offset fijo
(`DB_OFFSET`). Para que el número sea defendible en un informe hay que medir en
paralelo con un sonómetro calibrado y ajustar ese offset. Mientras no se haga,
el valor sirve para detectar excursiones, no para certificar niveles.

---

## Estructura

```
simq/
├── firmware/simq_esp32/simq_esp32.ino   Adquisición y publicación (Arduino)
├── web/index.html                        Interfaz completa, sin build
├── supabase/schema.sql                   Tablas, índices y políticas
└── README.md
```

---

## Puesta en marcha

### 1. Base de datos

En el SQL Editor de Supabase, ejecutar `supabase/schema.sql`. Crea las tablas,
los índices y las políticas de acceso.

### 2. Firmware

Requiere el core ESP32 en el Arduino IDE (gestor de tarjetas) y estas
librerías:

- `Adafruit SHT31 Library`
- `BH1750` (Christopher Laws)
- `ArduinoJson`

Abrir `firmware/simq_esp32/simq_esp32.ino`, completar el bloque de
configuración (WiFi, URL del proyecto, anon key, `DEVICE_ID`), seleccionar la
placa ESP32-C3 y cargar. El monitor serie a 115200 baudios muestra cada lectura
y el código de respuesta del POST.

### 3. Interfaz

`web/index.html` es un archivo único sin dependencias de build. Se abre directo
en el navegador o se despliega en cualquier hosting estático.

En el panel **Fuente de datos** hay dos modos:

- **Simulación** — genera lecturas sintéticas cada segundo. Sirve para probar
  alertas, gráficas y reporte sin hardware.
- **Supabase** — consulta la última fila de `lecturas` filtrada por
  `device_id`. Requiere URL del proyecto y anon key.

---

## Umbrales por defecto

| Variable | Rango | Unidad |
|---|---|---|
| Temperatura | 18 – 24 | °C |
| Humedad relativa | 30 – 60 | % |
| Iluminación general | 300 – 500 | lux |
| Ruido | 0 – 45 | dB |

Son valores de arranque tomados de referencias de ambiente quirúrgico. **Deben
contrastarse con la Resolución 3100 de 2019 y con ASHRAE 170 antes de usarse
como criterio formal de cumplimiento.** Se editan desde la interfaz y quedan
guardados en el navegador.

Una alerta se dispara tras tres lecturas consecutivas fuera de rango, para
evitar que un pico instantáneo ensucie el registro.

---

## Reporte

Al finalizar la sesión se habilita la descarga en PDF, que incluye:

- Identificación del paciente, procedimiento, quirófano y tiempos
- Descripción del procedimiento
- Personal asistencial con su rol
- Equipos biomédicos con marca, modelo y serie
- Instrumentación e insumos con cantidades
- Desarrollo paso a paso con hora de cada paso
- Estadísticas por variable: mínimo, promedio, máximo y porcentaje de tiempo
  fuera de rango
- Gráfica completa de cada variable
- Registro cronológico de alertas y eventos
- Espacio de firma

---

## Datos personales

El sistema almacena nombre, documento y diagnóstico del paciente. Eso es dato
sensible bajo la **Ley 1581 de 2012**, lo que implica consentimiento informado,
cifrado en tránsito y en reposo, política de retención y registro de la base de
datos ante la SIC.

Antes de usar esto con pacientes reales:

- Activar Row Level Security en todas las tablas
- No exponer `sesiones` ni `pacientes` con la anon key pública
- Definir quién accede a los reportes generados y por cuánto tiempo se conservan

---

## Estado

| Módulo | Estado |
|---|---|
| Interfaz web | Funcional |
| Simulación de datos | Funcional |
| Generación de PDF | Funcional |
| Esquema de base de datos | Definido |
| Firmware ESP32 | Base, pendiente de pruebas con hardware |
| Calibración de ruido | Pendiente |
| CO₂ (SCD40) | Aplazado a la siguiente versión |
| Buffer offline en la ESP32 | Pendiente |
