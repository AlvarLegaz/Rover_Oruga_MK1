#define BAUDRATE 115200
#define TIMEOUT_SEGURIDAD 25000

#define DEBUG TRUE

// Pines L298N
const int R_IN1 = 2;
const int R_IN2 = 3;
const int R_ENA = 5;

const int L_IN3 = 4;
const int L_IN4 = 7;
const int L_ENB = 6;

// Pines receptor RC
const int PIN_THROTTLE = 8;
const int PIN_STEERING = 9;

void setup() {
  pinMode(R_IN1, OUTPUT);
  pinMode(R_IN2, OUTPUT);
  pinMode(L_IN3, OUTPUT);
  pinMode(L_IN4, OUTPUT);

  // Dirección fija hacia adelante
  digitalWrite(R_IN1, HIGH);
  digitalWrite(R_IN2, LOW);
  digitalWrite(L_IN3, HIGH);
  digitalWrite(L_IN4, LOW);

  // Pines de entrada
  pinMode(PIN_THROTTLE, INPUT);
  pinMode(PIN_STEERING, INPUT);

#ifdef DEBUG
  Serial.begin(BAUDRATE);
  Serial.println("Rover Oruga 1 MK1");
  Serial.println("Ejecutando Programa Control v 1.0");
#endif
}

void stopMotors() {
  analogWrite(R_ENA, 0);
  analogWrite(L_ENB, 0);
}

void loop() {
  // Leer señales con timeout de seguridad
  int pwmThrottle = 0;  
  int pwmSteering = 0;
  updateRCInputs(pwmThrottle, pwmSteering);
  controlFunction(pwmThrottle,pwmSteering);
}

void updateRCInputs(int &throttle_pwm, int &steering_pwm) {
  throttle_pwm = pulseIn(PIN_THROTTLE, HIGH, TIMEOUT_SEGURIDAD);
  steering_pwm = pulseIn(PIN_STEERING, HIGH, TIMEOUT_SEGURIDAD);

#ifdef DEBUG
  Serial.print("PWM Throttle(us): ");
  Serial.print(throttle_pwm);
  Serial.print("   |   PWM Steering(us): ");
  Serial.println(steering_pwm);
  #endif
}

void controlFunction(int throttle_pwm, int steering_pwm){

  // Seguridad: si cualquier señal no llega → apagar motores
  if (throttle_pwm == 0 || steering_pwm == 0) {
    //Apaga motores
    L298_Driver(0, 0);
#ifdef DEBUG
  Serial.println("SIN SEÑAL, PARADA EMERGENCIA");
#endif
    return;
  }

  // Convertimos a rango 0–255 para marcha adelante
  int throttle = map(throttle_pwm, 1000, 2000, 0, 255);
  throttle = constrain(throttle, 0, 255);

  int steering = map(steering_pwm, 1000, 2000, -255, 255);
  steering = constrain(steering, -255, 255);

  // Mezcla diferencial
  int VR = throttle + steering;
  int VL = throttle - steering;

  // Solo marcha adelante
  VR = constrain(VR, 0, 255);
  VL = constrain(VL, 0, 255);
#ifdef DEBUG
  Serial.print("Velocidad Derecha (0-255): ");
  Serial.print(VR);
  Serial.print(" | Velocidad Izquierda (0-255): ");
  Serial.println(VL);
#endif
  // Aplicar a los motores
  L298_Driver(VR, VL);

}

void L298_Driver(int VR, int VL){

  // --- Motor derecho ---
  if (VR >= 0) {
    // Adelante
    digitalWrite(R_IN1, HIGH);
    digitalWrite(R_IN2, LOW);
    analogWrite(R_ENA, VR);
  } else {
    // Atrás
    digitalWrite(R_IN1, LOW);
    digitalWrite(R_IN2, HIGH);
    analogWrite(R_ENA, -VR);   // PWM positivo
  }

  // --- Motor izquierdo ---
  if (VL >= 0) {
    // Adelante
    digitalWrite(L_IN3, HIGH);
    digitalWrite(L_IN4, LOW);
    analogWrite(L_ENB, VL);
  } else {
    // Atrás
    digitalWrite(L_IN3, LOW);
    digitalWrite(L_IN4, HIGH);
    analogWrite(L_ENB, -VL);   // PWM positivo
  }

}