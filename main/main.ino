/*
  CONTROL DE ACCESO + CONFORT TERMICO  (version simple, un solo archivo)
  Placa: Arduino Uno / Mega

  Librerias (Gestor de librerias):
    - Keypad                (Mark Stanley, Alexander Brevig)
    - LiquidCrystal I2C     (Frank de Brabander)
    - DHT sensor library    (Adafruit)  + Adafruit Unified Sensor
  Servo viene incluida en el IDE.

  Teclas:  0-9 digitos | * borrar | # confirmar | A = salio una persona
*/

#include <Keypad.h>
#include <Wire.h>
#include <LiquidCrystal.h>
#include <Servo.h>
#include <DHT.h>

// ======================= CONFIGURACION =======================
#define MAX_INTENTOS      3
#define T_APERTURA_MS     5000UL   // cerradura abierta
#define T_BLOQUEO_MS     10000UL   // bloqueo temporal
#define USOS_CAMBIO_CLAVE 4
#define LARGO_MIN         4
#define LARGO_MAX         6
#define PMV_ALTO          0.5
#define PMV_BAJO         -0.7
#define TEMP_FUEGO       50.0      // si no hay sensor de fuego, se usa temperatura
#define SEG_POR_HORA     60        // reloj simulado: 60 s reales = 1 h (demo)
#define HORA_INICIAL      8        // cambiar por un RTC DS3231 en la version final

// Pines
const byte FILAS[4] = {9, 8, 7, 6};
const byte COLS[4]  = {5, 4, 3, 2};
#define PIN_SERVO   10
#define PIN_BUZZER  11
#define PIN_LED_ROJO 12
#define PIN_LED_AZUL 13
#define PIN_VENT    A0   // rele/transistor del ventilador
#define PIN_DHT     A1
#define PIN_PUERTA  A2   // LOW = puerta cerrada (iman), HIGH = abierta
#define PIN_FUEGO   A3   // LOW = fuego detectado

// ======================= USUARIOS =======================
enum Perfil { SEGURIDAD, OPERARIO, COORDINADOR, GERENTE };
const char* NOMBRE_PERFIL[] = {"SEG", "OPE", "COO", "GER"};

struct Usuario {
  char clave[LARGO_MAX + 1];
  char anterior[LARGO_MAX + 1];
  Perfil perfil;
  byte usos;
  byte horaIni, horaFin;   // franja horaria permitida
};

Usuario usuarios[] = {
  {"1234", "", SEGURIDAD,   0,  0, 24},
  {"2345", "", OPERARIO,    0,  8, 17},
  {"3456", "", COORDINADOR, 0,  7, 19},
  {"4567", "", GERENTE,     0,  6, 22},
};
const byte N_USUARIOS = sizeof(usuarios) / sizeof(usuarios[0]);

// ======================= ESTADOS =======================
enum Estado { IDLE, OPEN, ERR, BLOQUEO, ALR, CAMBIO };
Estado estado = IDLE;
unsigned long tEstado = 0;          // momento en que se entro al estado

// ======================= OBJETOS =======================
char teclas[4][4] = {{'1','2','3','A'},{'4','5','6','B'},{'7','8','9','C'},{'*','0','#','D'}};
Keypad teclado = Keypad(makeKeymap(teclas), (byte*)FILAS, (byte*)COLS, 4, 4);
LiquidCrystal_I2C lcd(0x27, 16, 2);   // si no se ve nada prueba 0x3F
Servo cerradura;
DHT dht(PIN_DHT, DHT11);

// ======================= VARIABLES =======================
char entrada[LARGO_MAX + 1];
byte nEntrada = 0;
byte intentos = 0;
int  usuarioActual = -1;
const char* mensaje = "";
unsigned long tMensaje = 0;

float temperatura = 25, humedad = 50, pmv = 0;
int personas = 0;
bool fuego = false;

// ======================= UTILIDADES =======================
byte horaActual() {
  return (HORA_INICIAL + (millis() / 1000UL) / SEG_POR_HORA) % 24;
}

void decir(const char* m) { mensaje = m; tMensaje = millis(); }

void cambiarEstado(Estado nuevo) {
  estado = nuevo;
  tEstado = millis();
  nEntrada = 0; entrada[0] = '\0';
  lcd.clear();
}

