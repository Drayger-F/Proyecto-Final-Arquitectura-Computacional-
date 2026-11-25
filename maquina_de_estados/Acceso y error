/**
 * @file ControlAcceso.ino
 * @brief CONTROL DE ACCESO + CONFORT TERMICO (un solo archivo)
 *        Arduino Uno / Mega 2560
 *
 * @mainpage Arquitectura Computacional - Control de acceso y confort termico
 * - FSM:    StateMachineLib (Luis Llamas)  -> tabla de transiciones en configurarMaquina()
 * - Tareas: AsyncTaskLib   (Luis Llamas)  -> timeout, ambiente, pantalla y salidas
 *
 * Librerias (Gestor de librerias):
 *   Keypad (Mark Stanley) | LiquidCrystal I2C (Frank de Brabander)
 *   DHT sensor library (Adafruit) + Adafruit Unified Sensor
 *   StateMachineLib | AsyncTaskLib   (Servo y Wire vienen con el IDE)
 *
 * Teclas: 0-9 digitos | * borrar | # confirmar | A = salio una persona
 *
 * Patron de la FSM: cada loop() generarEntrada() convierte lo que pasa
 * (tecla, timeout, sensores) en UN evento "entrada"; las transiciones solo
 * comparan ese evento. Las acciones van en entrarX() / salirX().
 */

#include <Keypad.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <DHT.h>
#include <StateMachineLib.h>
#include <AsyncTaskLib.h>

// ======================= CONFIGURACION =======================
#define MAX_INTENTOS        3
#define USOS_CAMBIO_CLAVE   4
#define LARGO_MIN           4
#define LARGO_MAX           6
#define N_HIST              3        // claves anteriores que no se pueden repetir

#define T_INACTIVIDAD_MS    5000UL   // INICIO -> MONITOR si no hay teclas
#define T_APERTURA_MS       5000UL   // cerradura abierta
#define T_PUERTA_MAX_MS    10000UL   // puerta abierta mas de esto -> ALARMA
#define T_ERROR_MS          2000UL
#define T_ALARMA_MS        10000UL   // alarma por intentos/intrusion/puerta
#define T_BLOQUEO_MS       10000UL   // (la guia propone 30000)
#define T_CAMBIO_MS        30000UL
#define T_MONITOR_MS        3000UL
#define T_PMV_ALTO_MS       3000UL
#define T_PMV_BAJO_MS       2000UL

#define UMBRAL_PMV_ALTO     0.5
#define UMBRAL_PMV_BAJO    -0.7
#define TEMP_ALARMA        30.0      // 3 lecturas seguidas (cada 2 s) -> ALARMA
#define SEG_POR_HORA       60        // reloj simulado: 60 s reales = 1 h (demo)
#define HORA_INICIAL        8        // cambiar por un RTC DS3231 en la version final

// ======================= PINES =======================
const byte FILAS[4] = {9, 8, 7, 6};
const byte COLS[4]  = {5, 4, 3, 2};
#define PIN_SERVO    10
#define PIN_BUZZER   11
#define PIN_LED_ROJO 12
#define PIN_LED_AZUL 13
#define PIN_VENT     A0   // rele/transistor del ventilador
#define PIN_DHT      A1
#define PIN_PUERTA   A2   // LOW = puerta cerrada (iman), HIGH = abierta
#define PIN_FUEGO    A3   // LOW = fuego detectado
#if defined(__AVR_ATmega2560__)
  #define PIN_BOTON  22   // boton de desbloqueo (solo Mega; en Uno no hay pines libres)
#endif

// ======================= USUARIOS =======================
enum Perfil { SEGURIDAD, OPERARIO, COORDINADOR, GERENTE };
const char* NOMBRE_PERFIL[] = {"SEG", "OPE", "COO", "GER"};

struct Usuario {
  char clave[LARGO_MAX + 1];
  char hist[N_HIST][LARGO_MAX + 1];   // claves anteriores
  Perfil perfil;
  byte usos;
  byte horaIni, horaFin;              // franja horaria permitida [ini, fin)
};

Usuario usuarios[] = {
  {"1234", {"", "", ""}, SEGURIDAD,   0,  0, 24},
  {"2345", {"", "", ""}, OPERARIO,    0,  8, 17},
  {"3456", {"", "", ""}, COORDINADOR, 0,  7, 19},
  {"4567", {"", "", ""}, GERENTE,     0,  6, 22},
};
const byte N_USUARIOS = sizeof(usuarios) / sizeof(usuarios[0]);

