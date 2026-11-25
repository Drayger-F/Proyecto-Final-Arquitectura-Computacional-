/**
 * @file TestAcceso.ino
 * @brief Pruebas automaticas del flujo de ACCESO y ERRORES
 *        del proyecto ControlAcceso.ino
 *        Arduino Mega 2560
 *
 * Al arrancar ejecuta automaticamente:
 *   T1  Clave correcta 1234 -> ACCESO
 *   T2  Clave incorrecta 9999 -> ERR -> INICIO
 *   T3  3 intentos malos -> ALARMA -> BLOQUEO
 *   T4  Timeout INICIO -> MONITOR
 *   T5  Cierre automatico tras 5 s en ACCESO
 *   T6  Clave vencida (4 usos) -> CAMBIO_CLAVE
 *   T7  Fuera de horario -> ERR
 *
 * Al terminar entra en modo normal con el teclado real.
 * Resultados por Serial a 9600 baudios.
 */

#include <Keypad.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <DHT.h>
#include <StateMachineLib.h>
#include <AsyncTaskLib.h>

// ---------------- CONFIG ----------------
#define MAX_INTENTOS        3
#define USOS_CAMBIO_CLAVE   4
#define LARGO_MIN           4
#define LARGO_MAX           6
#define N_HIST              3

#define T_INACTIVIDAD_MS    5000UL
#define T_APERTURA_MS       5000UL
#define T_PUERTA_MAX_MS    10000UL
#define T_ERROR_MS          2000UL
#define T_ALARMA_MS        10000UL
#define T_BLOQUEO_MS       10000UL
#define T_CAMBIO_MS        30000UL
#define T_MONITOR_MS        3000UL
#define T_PMV_ALTO_MS       3000UL
#define T_PMV_BAJO_MS       2000UL

#define UMBRAL_PMV_ALTO      0.5
#define UMBRAL_PMV_BAJO     -0.7
#define TEMP_ALARMA         30.0
#define SEG_POR_HORA        60
#define HORA_INICIAL         8

// ---------------- PINES MEGA ----------------
const byte FILAS[4] = {9, 8, 7, 6};
const byte COLS[4]  = {5, 4, 3, 2};
#define PIN_SERVO    10
#define PIN_BUZZER   11
#define PIN_LED_ROJO 12
#define PIN_LED_AZUL 13
#define PIN_VENT     A0
#define PIN_DHT      A1
#define PIN_PUERTA   A2
#define PIN_FUEGO    A3
#define PIN_BOTON    22

// ---------------- USUARIOS ----------------
enum Perfil { SEGURIDAD, OPERARIO, COORDINADOR, GERENTE };
const char* NOMBRE_PERFIL[] = {"SEG", "OPE", "COO", "GER"};

struct Usuario {
  char clave[LARGO_MAX + 1];
  char hist[N_HIST][LARGO_MAX + 1];
  Perfil perfil;
  byte usos;
  byte horaIni, horaFin;
};

Usuario usuarios[] = {
  {"1234", {"", "", ""}, SEGURIDAD,   0,  0, 24},
  {"2345", {"", "", ""}, OPERARIO,    0,  8, 17},
  {"3456", {"", "", ""}, COORDINADOR, 0,  7, 19},
  {"4567", {"", "", ""}, GERENTE,     0,  6, 22},
};
const byte N_USUARIOS = sizeof(usuarios) / sizeof(usuarios[0]);

// ---------------- FSM ----------------
enum Estado { INICIO=0, VALIDACION, ACCESO, CAMBIO_CLAVE, ERR,
              MONITOR, PMV_ALTO, PMV_BAJO, ALARMA, BLOQUEO };
const int NUM_ESTADOS = 10;
const int NUM_TRANSICIONES = 23;
const char* NOMBRE_ESTADO[] = {"IDLE","VALI","OPEN","CLAV","ERR ","MON ","PMV+","PMV-","ALR ","BLOQ"};

