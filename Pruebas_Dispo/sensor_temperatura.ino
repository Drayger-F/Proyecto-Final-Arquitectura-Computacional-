// ===== Sensor de temperatura NTC (salida OUT en A1) =====
// Muestra la temperatura en °C y °F, y un aviso según el rango.
#include <LiquidCrystal.h>

LiquidCrystal lcd(12, 11, 5, 4, 3, 2);  // RS, E, D4, D5, D6, D7

#define NTC_PIN A1
const float BETA = 3950;  // Coeficiente del NTC de Wokwi

float leerCelsius() {
  int lectura = analogRead(NTC_PIN);
  return 1 / (log(1 / (1023.0 / lectura - 1)) / BETA + 1.0 / 298.15) - 273.15;
}

void setup() {
  lcd.begin(16, 2);
}

void loop() {
  float c = leerCelsius();
  float f = c * 9.0 / 5.0 + 32;

  lcd.setCursor(0, 0);
  lcd.print(c, 1);
  lcd.print((char)223);  // símbolo de grados
  lcd.print("C ");
  lcd.print(f, 1);
  lcd.print((char)223);
  lcd.print("F    ");

  lcd.setCursor(0, 1);
  if (c < 15)       lcd.print("Hace frio       ");
  else if (c < 28)  lcd.print("Temp. normal    ");
  else if (c < 40)  lcd.print("Hace calor      ");
  else              lcd.print("ALERTA: muy alta");

  delay(500);
}
