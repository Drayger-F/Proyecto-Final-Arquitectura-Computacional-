// ===== Sensor de luz (fotoresistencia, salida AO en A0) =====
// Muestra los lux, el nivel de luz y una barra en la LCD.
#include <LiquidCrystal.h>

LiquidCrystal lcd(12, 11, 5, 4, 3, 2);  // RS, E, D4, D5, D6, D7

#define LDR_PIN A0
const float GAMMA = 0.7;   // Valores del módulo de Wokwi
const float RL10  = 50;

float leerLux() {
  int lectura = analogRead(LDR_PIN);
  float voltaje = lectura / 1024.0 * 5;
  float resistencia = 2000 * voltaje / (1 - voltaje / 5);
  return pow(RL10 * 1e3 * pow(10, GAMMA) / resistencia, (1 / GAMMA));
}

void setup() {
  lcd.begin(16, 2);
}

void loop() {
  float lux = leerLux();

  const char* nivel;
  if (lux < 10)        nivel = "Oscuro";
  else if (lux < 300)  nivel = "Tenue";
  else if (lux < 2000) nivel = "Normal";
  else                 nivel = "Brillante";

  lcd.setCursor(0, 0);
  lcd.print("Luz: ");
  lcd.print(lux, 0);
  lcd.print(" lx         ");

  // Segunda línea: nivel + barra de 6 bloques
  lcd.setCursor(0, 1);
  lcd.print(nivel);
  lcd.print("          ");
  int bloques = constrain(map((long)lux, 0, 5000, 0, 6), 0, 6);
  lcd.setCursor(10, 1);
  for (int i = 0; i < 6; i++) lcd.write(i < bloques ? 255 : ' ');

  delay(300);
}