enum Entrada {
  IN_NINGUNA=0, IN_CLAVE_LISTA, IN_CLAVE_OK, IN_CLAVE_MAL,
  IN_CAMBIO_REQ, IN_INTENTOS_MAX, IN_TIMEOUT, IN_USOS_MAX,
  IN_FIN, IN_PUERTA_ABIERTA, IN_INTRUSION, IN_CLAVE_NUEVA_OK,
  IN_PMV_ALTO, IN_PMV_BAJO, IN_TEMP_ALTA, IN_TECLA,
  IN_TECLA_HASH, IN_BOTON
};

enum Causa { CAUSA_NINGUNA, CAUSA_INTENTOS, CAUSA_INTRUSION, CAUSA_PUERTA, CAUSA_FUEGO, CAUSA_TEMP };

Entrada entrada = IN_NINGUNA;
Entrada pendiente = IN_NINGUNA;
Causa   causa = CAUSA_NINGUNA;
bool    alarmaPorTemp = false;

// ---------------- OBJETOS ----------------
StateMachine maquina(NUM_ESTADOS, NUM_TRANSICIONES);
AsyncTask tTimeout(1000, false, NULL);
AsyncTask tAmbiental(2000, true, NULL);
AsyncTask tPantalla(200, true, NULL);
AsyncTask tSalidas(20, true, NULL);

char teclas[4][4] = {{'1','2','3','A'},{'4','5','6','B'},{'7','8','9','C'},{'*','0','#','D'}};
Keypad teclado = Keypad(makeKeymap(teclas), (byte*)FILAS, (byte*)COLS, 4, 4);
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo cerradura;
DHT dht(PIN_DHT, DHT11);

// ---------------- VARIABLES ----------------
char buf[LARGO_MAX + 1];
byte nBuf = 0;
char claveNueva[LARGO_MAX + 1];
byte pasoCambio = 0;
byte intentos = 0;
int  usuarioActual = -1;
bool lockAbierto = false;
bool puertaAbierta = false;
const char* mensaje = "";
const char* motivo = "";
unsigned long tMensaje = 0;
unsigned long tEstado = 0;
unsigned long tPlazo = 0, plazoMs = 0;
unsigned long pitidoHasta = 0;
volatile bool evTimeout = false;

float temperatura = 25, humedad = 50, pmv = 0;
int  personas = 0;
bool tempAlta = false;
bool emergencia = false;
bool ventOn = false;

// =============== TEST HARNESS ===============
bool    enTest = true;
char    testQueue[20];
byte    tqPos = 0, tqLen = 0;
bool    testDoorOpen = false;
unsigned long testHoraForzada = 0xFFFFFFFF; // hora simulada en pruebas
bool    testHoraActiva = false;

char    testNombre[40];
Estado  testEsperado;
unsigned long testT0 = 0;
unsigned long testTimeout = 0;
bool    testActivo = false;
int     testsOk = 0, testsFail = 0;

void cargarEntrada(const char* s) {
  tqLen = 0; tqPos = 0;
  while (*s && tqLen < sizeof(testQueue)) testQueue[tqLen++] = *s++;
}

byte horaActual() {
  if (testHoraActiva) return (byte)testHoraForzada;
  return (HORA_INICIAL + (millis() / 1000UL) / SEG_POR_HORA) % 24;
}

Estado estadoActual() { return (Estado)maquina.GetState(); }
void decir(const char* m) { mensaje = m; tMensaje = millis(); }
void pitido(unsigned int ms) { pitidoHasta = millis() + ms; }
void limpiarBuf() { nBuf = 0; buf[0] = '\0'; }
bool parpadeo(unsigned int tOn, unsigned int tOff) { return (millis() % (tOn + tOff)) < tOn; }

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

void cbTimeout() { evTimeout = true; }
void iniciarTimeout(unsigned long ms) {
  tTimeout.Stop(); tTimeout.SetIntervalMillis(ms);
  evTimeout = false; plazoMs = ms; tPlazo = millis(); tTimeout.Start();
}
void detenerTimeout() { tTimeout.Stop(); evTimeout = false; plazoMs = 0; }
void inicioEstado() { tEstado = millis(); detenerTimeout(); }

