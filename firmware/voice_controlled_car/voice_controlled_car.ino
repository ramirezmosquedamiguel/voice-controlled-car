#define MIC_PIN 34

// ======================================================
// L298N - Pines de control
// ======================================================

// Canal B
#define ENB 19
#define IN4 18
#define IN3 5

// Canal A
#define IN2 17
#define IN1 16
#define ENA 4

// ======================================================
// Configuración ADC / señal
// ======================================================

// Centro DC medido experimentalmente
const float CENTRO_DC = 1.50;

// Frecuencia de muestreo
const float FS = 4000.0;

// Periodo de muestreo:
// 1 / 4000 Hz = 250 us
const unsigned long TS_US = 250;

// ======================================================
// Filtro pasabanda
// ======================================================

const float FC_HIGH_PASS = 300.0;
const float FC_LOW_PASS  = 1500.0;

// Periodo de muestreo en segundos
const float DT = 1.0 / FS;

// Coeficiente filtro pasa altas
const float RC_HP =
    1.0 / (2.0 * PI * FC_HIGH_PASS);

const float ALPHA_HP =
    RC_HP / (RC_HP + DT);

// Coeficiente filtro pasa bajas
const float RC_LP =
    1.0 / (2.0 * PI * FC_LOW_PASS);

const float ALPHA_LP =
    DT / (RC_LP + DT);

// Variables de estado de los filtros
float x_prev = 0.0;
float y_hp_prev = 0.0;
float y_bp_prev = 0.0;

// Control temporal del muestreo
unsigned long lastSample = 0;

// ======================================================
// Ventana de energía
// ======================================================

float sumaEnergia = 0.0;
unsigned int contador = 0;

unsigned long inicioVentana = 0;

const unsigned long VENTANA_MS = 300;

// ======================================================
// Umbrales experimentales
// ======================================================

// Vocal I
const float UMBRAL_I_MIN = 0.00015;
const float UMBRAL_I_MAX = 0.00150;

// Vocal O
const float UMBRAL_O = 0.00192;

// ======================================================
// Configuración PWM / motores
// ======================================================

const int PWM_FREQ = 1000;
const int PWM_RESOLUTION = 8;

// PWM de 8 bits:
// 0   = 0 %
// 255 = 100 %
const int VELOCIDAD = 150;

// Tiempo de bloqueo después de ejecutar un comando
const unsigned long TIEMPO_MOVIMIENTO_MS = 1500;

// ======================================================
// Comandos de voz
// ======================================================

enum VoiceCommand {
  CMD_NONE,
  CMD_I,
  CMD_O
};

// ======================================================
// Estado de movimiento
// ======================================================

enum MotorState {
  MOTOR_STOP,
  MOTOR_FORWARD,
  MOTOR_REVERSE
};

MotorState estadoMotor = MOTOR_STOP;

// ======================================================
// Clasificación del comando
// ======================================================

VoiceCommand detectCommand(float energiaPromedio) {

  // O -> Adelante
  if (energiaPromedio > UMBRAL_O) {
    return CMD_O;
  }

  // I -> Atrás
  if (
    energiaPromedio > UMBRAL_I_MIN &&
    energiaPromedio < UMBRAL_I_MAX
  ) {
    return CMD_I;
  }

  return CMD_NONE;
}

// ======================================================
// Control de motores
// ======================================================

void forward() {

  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);

  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);

  ledcWrite(ENA, VELOCIDAD);
  ledcWrite(ENB, VELOCIDAD);
}


void reverse() {

  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);

  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);

  ledcWrite(ENA, VELOCIDAD);
  ledcWrite(ENB, VELOCIDAD);
}


void stopMotors() {

  // PWM = 0
  ledcWrite(ENA, 0);
  ledcWrite(ENB, 0);

  // Entradas del puente H en LOW
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);

  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}

// ======================================================
// SETUP
// ======================================================

