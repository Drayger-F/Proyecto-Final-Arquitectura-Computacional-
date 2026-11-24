// ===== Lector RFID MFRC522 =====
// Muestra en la LCD el UID y el tipo de cada tarjeta que acercas.
#include <LiquidCrystal.h>
#include <SPI.h>
#include <MFRC522.h>

LiquidCrystal lcd(12, 11, 5, 4, 3, 2);  // RS, E, D4, D5, D6, D7

#define SS_PIN  53
#define RST_PIN 49
MFRC522 rfid(SS_PIN, RST_PIN);

int lecturas = 0;

void esperando() {
  lcd.clear();
  lcd.print("Acerque tarjeta");
  lcd.setCursor(0, 1);
  lcd.print("Leidas: ");
  lcd.print(lecturas);
}

void setup() {
  lcd.begin(16, 2);
  SPI.begin();
  rfid.PCD_Init();
  esperando();
}

void loop() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;

  lecturas++;

  // Línea 1: UID
  lcd.clear();
  for (byte i = 0; i < rfid.uid.size && i < 7; i++) {
    if (rfid.uid.uidByte[i] < 0x10) lcd.print("0");
    lcd.print(rfid.uid.uidByte[i], HEX);
  }

  // Línea 2: tipo de tarjeta
  lcd.setCursor(0, 1);
  MFRC522::PICC_Type tipo = rfid.PICC_GetType(rfid.uid.sak);
  String nombre = rfid.PICC_GetTypeName(tipo);
  lcd.print(nombre.substring(0, 16));

  rfid.PICC_HaltA();
  delay(2500);
  esperando();
}