char leerTecla() {
  // En test: primero cola de inyeccion
  if (enTest && tqPos < tqLen) return testQueue[tqPos++];
  char t = teclado.getKey();
  Estado e = estadoActual();
  if (t == 'A' && (e == INICIO || e == MONITOR || e == PMV_ALTO || e == PMV_BAJO)) {
    if (personas > 0) personas--;
    return 0;
  }
  return t;
}

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

void actualizarPuerta() {
  if (enTest) {
    puertaAbierta = testDoorOpen;
    emergencia = false;
    return;
  }
  static bool cruda = false;
  static unsigned long t = 0;
  bool v = (digitalRead(PIN_PUERTA) == HIGH);
  if (v != cruda) { cruda = v; t = millis(); }
  if (millis() - t > 200) puertaAbierta = v;
  emergencia = (digitalRead(PIN_FUEGO) == LOW) || tempAlta;
}

// =============== TAREAS ===============
void tareaAmbiental() {
  static byte lecturasAltas = 0;
  if (enTest) return;
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) temperatura = t;
  if (!isnan(h)) humedad = h;
  pmv = 0.3 * (temperatura - 24.0) + 0.02 * (humedad - 50.0) + 0.1 * personas;
  if (temperatura >= TEMP_ALARMA) { if (lecturasAltas < 255) lecturasAltas++; }
  else lecturasAltas = 0;
  tempAlta = (lecturasAltas >= 3);
  if (pmv > UMBRAL_PMV_ALTO) ventOn = true;
  else if (pmv < UMBRAL_PMV_ALTO - 0.2) ventOn = false;
  digitalWrite(PIN_VENT, ventOn);
}

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

void linea(byte fila, const char* txt) {
  char b[17]; snprintf(b, sizeof(b), "%-16s", txt);
  lcd.setCursor(0, fila); lcd.print(b);
}

void tareaPantalla() {
  if (enTest && testActivo) {
    char l0[17], l1[17];
    snprintf(l0, sizeof(l0), "TEST %s", testNombre);
    snprintf(l1, sizeof(l1), "Esp:%s Act:%s",
             NOMBRE_ESTADO[testEsperado], NOMBRE_ESTADO[estadoActual()]);
    linea(0, l0); linea(1, l1);
    return;
  }
  char l0[17], l1[17], c[6];
  Estado e = estadoActual();
  bool conCuenta = (e == ACCESO || e == CAMBIO_CLAVE || e == BLOQUEO || (e == ALARMA && !alarmaPorTemp));
  strcpy(c, "   ");
  if (conCuenta && plazoMs > 0) {
    long r = (long)plazoMs - (long)(millis() - tPlazo);
    if (r > 0) snprintf(c, sizeof(c), "%2lds", r / 1000 + 1);
  }
  snprintf(l0, sizeof(l0), "%s %s %02dh P%d", NOMBRE_ESTADO[e], c, horaActual(), personas);
  if (millis() - tMensaje < 2000 && mensaje[0]) snprintf(l1, sizeof(l1), "%s", mensaje);
  else if (nBuf > 0) {
    char m[LARGO_MAX + 1]; memset(m, '*', nBuf); m[nBuf] = '\0';
    const char* pre = (e == CAMBIO_CLAVE) ? (pasoCambio ? "Conf.: " : "Nueva: ") : "Clave: ";
    snprintf(l1, sizeof(l1), "%s%s", pre, m);
  } else if (e == ERR) snprintf(l1, sizeof(l1), "Intento %d de %d", intentos, MAX_INTENTOS);
  else if (e == ACCESO) snprintf(l1, sizeof(l1), "Bienvenido");
  else if (e == BLOQUEO) snprintf(l1, sizeof(l1), "Teclado OFF");
  else snprintf(l1, sizeof(l1), "T%.0fC H%.0f%%", temperatura, humedad);
  linea(0, l0); linea(1, l1);
}

