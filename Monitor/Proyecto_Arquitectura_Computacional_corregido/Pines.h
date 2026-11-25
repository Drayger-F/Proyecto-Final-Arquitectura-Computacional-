#ifndef PINES_H
#define PINES_H

// ============================================================================
//  Pines.h - Mapa de pines del proyecto
//
//  Fuente: diagram.json del proyecto de Wokwi (Arduino MEGA 2560).
//  Si cambian una conexión en el diagrama, cambiarla también aquí.
//  Pines usados: 2 3 4 5 9 11 12 22 24 26 34 36 38 40 42 44 46 48 49 50 51 52 53
//  y analógicos A0, A1.  El pin 28 se eligió libre (ver PIN_VENTILADOR).
// ============================================================================

// ----------------------------- LCD 1602 (4 bits) ----------------------------
// En el diagrama: RW a GND y V0 al potenciómetro (contraste).
#define LCD_RS             12
#define LCD_EN             11
#define LCD_D4             5
#define LCD_D5             4
#define LCD_D6             3
#define LCD_D7             2
#define LCD_COLUMNAS       16
#define LCD_FILAS          2

// --------------------------------- LED RGB ----------------------------------
// ÁNODO COMÚN: el pin común va a 5V, así que el LED se ENCIENDE con LOW y se
// APAGA con HIGH. Usar siempre LED_ON / LED_OFF, nunca HIGH / LOW directo.
#define PIN_LED_ROJO       24     // [COMPARTIDO] VALIDACION, BLOQUEO, ALARMA, PMV_ALTO
#define PIN_LED_VERDE      26     // [COMPARTIDO] ACCESO
#define PIN_LED_AZUL       22     // PMV_BAJO
#define LED_ON             LOW
#define LED_OFF            HIGH

// --------------------------------- Servo ------------------------------------
// OJO: en el diagrama hay UN solo servo. El diagrama de estados usa un servo
// para el pestillo (ACCESO) y otro para la rejilla (PMV_BAJO). Si son dos
// piezas distintas, falta agregar uno (por ejemplo en el pin 10).
#define PIN_SERVO_REJILLA  9

// --------------------------- Sensores analógicos ----------------------------
#define PIN_TEMP           A1     // Termistor NTC: pin OUT
#define PIN_LDR            A0     // Fotorresistencia (aún sin uso en el diagrama de estados)

// Fórmula de temperatura:
//   1 = NTC de Wokwi (termistor 10k + resistencia 10k en serie, BETA 3950)
//   0 = módulo KY-013 real (Steinhart-Hart)
#define NTC_WOKWI          1

// ------------------------------ Teclado 4x4 ---------------------------------
#define KEYPAD_R1          36
#define KEYPAD_R2          38
#define KEYPAD_R3          40
#define KEYPAD_R4          42
#define KEYPAD_C1          44
#define KEYPAD_C2          46
#define KEYPAD_C3          48
#define KEYPAD_C4          34

// ------------------------------ RFID MFRC522 --------------------------------
// SPI de la Mega (fijos): SCK=52, MOSI=51, MISO=50. No usarlos para otra cosa.
#define PIN_RFID_SS        53     // SDA del módulo
#define PIN_RFID_RST       49

// ------------------------------- Ventilador ---------------------------------
// NO aparece en el diagrama de Wokwi. Se asignó el pin 28 (libre). Para verlo
// en la simulación hay que agregar un componente (LED, motor o relé) a ese pin.
#define PIN_VENTILADOR     28

#endif
