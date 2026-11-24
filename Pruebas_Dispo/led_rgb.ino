// ===== LED RGB con AsyncTask (Luis Llamas) =====
// Cambia de color cada segundo sin usar delay() y muestra el color en la LCD.
// LED de ánodo común: LOW = encendido.
#include <LiquidCrystal.h>
#include "AsyncTaskLib.h"

LiquidCrystal lcd(12, 11, 5, 4, 3, 2);  // RS, E, D4, D5, D6, D7

#define LED_R 24
#define LED_G 26
#define LED_B 22

struct Color { const char* nombre; bool r, g, b; };
Color colores[] = {
  {"Rojo",     1, 0, 0},
  {"Verde",    0, 1, 0},
  {"Azul",     0, 0, 1},
  {"Amarillo", 1, 1, 0},
  {"Cian",     0, 1, 1},
  {"Magenta",  1, 0, 1},
  {"Blanco",   1, 1, 1},
  {"Apagado",  0, 0, 0}
};
const byte N_COLORES = sizeof(colores) / sizeof(colores[0]);
byte actual = 0;

void ponerColor(byte i) {
  digitalWrite(LED_R, colores[i].r ? LOW : HIGH);
  digitalWrite(LED_G, colores[i].g ? LOW : HIGH);
  digitalWrite(LED_B, colores[i].b ? LOW : HIGH);

  lcd.setCursor(0, 1);
  lcd.print("Color: ");
  lcd.print(colores[i].nombre);
  lcd.print("        ");  // borra restos del texto anterior
}

// Tarea que se repite cada 1000 ms (autoReset = true)
AsyncTask tareaColor(1000, true, []() {
  actual = (actual + 1) % N_COLORES;
  ponerColor(actual);
});

// Tarea que hace parpadear un indicador en la LCD cada 500 ms
bool indicador = false;
AsyncTask tareaIndicador(500, true, []() {
  indicador = !indicador;
  lcd.setCursor(15, 0);
  lcd.print(indicador ? "*" : " ");
});

void setup() {
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);

  lcd.begin(16, 2);
  lcd.print("LED RGB - Task");
  ponerColor(actual);

  tareaColor.Start();
  tareaIndicador.Start();
}

void loop() {
  tareaColor.Update();
  tareaIndicador.Update();
}
