// ===== Teclado 4x4 =====
// Lo que escribes aparece en la LCD.
// '*' borra todo, 'D' borra el último carácter, '#' muestra el texto final.
#include <LiquidCrystal.h>
#include <Keypad.h>

LiquidCrystal lcd(12, 11, 5, 4, 3, 2);  // RS, E, D4, D5, D6, D7

const byte FILAS = 4, COLS = 4;
char teclas[FILAS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};
byte pinesFilas[FILAS] = {36, 38, 40, 42};
byte pinesCols[COLS]   = {44, 46, 48, 34};
Keypad teclado = Keypad(makeKeymap(teclas), pinesFilas, pinesCols, FILAS, COLS);

String texto = "";

void mostrar() {
  lcd.clear();
  lcd.print("Escribe:");
  lcd.setCursor(0, 1);
  lcd.print(texto);
}

void setup() {
  lcd.begin(16, 2);
  mostrar();
}

void loop() {
  char k = teclado.getKey();
  if (!k) return;

  if (k == '*') {
    texto = "";
    mostrar();
  } else if (k == 'D') {
    if (texto.length() > 0) texto.remove(texto.length() - 1);
    mostrar();
  } else if (k == '#') {
    lcd.clear();
    lcd.print("Escribiste:");
    lcd.setCursor(0, 1);
    lcd.print(texto.length() ? texto : "(nada)");
    delay(2000);
    texto = "";
    mostrar();
  } else if (texto.length() < 16) {
    texto += k;
    mostrar();
  }
}