// ======================= FSM: ESTADOS Y ENTRADAS =======================
enum Estado {
  INICIO = 0,
  VALIDACION,
  ACCESO,
  CAMBIO_CLAVE,
  ERR,
  MONITOR,
  PMV_ALTO,
  PMV_BAJO,
  ALARMA,
  BLOQUEO
};
const int NUM_ESTADOS = 10;
const int NUM_TRANSICIONES = 23;      // ACTUALIZAR si se agregan transiciones
const char* NOMBRE_ESTADO[] = {"IDLE", "VALI", "OPEN", "CLAV", "ERR ", "MON ", "PMV+", "PMV-", "ALR ", "BLOQ"};

enum Entrada {
  IN_NINGUNA = 0,
  IN_CLAVE_LISTA,       // '#' con >= 4 digitos
  IN_CLAVE_OK,
  IN_CLAVE_MAL,         // clave incorrecta o fuera de horario
  IN_CAMBIO_REQ,        // clave vencida (4 usos)
  IN_INTENTOS_MAX,
  IN_TIMEOUT,
  IN_USOS_MAX,
  IN_FIN,               // acceso terminado con la puerta cerrada
  IN_PUERTA_ABIERTA,    // puerta abierta demasiado tiempo
  IN_INTRUSION,
  IN_CLAVE_NUEVA_OK,
  IN_PMV_ALTO,
  IN_PMV_BAJO,
  IN_TEMP_ALTA,         // fuego o temperatura alta
  IN_TECLA,             // cualquier tecla
  IN_TECLA_HASH,        // '#' para salir de la alarma
  IN_BOTON
};

enum Causa { CAUSA_NINGUNA, CAUSA_INTENTOS, CAUSA_INTRUSION, CAUSA_PUERTA, CAUSA_FUEGO, CAUSA_TEMP };

Entrada entrada = IN_NINGUNA;     // evento del ciclo actual
Entrada pendiente = IN_NINGUNA;   // evento ya resuelto al entrar a VALIDACION
Causa   causa = CAUSA_NINGUNA;
bool    alarmaPorTemp = false;

// ======================= PROTOTIPOS (callbacks de tareas) =======================
void cbTimeout();
void tareaAmbiental();
void tareaPantalla();
void tareaSalidas();

// ======================= OBJETOS =======================
StateMachine maquina(NUM_ESTADOS, NUM_TRANSICIONES);

AsyncTask tTimeout(1000, false, cbTimeout);      // un disparo, reconfigurable
AsyncTask tAmbiental(2000, true, tareaAmbiental);
AsyncTask tPantalla(200, true, tareaPantalla);
AsyncTask tSalidas(20, true, tareaSalidas);

char teclas[4][4] = {{'1','2','3','A'},{'4','5','6','B'},{'7','8','9','C'},{'*','0','#','D'}};
Keypad teclado = Keypad(makeKeymap(teclas), (byte*)FILAS, (byte*)COLS, 4, 4);
LiquidCrystal_I2C lcd(0x27, 16, 2);   // si no se ve nada prueba 0x3F
Servo cerradura;
DHT dht(PIN_DHT, DHT11);

// ======================= VARIABLES =======================
char buf[LARGO_MAX + 1];          // digitos tecleados
byte nBuf = 0;
char claveNueva[LARGO_MAX + 1];
byte pasoCambio = 0;              // 0 = nueva, 1 = confirmar
byte intentos = 0;
int  usuarioActual = -1;
bool lockAbierto = false;
bool puertaAbierta = false;       // ya con antirrebote
bool pmvAtendido = false;         // evita el bucle MONITOR <-> PMV
const char* mensaje = "";
const char* motivo = "";
unsigned long tMensaje = 0;
unsigned long tEstado = 0;
unsigned long tPlazo = 0, plazoMs = 0;   // plazo del timeout activo (para la cuenta regresiva)
unsigned long pitidoHasta = 0;
volatile bool evTimeout = false;

float temperatura = 25, humedad = 50, pmv = 0;
int  personas = 0;
bool tempAlta = false;            // 3 lecturas >= TEMP_ALARMA
bool emergencia = false;          // fuego o temperatura alta
bool ventOn = false;