// =============== ACCIONES ===============
void entrarInicio() {
  inicioEstado(); limpiarBuf(); usuarioActual = -1;
  cerrar(); iniciarTimeout(T_INACTIVIDAD_MS);
}

void entrarValidacion() {
  inicioEstado();
  int u = buscarUsuario(buf);
  usuarioActual = u;
  limpiarBuf();
  if (u < 0) { motivo = "Clave incorrecta"; pendiente = IN_CLAVE_MAL; return; }
  byte h = horaActual();
  if (h < usuarios[u].horaIni || h >= usuarios[u].horaFin) {
    motivo = "Fuera de horario"; pendiente = IN_CLAVE_MAL; return;
  }
  if (usuarios[u].usos >= USOS_CAMBIO_CLAVE) { pendiente = IN_CAMBIO_REQ; return; }
  pendiente = IN_CLAVE_OK;
}

void entrarAcceso() {
  inicioEstado(); intentos = 0;
  usuarios[usuarioActual].usos++; personas++;
  abrir(); pitido(150);
  decir(NOMBRE_PERFIL[usuarios[usuarioActual].perfil]);
  iniciarTimeout(T_APERTURA_MS);
}
void salirAcceso() { cerrar(); }

void entrarCambioClave() {
  inicioEstado(); limpiarBuf(); pasoCambio = 0;
  decir("Cambie su clave"); iniciarTimeout(T_CAMBIO_MS);
}
void salirCambioClave() { limpiarBuf(); memset(claveNueva, 0, sizeof(claveNueva)); }

void procesarClaveNueva() {
  Usuario &u = usuarios[usuarioActual];
  if (pasoCambio == 0) {
    if (claveRepetida(u, buf)) { decir("Clave ya usada"); limpiarBuf(); return; }
    if (buscarUsuario(buf) >= 0) { decir("Clave en uso"); limpiarBuf(); return; }
    strcpy(claveNueva, buf); limpiarBuf(); pasoCambio = 1;
  } else {
    if (strcmp(buf, claveNueva) != 0) {
      decir("No coincide"); pasoCambio = 0; limpiarBuf(); pitido(300); return;
    }
    for (byte h = N_HIST - 1; h > 0; h--) strcpy(u.hist[h], u.hist[h - 1]);
    strcpy(u.hist[0], u.clave); strcpy(u.clave, claveNueva);
    u.usos = 0; limpiarBuf(); decir("Clave cambiada"); pitido(150);
    entrada = IN_CLAVE_NUEVA_OK;
  }
}

void entrarError() {
  inicioEstado(); intentos++; decir(motivo); pitido(300);
  iniciarTimeout(T_ERROR_MS);
  if (intentos >= MAX_INTENTOS) causa = CAUSA_INTENTOS;
}

void entrarMonitor() { inicioEstado(); iniciarTimeout(T_MONITOR_MS); }
void entrarPmvAlto() { inicioEstado(); iniciarTimeout(T_PMV_ALTO_MS); }
void entrarPmvBajo() { inicioEstado(); iniciarTimeout(T_PMV_BAJO_MS); }

void entrarAlarma() {
  inicioEstado();
  alarmaPorTemp = (causa == CAUSA_FUEGO || causa == CAUSA_TEMP);
  if (alarmaPorTemp) abrir();
  else iniciarTimeout(T_ALARMA_MS);
}
void salirAlarma() { cerrar(); }

void entrarBloqueo() { inicioEstado(); iniciarTimeout(T_BLOQUEO_MS); }
void salirBloqueo() { intentos = 0; causa = CAUSA_NINGUNA; }