// Parpadeo sin delay(): true durante tOn ms, false durante tOff ms
bool parpadeo(unsigned int tOn, unsigned int tOff) {
  return (millis() % (tOn + tOff)) < tOn;
}

int buscarUsuario(const char* clave) {
  for (byte i = 0; i < N_USUARIOS; i++)
    if (strcmp(usuarios[i].clave, clave) == 0) return i;
  return -1;
}

void abrir()  { cerradura.write(90); }
void cerrar() { cerradura.write(0);  }

// ======================= TAREAS =======================

// --- Teclado: devuelve la tecla pulsada (0 si no hay) ---
char leerTecla() { return teclado.getKey(); }

// --- Ambiental: cada 2 s ---
void tareaAmbiental() {
  static unsigned long ultimo = 0;
  if (millis() - ultimo < 2000) return;
  ultimo = millis();

  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) temperatura = t;
  if (!isnan(h)) humedad = h;

  // PMV simplificado (aproximacion didactica, NO es la norma ISO 7730):
  // sube con temperatura, humedad y numero de personas.
  pmv = 0.3 * (temperatura - 24.0) + 0.02 * (humedad - 50.0) + 0.1 * personas;

  fuego = (digitalRead(PIN_FUEGO) == LOW) || (temperatura >= TEMP_FUEGO);

  digitalWrite(PIN_VENT, pmv > PMV_ALTO);   // PMV alto -> ventilador
}

// --- Salidas de confort: LED azul parpadea si PMV bajo ---
void tareaConfort() {
  bool frio = (pmv < PMV_BAJO);
  digitalWrite(PIN_LED_AZUL, frio && parpadeo(100, 400));
}

// --- Alarma / LED rojo / buzzer segun el estado ---
void tareaAlarma() {
  bool rojo = false, buzzer = false;
  switch (estado) {
    case BLOQUEO: rojo = parpadeo(500, 500); buzzer = false;           break;
    case ALR:     rojo = parpadeo(100, 500); buzzer = parpadeo(100, 500); break;
    default: break;
  }
  digitalWrite(PIN_LED_ROJO, rojo);
  digitalWrite(PIN_BUZZER, buzzer);
}

// --- Pantalla: cada 200 ms ---
void tareaPantalla() {
  static unsigned long ultimo = 0;
  if (millis() - ultimo < 200) return;
  ultimo = millis();

  char l0[17], l1[17];
  const char* nom[] = {"IDLE", "OPEN", "ERR ", "BLOQ", "ALR ", "CLAV"};
  long resto = 0;

  if (estado == OPEN)    resto = (T_APERTURA_MS - (millis() - tEstado)) / 1000 + 1;
  if (estado == BLOQUEO) resto = (T_BLOQUEO_MS  - (millis() - tEstado)) / 1000 + 1;

  // Linea 0: estado + cuenta regresiva + hora
  if (resto > 0) snprintf(l0, sizeof(l0), "%s %2lds  %02dh P%d", nom[estado], resto, horaActual(), personas);
  else           snprintf(l0, sizeof(l0), "%s     %02dh P%d",     nom[estado], horaActual(), personas);

  // Linea 1: mensaje temporal > digitos enmascarados > ambiente
  if (millis() - tMensaje < 2000 && mensaje[0]) {
    snprintf(l1, sizeof(l1), "%-16s", mensaje);
  } else if (nEntrada > 0) {
    char m[LARGO_MAX + 1]; memset(m, '*', nEntrada); m[nEntrada] = '\0';
    snprintf(l1, sizeof(l1), "Clave: %-9s", m);
  } else if (estado == CAMBIO) {
    snprintf(l1, sizeof(l1), "%-16s", "Nueva clave:");
  } else {
    char tt[8], hh[8], pp[8];
    dtostrf(temperatura, 3, 0, tt); dtostrf(humedad, 3, 0, hh); dtostrf(pmv, 4, 1, pp);
    snprintf(l1, sizeof(l1), "T%sC H%s%% %s", tt, hh, pp);
  }
  lcd.setCursor(0, 0); lcd.print(l0);
  lcd.setCursor(0, 1); lcd.print(l1);
}

// ======================= FSM =======================