// ======================= UTILIDADES =======================
byte horaActual() {
  return (HORA_INICIAL + (millis() / 1000UL) / SEG_POR_HORA) % 24;
}

Estado estadoActual() { return (Estado)maquina.GetState(); }

void decir(const char* m) { mensaje = m; tMensaje = millis(); }
void pitido(unsigned int ms) { pitidoHasta = millis() + ms; }
void limpiarBuf() { nBuf = 0; buf[0] = '\0'; }

// Parpadeo sin delay(): true durante tOn ms, false durante tOff ms
bool parpadeo(unsigned int tOn, unsigned int tOff) {
  return (millis() % (tOn + tOff)) < tOn;
}

int buscarUsuario(const char* clave) {
  for (byte i = 0; i < N_USUARIOS; i++)
    if (strcmp(usuarios[i].clave, clave) == 0) return i;
  return -1;
}

bool claveRepetida(const Usuario &u, const char* c) {
  if (strcmp(u.clave, c) == 0) return true;
  for (byte h = 0; h < N_HIST; h++)
    if (u.hist[h][0] && strcmp(u.hist[h], c) == 0) return true;
  return false;
}

void abrir()  { cerradura.write(90); lockAbierto = true; }
void cerrar() { cerradura.write(0);  lockAbierto = false; }

const char* textoCausa() {
  switch (causa) {
    case CAUSA_INTENTOS:  return "3 intentos";
    case CAUSA_INTRUSION: return "INTRUSION";
    case CAUSA_PUERTA:    return "Puerta abierta";
    case CAUSA_FUEGO:     return "FUEGO! Evacue";
    case CAUSA_TEMP:      return "TEMP ALTA";
    default:              return "";
  }
}

// ---- timeout (AsyncTask de un disparo) ----
void cbTimeout() { evTimeout = true; }

void iniciarTimeout(unsigned long ms) {
  tTimeout.Stop();
  tTimeout.SetIntervalMillis(ms);
  evTimeout = false;
  plazoMs = ms;
  tPlazo = millis();
  tTimeout.Start();
}

void detenerTimeout() {
  tTimeout.Stop();
  evTimeout = false;
  plazoMs = 0;
}

void inicioEstado() { tEstado = millis(); detenerTimeout(); }

// ---- teclado: 'A' (salio una persona) se atiende en los estados de reposo ----
char leerTecla() {
  char t = teclado.getKey();
  Estado e = estadoActual();
  if (t == 'A' && (e == INICIO || e == MONITOR || e == PMV_ALTO || e == PMV_BAJO)) {
    if (personas > 0) personas--;
    return 0;
  }
  return t;
}

// Lee digitos; devuelve true cuando se confirma con '#'
bool capturarClave(char t) {
  if (t >= '0' && t <= '9' && nBuf < LARGO_MAX) {
    buf[nBuf++] = t; buf[nBuf] = '\0';
  } else if (t == '*') {
    limpiarBuf();
  } else if (t == '#') {
    if (nBuf >= LARGO_MIN) return true;
    decir("Minimo 4 digitos");
  }
  return false;
}

// ---- puerta con antirrebote (200 ms) ----
void actualizarPuerta() {
  static bool cruda = false;
  static unsigned long t = 0;
  bool v = (digitalRead(PIN_PUERTA) == HIGH);
  if (v != cruda) { cruda = v; t = millis(); }
  if (millis() - t > 200) puertaAbierta = v;
  emergencia = (digitalRead(PIN_FUEGO) == LOW) || tempAlta;
}

// ======================= TAREAS ASINCRONAS =======================

// Ambiente: cada 2 s. Es el unico que controla el ventilador (con histeresis)
// para que el confort se mantenga aunque la FSM este en otro estado.
void tareaAmbiental() {
  static byte lecturasAltas = 0;
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) temperatura = t;
  if (!isnan(h)) humedad = h;

  // PMV simplificado (aproximacion didactica, NO es la norma ISO 7730)
  pmv = 0.3 * (temperatura - 24.0) + 0.02 * (humedad - 50.0) + 0.1 * personas;

  if (temperatura >= TEMP_ALARMA) { if (lecturasAltas < 255) lecturasAltas++; }
  else lecturasAltas = 0;
  tempAlta = (lecturasAltas >= 3);

  if (pmv > UMBRAL_PMV_ALTO) ventOn = true;
  else if (pmv < UMBRAL_PMV_ALTO - 0.2) ventOn = false;
  digitalWrite(PIN_VENT, ventOn);
}

