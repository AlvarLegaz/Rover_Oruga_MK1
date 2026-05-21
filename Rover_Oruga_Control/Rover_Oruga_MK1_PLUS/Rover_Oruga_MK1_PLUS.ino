#define BAUDRATE 115200
#define TIMEOUT_SEGURIDAD 45000
#define MINIMO 30

#define DEBUG TRUE

// ==========================================================
// CONFIGURACION CONTROL SERIE
// ==========================================================

// El ESP32 envia tramas tipo:
// <ROVER_CTRL,avanzar,retroceder,izquierda,derecha>
// <ROVER_STOP,ZERO,0,0,0,0>
// <ROVER_STOP,TIMEOUT,0,0,0,0>
//
// Valores de control: 0 a 255.
// Prioridad:
// 1) Si hay PWM valido en throttle y steering, manda el receptor RC.
// 2) Si falta PWM, manda el control serie.
// 3) Si tampoco hay trama serie reciente, parada de seguridad.

#define SERIAL_CONTROL_TIMEOUT_MS 1000UL
#define PWM_MIN_VALIDO 900
#define PWM_MAX_VALIDO 2100
#define SERIAL_FRAME_BUFFER_SIZE 80
#define DEBUG_INTERVAL_MS 500UL

// Pines L298N
const int R_IN1 = 2;
const int R_IN2 = 3;

const int L_IN3 = 4;
const int L_IN4 = 7;

const int R_ENA = 5;
const int L_ENB = 6;

// Pines receptor RC
const int PIN_THROTTLE = 8;
const int PIN_STEERING = 9;

struct SerialControlData {
  int avanzar;
  int retroceder;
  int izquierda;
  int derecha;
};

SerialControlData serialControl = {0, 0, 0, 0};

bool serialControlDisponible = false;
unsigned long ultimoControlSerieMs = 0;

char serialFrameBuffer[SERIAL_FRAME_BUFFER_SIZE];
byte serialFrameIndex = 0;
bool serialFrameActiva = false;

unsigned long ultimoDebugMs = 0;

void setup() {
  pinMode(R_IN1, OUTPUT);
  pinMode(R_IN2, OUTPUT);
  pinMode(L_IN3, OUTPUT);
  pinMode(L_IN4, OUTPUT);

  // Dirección fija inicial hacia adelante.
  // La función L298_Driver cambia la dirección cuando hace falta.
  digitalWrite(R_IN1, HIGH);
  digitalWrite(R_IN2, LOW);
  digitalWrite(L_IN3, HIGH);
  digitalWrite(L_IN4, LOW);

  pinMode(R_ENA, OUTPUT);
  pinMode(L_ENB, OUTPUT);

  // Pines de entrada PWM RC
  pinMode(PIN_THROTTLE, INPUT);
  pinMode(PIN_STEERING, INPUT);

  Serial.begin(BAUDRATE);

#ifdef DEBUG
  Serial.println("Rover Oruga 1 MK1");
  Serial.println("Control v1.1 - PWM prioritario + fallback serie");
  Serial.println("Trama serie esperada: <ROVER_CTRL,avanzar,retroceder,izquierda,derecha>");
#endif
}

void loop() {
  updateSerialControl();

  int pwmThrottle = 0;
  int pwmSteering = 0;

  updateRCInputs(pwmThrottle, pwmSteering);

  // Leemos otra vez por si entraron bytes mientras pulseIn estaba esperando.
  updateSerialControl();

  if (rcSignalValid(pwmThrottle, pwmSteering)) {
    controlFunction(pwmThrottle, pwmSteering);
    debugEstado("PWM_RC", pwmThrottle, pwmSteering, 0, 0);
  } else {
    controlSerialFunction();
  }
}

// ==========================================================
// LECTURA PWM RC
// ==========================================================

void updateRCInputs(int &throttle_pwm, int &steering_pwm) {
  throttle_pwm = pulseIn(PIN_THROTTLE, HIGH, TIMEOUT_SEGURIDAD);
  steering_pwm = pulseIn(PIN_STEERING, HIGH, TIMEOUT_SEGURIDAD);
}

bool rcSignalValid(int throttle_pwm, int steering_pwm) {
  return (
    throttle_pwm >= PWM_MIN_VALIDO &&
    throttle_pwm <= PWM_MAX_VALIDO &&
    steering_pwm >= PWM_MIN_VALIDO &&
    steering_pwm <= PWM_MAX_VALIDO
  );
}

// ==========================================================
// LECTURA Y PARSEO DE CONTROL SERIE
// ==========================================================

void updateSerialControl() {
  while (Serial.available() > 0) {
    char c = Serial.read();

    if (c == '<') {
      serialFrameActiva = true;
      serialFrameIndex = 0;
      serialFrameBuffer[0] = '\0';
    } else if (c == '>' && serialFrameActiva) {
      serialFrameBuffer[serialFrameIndex] = '\0';
      serialFrameActiva = false;
      parseSerialFrame(serialFrameBuffer);
    } else if (serialFrameActiva) {
      if (serialFrameIndex < SERIAL_FRAME_BUFFER_SIZE - 1) {
        serialFrameBuffer[serialFrameIndex++] = c;
      } else {
        // Trama demasiado larga: se descarta.
        serialFrameActiva = false;
        serialFrameIndex = 0;
      }
    }
  }
}