// Lee digitos; devuelve true cuando se confirma con '#'
bool capturarClave(char t) {
  if (t >= '0' && t <= '9' && nEntrada < LARGO_MAX) {
    entrada[nEntrada++] = t; entrada[nEntrada] = '\0';
  } else if (t == '*') {
    nEntrada = 0; entrada[0] = '\0';
  } else if (t == '#') {
    if (nEntrada >= LARGO_MIN) return true;
    decir("Minimo 4 digitos");
  }
  return false;
}

void validarClave() {
  int u = buscarUsuario(entrada);
  if (u < 0) {
    intentos++;
    if (intentos >= MAX_INTENTOS) { intentos = 0; cambiarEstado(BLOQUEO); decir("BLOQUEADO"); }
    else { cambiarEstado(ERR); decir("Clave incorrecta"); }
    return;
  }
  byte h = horaActual();
  if (h < usuarios[u].horaIni || h >= usuarios[u].horaFin) {
    cambiarEstado(ERR); decir("Fuera de horario"); return;
  }
  intentos = 0;
  usuarioActual = u;
  usuarios[u].usos++;
  personas++;
  abrir();
  cambiarEstado(OPEN);
  decir(NOMBRE_PERFIL[usuarios[u].perfil]);
}

void guardarNuevaClave() {
  Usuario &u = usuarios[usuarioActual];
  if (strcmp(entrada, u.clave) == 0 || strcmp(entrada, u.anterior) == 0) {
    decir("Debe ser distinta");
    nEntrada = 0; entrada[0] = '\0';
    return;
  }
  if (buscarUsuario(entrada) >= 0) {
    decir("Clave en uso");
    nEntrada = 0; entrada[0] = '\0';
    return;
  }
  strcpy(u.anterior, u.clave);
  strcpy(u.clave, entrada);
  u.usos = 0;
  decir("Clave cambiada");
  cambiarEstado(IDLE);
}

void tareaFSM() {
  char t = leerTecla();
  bool puertaAbierta = (digitalRead(PIN_PUERTA) == HIGH);

  // Eventos de seguridad que aplican en cualquier estado (menos ya en ALR)
  if (estado != ALR) {
    if (fuego)                                   { abrir(); cambiarEstado(ALR); decir("FUEGO!"); return; }  // evacuacion: se libera la cerradura
    if (puertaAbierta && estado != OPEN && estado != CAMBIO) {
      cerrar(); cambiarEstado(ALR); decir("INTRUSION"); return;
    }
  }

  switch (estado) {
    case IDLE:
      if (t == 'A' && personas > 0) personas--;
      else if (t && capturarClave(t)) validarClave();
      break;

    case OPEN:
      if (millis() - tEstado >= T_APERTURA_MS) {
        cerrar();
        if (usuarios[usuarioActual].usos >= USOS_CAMBIO_CLAVE) {
          cambiarEstado(CAMBIO); decir("Cambie su clave");
        } else {
          cambiarEstado(IDLE);                 // retorno automatico al inicio
        }
      }
      break;

    case ERR:
      if (millis() - tEstado >= 1500) cambiarEstado(IDLE);
      break;

    case BLOQUEO:
      if (millis() - tEstado >= T_BLOQUEO_MS) cambiarEstado(IDLE);
      break;

    case ALR:
      // Se sale con '#' solo si ya no hay fuego y la puerta esta cerrada
      if (t == '#' && !fuego && !puertaAbierta) { cerrar(); cambiarEstado(IDLE); }
      break;

    case CAMBIO:
      if (t && capturarClave(t)) guardarNuevaClave();
      break;
  }
}

// ======================= ARDUINO =======================
void setup() {
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_ROJO, OUTPUT);
  pinMode(PIN_LED_AZUL, OUTPUT);
  pinMode(PIN_VENT, OUTPUT);
  pinMode(PIN_PUERTA, INPUT_PULLUP);
  pinMode(PIN_FUEGO, INPUT_PULLUP);

  cerradura.attach(PIN_SERVO);
  cerrar();
  dht.begin();
  lcd.init(); lcd.backlight();
  tEstado = millis();
}

void loop() {
  tareaFSM();
  tareaAlarma();
  tareaAmbiental();
  tareaConfort();
  tareaPantalla();
}