// Salidas visuales/sonoras segun el estado (cada 20 ms)
void tareaSalidas() {
  bool rojo = false, azul = false, bz = false;
  switch (estadoActual()) {
    case ERR:      rojo = parpadeo(200, 200); break;
    case ALARMA:   rojo = bz = parpadeo(100, 100); break;
    case BLOQUEO:  rojo = parpadeo(500, 500); break;
    case PMV_ALTO: rojo = parpadeo(100, 500); break;
    case PMV_BAJO: azul = parpadeo(100, 400); break;
    default: break;
  }
  if (millis() < pitidoHasta) bz = true;
  digitalWrite(PIN_LED_ROJO, rojo);
  digitalWrite(PIN_LED_AZUL, azul);
  digitalWrite(PIN_BUZZER, bz);
}

// Pantalla: cada 200 ms
void linea(byte fila, const char* txt) {
  char b[17];
  snprintf(b, sizeof(b), "%-16s", txt);
  lcd.setCursor(0, fila);
  lcd.print(b);
}

void tareaPantalla() {
  char l0[17], l1[17], c[6];
  Estado e = estadoActual();

  // cuenta regresiva solo en estados con plazo visible
  bool conCuenta = (e == ACCESO || e == CAMBIO_CLAVE || e == BLOQUEO || (e == ALARMA && !alarmaPorTemp));
  strcpy(c, "   ");
  if (conCuenta && plazoMs > 0) {
    long r = (long)plazoMs - (long)(millis() - tPlazo);
    if (r > 0) snprintf(c, sizeof(c), "%2lds", r / 1000 + 1);
  }
  snprintf(l0, sizeof(l0), "%s %s %02dh P%d", NOMBRE_ESTADO[e], c, horaActual(), personas);

  // Linea 1: mensaje temporal > digitos enmascarados > texto del estado > ambiente
  if (millis() - tMensaje < 2000 && mensaje[0]) {
    snprintf(l1, sizeof(l1), "%s", mensaje);
  } else if (nBuf > 0) {
    char m[LARGO_MAX + 1];
    memset(m, '*', nBuf); m[nBuf] = '\0';
    const char* pre = (e == CAMBIO_CLAVE) ? (pasoCambio ? "Conf.: " : "Nueva: ") : "Clave: ";
    snprintf(l1, sizeof(l1), "%s%s", pre, m);
  } else if (e == CAMBIO_CLAVE) {
    snprintf(l1, sizeof(l1), "%s", pasoCambio ? "Confirme clave" : "Nueva clave:");
  } else if (e == ERR) {
    snprintf(l1, sizeof(l1), "Intento %d de %d", intentos, MAX_INTENTOS);
  } else if (e == ACCESO) {
    snprintf(l1, sizeof(l1), "Bienvenido");
  } else if (e == ALARMA) {
    if (alarmaPorTemp && !emergencia && !puertaAbierta) snprintf(l1, sizeof(l1), "Libre: pulse #");
    else snprintf(l1, sizeof(l1), "%s", textoCausa());
  } else if (e == BLOQUEO) {
    snprintf(l1, sizeof(l1), "Teclado OFF");
  } else {
    char tt[8], hh[8], pp[8];
    dtostrf(temperatura, 3, 0, tt); dtostrf(humedad, 3, 0, hh); dtostrf(pmv, 4, 1, pp);
    snprintf(l1, sizeof(l1), "T%sC H%s%% %s", tt, hh, pp);
  }
  linea(0, l0);
  linea(1, l1);
}

// ======================= ACCIONES DE LOS ESTADOS =======================

// ---- INICIO ----
void entrarInicio() {
  inicioEstado();
  limpiarBuf();
  usuarioActual = -1;
  pmvAtendido = false;
  cerrar();
  iniciarTimeout(T_INACTIVIDAD_MS);      // sin teclas -> MONITOR
}