bool detectarEmergencia() {
  if (enTest) return false;
  if (puertaAbierta) { causa = CAUSA_INTRUSION; entrada = IN_INTRUSION; return true; }
  if (emergencia) {
    causa = (digitalRead(PIN_FUEGO) == LOW) ? CAUSA_FUEGO : CAUSA_TEMP;
    entrada = IN_TEMP_ALTA; return true;
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
      if (t) { iniciarTimeout(T_INACTIVIDAD_MS); if (capturarClave(t)) entrada = IN_CLAVE_LISTA; }
      else if (evTimeout) entrada = IN_TIMEOUT;
      break;
    case VALIDACION: entrada = pendiente; pendiente = IN_NINGUNA; break;
    case ACCESO:
      if (evTimeout && lockAbierto) { cerrar(); decir("Cierre la puerta"); }
      if (puertaAbierta && (millis() - tEstado) > T_PUERTA_MAX_MS) {
        causa = CAUSA_PUERTA; entrada = IN_PUERTA_ABIERTA;
      } else if (evTimeout && !puertaAbierta) {
        entrada = (usuarios[usuarioActual].usos >= USOS_CAMBIO_CLAVE) ? IN_USOS_MAX : IN_FIN;
      }
      break;
    case CAMBIO_CLAVE:
      if (evTimeout) { decir("Tiempo agotado"); entrada = IN_TIMEOUT; break; }
      t = leerTecla(); if (t && capturarClave(t)) procesarClaveNueva();
      break;
    case ERR:
      if (intentos >= MAX_INTENTOS) entrada = IN_INTENTOS_MAX;
      else if (evTimeout) entrada = IN_TIMEOUT;
      break;
    case MONITOR:
      if (detectarEmergencia()) break;
      if (leerTecla()) { entrada = IN_TECLA; break; }
      if (evTimeout) entrada = IN_TIMEOUT;
      break;
    case PMV_ALTO:
    case PMV_BAJO:
      if (detectarEmergencia()) break;
      if (evTimeout) entrada = IN_TIMEOUT;
      break;
    case ALARMA:
      if (alarmaPorTemp) {
        t = leerTecla();
        if (t == '#' && !emergencia && !puertaAbierta) entrada = IN_TECLA_HASH;
      } else if (evTimeout) entrada = IN_TIMEOUT;
      break;
    case BLOQUEO:
      if (evTimeout) entrada = IN_TIMEOUT;
      else if (!enTest && digitalRead(PIN_BOTON) == LOW) entrada = IN_BOTON;
      break;
  }
}

bool condEmergencia() { return entrada == IN_INTRUSION || entrada == IN_TEMP_ALTA; }