void setup() {

  Serial.begin(115200);

  // ----------------------------------------------------
  // ADC
  // ----------------------------------------------------

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  // ----------------------------------------------------
  // Pines L298N
  // ----------------------------------------------------

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  // ----------------------------------------------------
  // PWM
  // Arduino-ESP32 Core 3.x
  // ----------------------------------------------------

  ledcAttach(
    ENA,
    PWM_FREQ,
    PWM_RESOLUTION
  );

  ledcAttach(
    ENB,
    PWM_FREQ,
    PWM_RESOLUTION
  );

  // Estado seguro al arrancar
  stopMotors();
  estadoMotor = MOTOR_STOP;

  // Inicialización temporal
  lastSample = micros();
  inicioVentana = millis();

  // ----------------------------------------------------
  // Información de inicio
  // ----------------------------------------------------

  Serial.println();
  Serial.println("==============================");
  Serial.println(" Voice Controlled ESP32 Car");
  Serial.println("==============================");

  Serial.println("Sistema iniciado correctamente");
  Serial.println();

  Serial.println("Comandos:");
  Serial.println("O -> Adelante");
  Serial.println("I -> Atras");

  Serial.println();

  Serial.print("Fs: ");
  Serial.print(FS);
  Serial.println(" Hz");

  Serial.print("Ventana: ");
  Serial.print(VENTANA_MS);
  Serial.println(" ms");

  Serial.print("PWM: ");
  Serial.print(PWM_FREQ);
  Serial.print(" Hz / ");
  Serial.print(PWM_RESOLUTION);
  Serial.println(" bits");

  Serial.print("Velocidad: ");
  Serial.print(VELOCIDAD);
  Serial.println(" / 255");

  Serial.println();
  Serial.println("Escuchando...");
  Serial.println();
}

// ======================================================
// LOOP
// ======================================================

void loop() {

  // Ejecutar una muestra cada 250 us
  if (micros() - lastSample >= TS_US) {

    lastSample = micros();

    // ==================================================
    // Adquisición ADC
    // ==================================================

    int adc = analogRead(MIC_PIN);

    float voltage =
        adc * (3.3 / 4095.0);

    // Quitar componente DC
    float x =
        voltage - CENTRO_DC;

    // ==================================================
    // Filtro pasa altas
    // ==================================================

    float y_hp =
        ALPHA_HP *
        (y_hp_prev + x - x_prev);

    x_prev = x;
    y_hp_prev = y_hp;

    // ==================================================
    // Filtro pasa bajas
    // ==================================================

    float y_bp =
        y_bp_prev +
        ALPHA_LP *
        (y_hp - y_bp_prev);

    y_bp_prev = y_bp;

    // ==================================================
    // Energía instantánea
    // ==================================================

    float energia =
        y_bp * y_bp;

    sumaEnergia += energia;

    contador++;

    // ==================================================
    // Fin de ventana
    // ==================================================

    if (
      millis() - inicioVentana >= VENTANA_MS
    ) {

      // Protección frente a división entre cero
      if (contador > 0) {

        float energiaPromedio =
            sumaEnergia / contador;

        // ==============================================
        // Clasificación
        // ==============================================

        VoiceCommand comando =
            detectCommand(
              energiaPromedio
            );

        // ==============================================
        // Monitor Serial
        // ==============================================

        Serial.print("Energy: ");
        Serial.print(
          energiaPromedio,
          6
        );

        Serial.print(
          " | Command: "
        );

        // ==============================================
        // Acción
        // ==============================================

        switch (comando) {

          // --------------------------------------------
          // O -> ADELANTE
          // --------------------------------------------

          case CMD_O:

            Serial.print("O");
            Serial.print(
              " | Action: FORWARD"
            );

            if (
              estadoMotor !=
              MOTOR_FORWARD
            ) {

              forward();

              estadoMotor =
                  MOTOR_FORWARD;

              delay(
                TIEMPO_MOVIMIENTO_MS
              );
            }

            break;

          // --------------------------------------------
          // I -> ATRÁS
          // --------------------------------------------

          case CMD_I:

            Serial.print("I");
            Serial.print(
              " | Action: REVERSE"
            );

            if (
              estadoMotor !=
              MOTOR_REVERSE
            ) {

              reverse();

              estadoMotor =
                  MOTOR_REVERSE;

              delay(
                TIEMPO_MOVIMIENTO_MS
              );
            }

            break;

          // --------------------------------------------
          // Ningún comando
          // --------------------------------------------

          case CMD_NONE:

          default:

            Serial.print("NONE");
            Serial.print(
              " | Action: STOP"
            );

            if (
              estadoMotor !=
              MOTOR_STOP
            ) {

              stopMotors();

              estadoMotor =
                  MOTOR_STOP;
            }

            break;
        }

        Serial.println();
      }

      // ==================================================
      // Reiniciar ventana
      // ==================================================

      sumaEnergia = 0.0;
      contador = 0;

      inicioVentana = millis();
    }
  }
}