// ---- VALIDACION: clave + perfil + franja horaria (se resuelve al entrar) ----
void entrarValidacion() {
  inicioEstado();
  int u = buscarUsuario(buf);
  usuarioActual = u;
  limpiarBuf();                          // no dejar la clave en memoria

  if (u < 0) { motivo = "Clave incorrecta"; pendiente = IN_CLAVE_MAL; return; }
  byte h = horaActual();
  if (h < usuarios[u].horaIni || h >= usuarios[u].horaFin) {
    motivo = "Fuera de horario"; pendiente = IN_CLAVE_MAL; return;
  }
  if (usuarios[u].usos >= USOS_CAMBIO_CLAVE) {   // clave vencida: obliga a cambiarla
    decir("Clave vencida"); pendiente = IN_CAMBIO_REQ; return;
  }
  pendiente = IN_CLAVE_OK;
}

// ---- ACCESO ----
void entrarAcceso() {
  inicioEstado();
  intentos = 0;
  usuarios[usuarioActual].usos++;
  personas++;
  abrir();
  pitido(150);
  decir(NOMBRE_PERFIL[usuarios[usuarioActual].perfil]);
  iniciarTimeout(T_APERTURA_MS);
  Serial.print(F("ACCESO,perfil="));
  Serial.print(NOMBRE_PERFIL[usuarios[usuarioActual].perfil]);
  Serial.print(F(",hora="));
  Serial.println(horaActual());
}
void salirAcceso() { cerrar(); }

// ---- CAMBIO_CLAVE ----
void entrarCambioClave() {
  inicioEstado();
  limpiarBuf();
  pasoCambio = 0;
  decir("Cambie su clave");
  iniciarTimeout(T_CAMBIO_MS);
}
void salirCambioClave() {
  limpiarBuf();
  memset(claveNueva, 0, sizeof(claveNueva));
}

// Nueva clave en dos pasos (nueva + confirmacion). Pone IN_CLAVE_NUEVA_OK al terminar.
void procesarClaveNueva() {
  Usuario &u = usuarios[usuarioActual];
  if (pasoCambio == 0) {
    if (claveRepetida(u, buf))      { decir("Clave ya usada"); limpiarBuf(); return; }
    if (buscarUsuario(buf) >= 0)    { decir("Clave en uso");   limpiarBuf(); return; }
    strcpy(claveNueva, buf);
    limpiarBuf();
    pasoCambio = 1;
  } else {
    if (strcmp(buf, claveNueva) != 0) {
      decir("No coincide"); pasoCambio = 0; limpiarBuf(); pitido(300); return;
    }
    for (byte h = N_HIST - 1; h > 0; h--) strcpy(u.hist[h], u.hist[h - 1]);
    strcpy(u.hist[0], u.clave);
    strcpy(u.clave, claveNueva);         // Save_Info (en RAM; ver nota de EEPROM)
    u.usos = 0;
    limpiarBuf();
    decir("Clave cambiada");
    pitido(150);
    entrada = IN_CLAVE_NUEVA_OK;
  }
}

// ---- ERR ----
void entrarError() {
  inicioEstado();
  intentos++;
  decir(motivo);
  pitido(300);
  iniciarTimeout(T_ERROR_MS);
  if (intentos >= MAX_INTENTOS) causa = CAUSA_INTENTOS;
}

// ---- MONITOR / PMV ----
void entrarMonitor()  { inicioEstado(); iniciarTimeout(T_MONITOR_MS); }
void entrarPmvAlto()  { inicioEstado(); iniciarTimeout(T_PMV_ALTO_MS); }   // el ventilador lo gobierna tareaAmbiental
void entrarPmvBajo()  { inicioEstado(); iniciarTimeout(T_PMV_BAJO_MS); }

// ---- ALARMA ----
void entrarAlarma() {
  inicioEstado();
  alarmaPorTemp = (causa == CAUSA_FUEGO || causa == CAUSA_TEMP);
  if (alarmaPorTemp) abrir();            // evacuacion: se libera la cerradura
  else iniciarTimeout(T_ALARMA_MS);      // luego -> BLOQUEO
  Serial.print(F("ALARMA,"));
  Serial.println(textoCausa());
}
void salirAlarma() { cerrar(); }

// ---- BLOQUEO ----
void entrarBloqueo() {
  inicioEstado();
  iniciarTimeout(T_BLOQUEO_MS);
}
void salirBloqueo() {
  intentos = 0;
  causa = CAUSA_NINGUNA;
}

// ======================= GENERADOR DE EVENTOS =======================