void configurarMaquina() {
  maquina.AddTransition(INICIO, VALIDACION, []() { return entrada == IN_CLAVE_LISTA; });
  maquina.AddTransition(INICIO, ALARMA,     condEmergencia);
  maquina.AddTransition(INICIO, MONITOR,    []() { return entrada == IN_TIMEOUT; });
  maquina.AddTransition(VALIDACION, ACCESO,       []() { return entrada == IN_CLAVE_OK; });
  maquina.AddTransition(VALIDACION, CAMBIO_CLAVE, []() { return entrada == IN_CAMBIO_REQ; });
  maquina.AddTransition(VALIDACION, ERR,          []() { return entrada == IN_CLAVE_MAL; });
  maquina.AddTransition(ACCESO, ALARMA,       []() { return entrada == IN_PUERTA_ABIERTA; });
  maquina.AddTransition(ACCESO, CAMBIO_CLAVE, []() { return entrada == IN_USOS_MAX; });
  maquina.AddTransition(ACCESO, INICIO,       []() { return entrada == IN_FIN; });
  maquina.AddTransition(CAMBIO_CLAVE, INICIO, []() { return entrada == IN_CLAVE_NUEVA_OK || entrada == IN_TIMEOUT; });
  maquina.AddTransition(ERR, ALARMA, []() { return entrada == IN_INTENTOS_MAX; });
  maquina.AddTransition(ERR, INICIO, []() { return entrada == IN_TIMEOUT; });
  maquina.AddTransition(MONITOR, ALARMA,   condEmergencia);
  maquina.AddTransition(MONITOR, PMV_ALTO, []() { return entrada == IN_PMV_ALTO; });
  maquina.AddTransition(MONITOR, PMV_BAJO, []() { return entrada == IN_PMV_BAJO; });
  maquina.AddTransition(MONITOR, INICIO,   []() { return entrada == IN_TIMEOUT || entrada == IN_TECLA; });
  maquina.AddTransition(PMV_ALTO, ALARMA,  condEmergencia);
  maquina.AddTransition(PMV_ALTO, MONITOR, []() { return entrada == IN_TIMEOUT; });
  maquina.AddTransition(PMV_BAJO, ALARMA,  condEmergencia);
  maquina.AddTransition(PMV_BAJO, MONITOR, []() { return entrada == IN_TIMEOUT; });
  maquina.AddTransition(ALARMA, BLOQUEO, []() { return entrada == IN_TIMEOUT; });
  maquina.AddTransition(ALARMA, INICIO,  []() { return entrada == IN_TECLA_HASH && alarmaPorTemp; });
  maquina.AddTransition(BLOQUEO, INICIO, []() { return entrada == IN_TIMEOUT || entrada == IN_BOTON; });

  maquina.SetOnEntering(INICIO, entrarInicio);
  maquina.SetOnEntering(VALIDACION, entrarValidacion);
  maquina.SetOnEntering(ACCESO, entrarAcceso);
  maquina.SetOnEntering(CAMBIO_CLAVE, entrarCambioClave);
  maquina.SetOnEntering(ERR, entrarError);
  maquina.SetOnEntering(MONITOR, entrarMonitor);
  maquina.SetOnEntering(PMV_ALTO, entrarPmvAlto);
  maquina.SetOnEntering(PMV_BAJO, entrarPmvBajo);
  maquina.SetOnEntering(ALARMA, entrarAlarma);
  maquina.SetOnEntering(BLOQUEO, entrarBloqueo);

  maquina.SetOnLeaving(ACCESO, salirAcceso);
  maquina.SetOnLeaving(CAMBIO_CLAVE, salirCambioClave);
  maquina.SetOnLeaving(ALARMA, salirAlarma);
  maquina.SetOnLeaving(BLOQUEO, salirBloqueo);

  maquina.SetState(INICIO, false, true);
}

// =============== MOTOR DE PRUEBAS ===============
void resetEstado() {
  // Volver a INICIO limpio
  intentos = 0;
  causa = CAUSA_NINGUNA;
  pendiente = IN_NINGUNA;
  limpiarBuf();
  memset(claveNueva, 0, sizeof(claveNueva));
  pasoCambio = 0;
  testDoorOpen = false;
  maquina.SetState(INICIO, true, true);
  tqPos = 0;
}

void iniciarPrueba(const char* nombre, const char* input, Estado esperado, unsigned long timeoutMs) {
  resetEstado();
  cargarEntrada(input);
  strncpy(testNombre, nombre, sizeof(testNombre) - 1);
  testNombre[sizeof(testNombre) - 1] = '\0';
  testEsperado = esperado;
  testTimeout = timeoutMs;
  testT0 = millis();
  testActivo = true;
  Serial.print(F("\n[TEST] "));
  Serial.print(nombre);
  Serial.print(F("  esperado="));
  Serial.print(NOMBRE_ESTADO[esperado]);
  Serial.print(F("  input=\""));
  Serial.print(input);
  Serial.println(F("\""));
}

bool verificar() {
  if (!testActivo) return false;
  Estado e = estadoActual();
  if (e == testEsperado) {
    Serial.print(F("  PASS  alcanzo "));
    Serial.print(NOMBRE_ESTADO[e]);
    Serial.print(F(" en "));
    Serial.print(millis() - testT0);
    Serial.println(F(" ms"));
    testsOk++;
    testActivo = false;
    return true;
  }
  if (millis() - testT0 > testTimeout) {
    Serial.print(F("  FAIL  esperaba "));
    Serial.print(NOMBRE_ESTADO[testEsperado]);
    Serial.print(F("  llego a "));
    Serial.println(NOMBRE_ESTADO[e]);
    testsFail++;
    testActivo = false;
    return true;
  }
  return false;
}