void parseSerialFrame(char *frame) {
  char *tipo = strtok(frame, ",");

  if (tipo == NULL) {
    return;
  }

  if (strcmp(tipo, "ROVER_CTRL") == 0) {
    char *tokAvanzar = strtok(NULL, ",");
    char *tokRetroceder = strtok(NULL, ",");
    char *tokIzquierda = strtok(NULL, ",");
    char *tokDerecha = strtok(NULL, ",");

    if (
      tokAvanzar == NULL ||
      tokRetroceder == NULL ||
      tokIzquierda == NULL ||
      tokDerecha == NULL
    ) {
      return;
    }

    serialControl.avanzar = constrain(atoi(tokAvanzar), 0, 255);
    serialControl.retroceder = constrain(atoi(tokRetroceder), 0, 255);
    serialControl.izquierda = constrain(atoi(tokIzquierda), 0, 255);
    serialControl.derecha = constrain(atoi(tokDerecha), 0, 255);

    serialControlDisponible = true;
    ultimoControlSerieMs = millis();
  }
  else if (strcmp(tipo, "ROVER_STOP") == 0) {
    serialControl.avanzar = 0;
    serialControl.retroceder = 0;
    serialControl.izquierda = 0;
    serialControl.derecha = 0;

    serialControlDisponible = true;
    ultimoControlSerieMs = millis();
  }
}

bool serialControlReciente() {
  if (!serialControlDisponible) {
    return false;
  }

  return (millis() - ultimoControlSerieMs) <= SERIAL_CONTROL_TIMEOUT_MS;
}

// ==========================================================
// CONTROL POR PWM RC
// ==========================================================

void controlFunction(int throttle_pwm, int steering_pwm) {
  // Convertimos a rango 0–255 para marcha adelante.
  // Este bloque mantiene la lógica original del receptor PWM.
  int throttle = map(throttle_pwm, 1000, 2000, 0, 255);
  throttle = constrain(throttle, 0, 255);

  int steering = map(steering_pwm, 1000, 2000, -350, 350);
  steering = constrain(steering, -350, 350);

  aplicarMezclaDiferencial(throttle, steering);
}

// ==========================================================
// CONTROL POR SERIE
// ==========================================================

void controlSerialFunction() {
  if (!serialControlReciente()) {
    L298_Driver(0, 0);
    debugParadaSerieTimeout();
    return;
  }

  int avanzar = serialControl.avanzar;
  int retroceder = serialControl.retroceder;
  int izquierda = serialControl.izquierda;
  int derecha = serialControl.derecha;

  if (avanzar == 0 && retroceder == 0 && izquierda == 0 && derecha == 0) {
    L298_Driver(0, 0);
    debugEstado("SERIE_STOP", 0, 0, 0, 0);
    return;
  }

  // Avanzar positivo, retroceder negativo.
  int throttle = avanzar - retroceder;
  throttle = constrain(throttle, -255, 255);

  // Derecha positiva, izquierda negativa.
  // Con la mezcla usada: VR = throttle - steering, VL = throttle + steering.
  int steering = derecha - izquierda;
  steering = constrain(steering, -255, 255);

  aplicarMezclaDiferencial(throttle, steering);
  debugEstado("SERIE", throttle, steering, avanzar, retroceder);
}

// ==========================================================
// MEZCLA Y DRIVER
// ==========================================================

void aplicarMezclaDiferencial(int throttle, int steering) {
  int VR = throttle - steering;
  int VL = throttle + steering;

  VR = constrain(VR, -255, 255);
  VL = constrain(VL, -255, 255);

  L298_Driver(VR, VL);
}

void stopMotors() {
  analogWrite(R_ENA, 0);
  analogWrite(L_ENB, 0);
}

void L298_Driver(int VR, int VL) {
  // --- Motor derecho ---
  if (VR > MINIMO) {
    digitalWrite(R_IN1, HIGH);
    digitalWrite(R_IN2, LOW);
    analogWrite(R_ENA, VR);
  }
  else if (VR < -MINIMO) {
    digitalWrite(R_IN1, LOW);
    digitalWrite(R_IN2, HIGH);
    analogWrite(R_ENA, -VR);
  }
  else {
    digitalWrite(R_IN1, LOW);
    digitalWrite(R_IN2, LOW);
    analogWrite(R_ENA, 0);
  }

  // --- Motor izquierdo ---
  if (VL > MINIMO) {
    digitalWrite(L_IN3, HIGH);
    digitalWrite(L_IN4, LOW);
    analogWrite(L_ENB, VL);
  }
  else if (VL < -MINIMO) {
    digitalWrite(L_IN3, LOW);
    digitalWrite(L_IN4, HIGH);
    analogWrite(L_ENB, -VL);
  }
  else {
    digitalWrite(L_IN3, LOW);
    digitalWrite(L_IN4, LOW);
    analogWrite(L_ENB, 0);
  }
}

// ==========================================================
// DEBUG
// ==========================================================

void debugEstado(const char *modo, int v1, int v2, int v3, int v4) {
#ifdef DEBUG
  unsigned long now = millis();

  if (now - ultimoDebugMs < DEBUG_INTERVAL_MS) {
    return;
  }

  ultimoDebugMs = now;

  Serial.print("MODO: ");
  Serial.print(modo);
  Serial.print(" | V1: ");
  Serial.print(v1);
  Serial.print(" | V2: ");
  Serial.print(v2);
  Serial.print(" | V3: ");
  Serial.print(v3);
  Serial.print(" | V4: ");
  Serial.println(v4);
#endif
}

void debugParadaSerieTimeout() {
#ifdef DEBUG
  unsigned long now = millis();

  if (now - ultimoDebugMs < DEBUG_INTERVAL_MS) {
    return;
  }

  ultimoDebugMs = now;
  Serial.println("SIN PWM Y SIN CONTROL SERIE RECIENTE: PARADA");
#endif
}