// Intrusion (puerta abierta sin clave) o fuego/temperatura: aplica en estados de reposo
bool detectarEmergencia() {
  if (puertaAbierta) { causa = CAUSA_INTRUSION; entrada = IN_INTRUSION; return true; }
  if (emergencia) {
    causa = (digitalRead(PIN_FUEGO) == LOW) ? CAUSA_FUEGO : CAUSA_TEMP;
    entrada = IN_TEMP_ALTA;
    return true;
  }
  return false;
}

void generarEntrada() {
  entrada = IN_NINGUNA;
  char t;

  switch (estadoActual()) {

    case INICIO:
      if (detectarEmergencia()) break;
      t = leerTecla();
      if (t) {
        iniciarTimeout(T_INACTIVIDAD_MS);          // hubo actividad: reinicia la espera
        if (capturarClave(t)) entrada = IN_CLAVE_LISTA;
      } else if (evTimeout) entrada = IN_TIMEOUT;
      break;

    case VALIDACION:
      entrada = pendiente;
      pendiente = IN_NINGUNA;
      break;

    case ACCESO:
      if (evTimeout && lockAbierto) {              // vencio el tiempo: cerrar pestillo
        cerrar();
        decir("Cierre la puerta");
      }
      if (puertaAbierta && (millis() - tEstado) > T_PUERTA_MAX_MS) {
        causa = CAUSA_PUERTA;
        entrada = IN_PUERTA_ABIERTA;
      } else if (evTimeout && !puertaAbierta) {
        entrada = (usuarios[usuarioActual].usos >= USOS_CAMBIO_CLAVE) ? IN_USOS_MAX : IN_FIN;
      }
      break;

    case CAMBIO_CLAVE:
      if (evTimeout) { decir("Tiempo agotado"); entrada = IN_TIMEOUT; break; }
      t = leerTecla();
      if (t && capturarClave(t)) procesarClaveNueva();
      break;

    case ERR:
      if (intentos >= MAX_INTENTOS) entrada = IN_INTENTOS_MAX;
      else if (evTimeout) entrada = IN_TIMEOUT;
      break;

    case MONITOR:
      if (detectarEmergencia()) break;
      if (leerTecla()) { entrada = IN_TECLA; break; }
      if (!pmvAtendido) {
        if (pmv > UMBRAL_PMV_ALTO) { pmvAtendido = true; entrada = IN_PMV_ALTO; break; }
        if (pmv < UMBRAL_PMV_BAJO) { pmvAtendido = true; entrada = IN_PMV_BAJO; break; }
      }
      if (evTimeout) entrada = IN_TIMEOUT;
      break;

    case PMV_ALTO:
    case PMV_BAJO:
      if (detectarEmergencia()) break;
      if (evTimeout) entrada = IN_TIMEOUT;
      break;

    case ALARMA:
      if (alarmaPorTemp) {
        // Se sale con '#' solo si ya no hay fuego ni temperatura alta y la puerta esta cerrada
        t = leerTecla();
        if (t == '#' && !emergencia && !puertaAbierta) entrada = IN_TECLA_HASH;
      } else if (evTimeout) {
        entrada = IN_TIMEOUT;
      }
      break;

    case BLOQUEO:                                   // teclado deshabilitado
      if (evTimeout) entrada = IN_TIMEOUT;
#ifdef PIN_BOTON
      else if (digitalRead(PIN_BOTON) == LOW) entrada = IN_BOTON;
#endif
      break;
  }
}

// ======================= TABLA DE TRANSICIONES =======================
bool condEmergencia() { return entrada == IN_INTRUSION || entrada == IN_TEMP_ALTA; }