// =============== SETUP ===============
void setup() {
  Serial.begin(9600);
  while (!Serial) {}
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_ROJO, OUTPUT);
  pinMode(PIN_LED_AZUL, OUTPUT);
  pinMode(PIN_VENT, OUTPUT);
  pinMode(PIN_PUERTA, INPUT_PULLUP);
  pinMode(PIN_FUEGO, INPUT_PULLUP);
  pinMode(PIN_BOTON, INPUT_PULLUP);

  cerradura.attach(PIN_SERVO); cerrar();
  dht.begin();
  Wire.begin();
  lcd.init(); lcd.backlight();

  tTimeout.SetCallback(cbTimeout);
  tAmbiental.SetCallback(tareaAmbiental);
  tPantalla.SetCallback(tareaPantalla);
  tSalidas.SetCallback(tareaSalidas);
  tAmbiental.Start(); tPantalla.Start(); tSalidas.Start();

  configurarMaquina();

  Serial.println(F("========================================"));
  Serial.println(F(" TEST ACCESO / ERRORES - Mega 2560"));
  Serial.println(F("========================================"));

  delay(500);
}

// =============== LOOP ===============
// Puntero de prueba actual (0..6)
int testN = 0;
unsigned long testCambioT = 0;

void loop() {
  tTimeout.Update();
  tAmbiental.Update();
  tPantalla.Update();
  tSalidas.Update();
  actualizarPuerta();
  generarEntrada();
  maquina.Update();
  entrada = IN_NINGUNA;

  if (enTest) {
    // Arrancar la siguiente prueba cuando no hay ninguna activa
    if (!testActivo && testN < 7 && millis() - testCambioT > 500) {
      testCambioT = millis();
      switch (testN++) {
        case 0: // T1: clave correcta -> ACCESO
          testHoraActiva = true; testHoraForzada = 10;
          iniciarPrueba("T1 clave OK", "1234#", ACCESO, 4000);
          break;
        case 1: // T2: clave incorrecta -> ERR
          testHoraActiva = true; testHoraForzada = 10;
          iniciarPrueba("T2 clave mala", "9999#", ERR, 4000);
          break;
        case 2: // T3: 3 intentos -> ALARMA
          testHoraActiva = true; testHoraForzada = 10;
          iniciarPrueba("T3 3 intentos", "9999#9999#9999#", ALARMA, 12000);
          break;
        case 3: // T4: timeout INICIO -> MONITOR
          testHoraActiva = true; testHoraForzada = 10;
          iniciarPrueba("T4 timeout", "", MONITOR, 7000);
          break;
        case 4: // T5: dentro de ACCESO, esperar cierre -> INICIO
          testHoraActiva = true; testHoraForzada = 10;
          iniciarPrueba("T5 cierre auto", "1234#", INICIO, 12000);
          break;
        case 5: // T6: clave vencida -> CAMBIO_CLAVE
          testHoraActiva = true; testHoraForzada = 10;
          usuarios[0].usos = USOS_CAMBIO_CLAVE;
          iniciarPrueba("T6 clave vencida", "1234#", CAMBIO_CLAVE, 5000);
          break;
        case 6: // T7: fuera de horario -> ERR
          usuarios[0].usos = 0;
          testHoraActiva = true; testHoraForzada = 3;  // 3am, SEGuridad esta 0-24 pero OPERARIO 8-17
          iniciarPrueba("T7 fuera hora", "2345#", ERR, 5000);
          break;
      }
    }
    verificar();

    // Fin de todas las pruebas
    if (testN >= 7 && !testActivo) {
      Serial.println(F("\n========================================"));
      Serial.print(F(" RESULTADOS: PASS="));
      Serial.print(testsOk);
      Serial.print(F("  FAIL="));
      Serial.println(testsFail);
      Serial.println(F("========================================"));
      Serial.println(F(" Ahora modo normal con teclado real."));
      enTest = false;
      testHoraActiva = false;
      resetEstado();
    }
  }
}
