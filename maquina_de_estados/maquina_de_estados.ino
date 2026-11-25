#include <StateMachineLib.h>

// ------------- ESTADOS -------------
enum Estado {
  INICIO = 0,
  VALIDACION,
  ACCESO,
  CAMBIO_CLAVE,
  MONITOR,
  PMV_ALTO,
  PMV_BAJO,
  ALARMA,
  BLOQUEO
};
const int NUM_ESTADOS = 9;
const int NUM_TRANSICIONES = 17;

// ------------- ENTRADAS -------------
enum Entrada {
  IN_NINGUNA = 0,
  IN_CLAVE_LISTA,
  IN_CLAVE_OK,
  IN_INTENTOS_MAX,
  IN_TIMEOUT,
  IN_USOS_MAX,
  IN_PUERTA_ABIERTA,
  IN_INTRUSION,
  IN_CLAVE_NUEVA_OK,
  IN_PMV_ALTO,
  IN_PMV_BAJO,
  IN_TEMP_ALTA,
  IN_TECLA_HASH,
  IN_BOTON
};

Entrada entrada = IN_NINGUNA;
bool alarmaPorTemp = false;

// ------------- MÁQUINA -------------
StateMachine maquina(NUM_ESTADOS, NUM_TRANSICIONES);

void configurarMaquina() {
  // INICIO
  maquina.AddTransition(INICIO, VALIDACION, []() { return entrada == IN_CLAVE_LISTA; });
  maquina.AddTransition(INICIO, ALARMA,     []() { return entrada == IN_INTRUSION; });

  // VALIDACION
  maquina.AddTransition(VALIDACION, ACCESO, []() { return entrada == IN_CLAVE_OK; });
  maquina.AddTransition(VALIDACION, INICIO, []() { return entrada == IN_TIMEOUT; });
  maquina.AddTransition(VALIDACION, ALARMA, []() { return entrada == IN_INTENTOS_MAX; });

  // ACCESO
  maquina.AddTransition(ACCESO, MONITOR,      []() { return entrada == IN_TIMEOUT; });
  maquina.AddTransition(ACCESO, CAMBIO_CLAVE, []() { return entrada == IN_USOS_MAX; });
  maquina.AddTransition(ACCESO, ALARMA,       []() { return entrada == IN_PUERTA_ABIERTA; });

  // CAMBIO_CLAVE
  maquina.AddTransition(CAMBIO_CLAVE, MONITOR, []() { return entrada == IN_CLAVE_NUEVA_OK; });

  // MONITOR
  maquina.AddTransition(MONITOR, INICIO,   []() { return entrada == IN_TIMEOUT; });
  maquina.AddTransition(MONITOR, PMV_ALTO, []() { return entrada == IN_PMV_ALTO; });
  maquina.AddTransition(MONITOR, PMV_BAJO, []() { return entrada == IN_PMV_BAJO; });
  maquina.AddTransition(MONITOR, ALARMA,   []() { return entrada == IN_TEMP_ALTA; });

  // PMV
  maquina.AddTransition(PMV_ALTO, MONITOR, []() { return entrada == IN_TIMEOUT; });
  maquina.AddTransition(PMV_BAJO, MONITOR, []() { return entrada == IN_TIMEOUT; });

  // ALARMA
  maquina.AddTransition(ALARMA, BLOQUEO, []() { return entrada == IN_TIMEOUT; });
  maquina.AddTransition(ALARMA, INICIO,  []() { return entrada == IN_TECLA_HASH && alarmaPorTemp; });

  // BLOQUEO
  maquina.AddTransition(BLOQUEO, INICIO, []() { return entrada == IN_TIMEOUT || entrada == IN_BOTON; });

  maquina.SetState(INICIO, false, true);
}

void setup() {
  Serial.begin(9600);
  configurarMaquina();
}

void loop() {
  maquina.Update();
  entrada = IN_NINGUNA;
}