void configurarMaquina() {
  // INICIO
  maquina.AddTransition(INICIO, VALIDACION, []() { return entrada == IN_CLAVE_LISTA; });
  maquina.AddTransition(INICIO, ALARMA,     condEmergencia);
  maquina.AddTransition(INICIO, MONITOR,    []() { return entrada == IN_TIMEOUT; });

  // VALIDACION
  maquina.AddTransition(VALIDACION, ACCESO,       []() { return entrada == IN_CLAVE_OK; });
  maquina.AddTransition(VALIDACION, CAMBIO_CLAVE, []() { return entrada == IN_CAMBIO_REQ; });
  maquina.AddTransition(VALIDACION, ERR,          []() { return entrada == IN_CLAVE_MAL; });

  // ACCESO
  maquina.AddTransition(ACCESO, ALARMA,       []() { return entrada == IN_PUERTA_ABIERTA; });
  maquina.AddTransition(ACCESO, CAMBIO_CLAVE, []() { return entrada == IN_USOS_MAX; });
  maquina.AddTransition(ACCESO, INICIO,       []() { return entrada == IN_FIN; });

  // CAMBIO_CLAVE
  maquina.AddTransition(CAMBIO_CLAVE, INICIO, []() { return entrada == IN_CLAVE_NUEVA_OK || entrada == IN_TIMEOUT; });

  // ERR
  maquina.AddTransition(ERR, ALARMA, []() { return entrada == IN_INTENTOS_MAX; });
  maquina.AddTransition(ERR, INICIO, []() { return entrada == IN_TIMEOUT; });

  // MONITOR
  maquina.AddTransition(MONITOR, ALARMA,   condEmergencia);
  maquina.AddTransition(MONITOR, PMV_ALTO, []() { return entrada == IN_PMV_ALTO; });
  maquina.AddTransition(MONITOR, PMV_BAJO, []() { return entrada == IN_PMV_BAJO; });
  maquina.AddTransition(MONITOR, INICIO,   []() { return entrada == IN_TIMEOUT || entrada == IN_TECLA; });

  // PMV
  maquina.AddTransition(PMV_ALTO, ALARMA,  condEmergencia);
  maquina.AddTransition(PMV_ALTO, MONITOR, []() { return entrada == IN_TIMEOUT; });
  maquina.AddTransition(PMV_BAJO, ALARMA,  condEmergencia);
  maquina.AddTransition(PMV_BAJO, MONITOR, []() { return entrada == IN_TIMEOUT; });

  // ALARMA
  maquina.AddTransition(ALARMA, BLOQUEO, []() { return entrada == IN_TIMEOUT; });
  maquina.AddTransition(ALARMA, INICIO,  []() { return entrada == IN_TECLA_HASH && alarmaPorTemp; });

  // BLOQUEO
  maquina.AddTransition(BLOQUEO, INICIO, []() { return entrada == IN_TIMEOUT || entrada == IN_BOTON; });

  maquina.SetOnEntering(INICIO,       entrarInicio);
  maquina.SetOnEntering(VALIDACION,   entrarValidacion);
  maquina.SetOnEntering(ACCESO,       entrarAcceso);
  maquina.SetOnEntering(CAMBIO_CLAVE, entrarCambioClave);
  maquina.SetOnEntering(ERR,          entrarError);
  maquina.SetOnEntering(MONITOR,      entrarMonitor);
  maquina.SetOnEntering(PMV_ALTO,     entrarPmvAlto);
  maquina.SetOnEntering(PMV_BAJO,     entrarPmvBajo);
  maquina.SetOnEntering(ALARMA,       entrarAlarma);
  maquina.SetOnEntering(BLOQUEO,      entrarBloqueo);

  maquina.SetOnLeaving(ACCESO,       salirAcceso);
  maquina.SetOnLeaving(CAMBIO_CLAVE, salirCambioClave);
  maquina.SetOnLeaving(ALARMA,       salirAlarma);
  maquina.SetOnLeaving(BLOQUEO,      salirBloqueo);

  maquina.SetState(INICIO, false, true);
}

// ======================= ARDUINO =======================
void setup() {
  Serial.begin(9600);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_ROJO, OUTPUT);
  pinMode(PIN_LED_AZUL, OUTPUT);
  pinMode(PIN_VENT, OUTPUT);
  pinMode(PIN_PUERTA, INPUT_PULLUP);
  pinMode(PIN_FUEGO, INPUT_PULLUP);
#ifdef PIN_BOTON
  pinMode(PIN_BOTON, INPUT_PULLUP);
#endif

  cerradura.attach(PIN_SERVO);
  cerrar();
  dht.begin();
  lcd.init();
  lcd.backlight();

  tAmbiental.Start();
  tPantalla.Start();
  tSalidas.Start();

  configurarMaquina();
}

void loop() {
  tTimeout.Update();
  tAmbiental.Update();
  tPantalla.Update();
  tSalidas.Update();

  actualizarPuerta();
  generarEntrada();
  maquina.Update();
  entrada = IN_NINGUNA;
}
