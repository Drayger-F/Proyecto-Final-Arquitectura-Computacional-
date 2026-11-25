// ============================================================================
//  Monitor.ino  -  Clase Monitoreo
//
//  Qué cubre: los estados MONITOR, PMV_ALTO y PMV_BAJO del diagrama.
//  Qué NO cubre (por ahora): alarma, fuego, temperatura > 30, ni ningún
//  otro estado.
//
//  Cómo lo usa INICIO / la clase principal:
//     - Al entrar/salir de cada estado:  entrarX() / salirX()
//     - En el loop() mientras está en ese estado:  actualizarX()
//     - En las condiciones de las transiciones:  pmvAlto(), pmvBajo(),
//       pmvEnRango(), tiempoMonitorCumplido(), tiempoPmvAltoCumplido(),
//       tiempoPmvBajoCumplido()
//     - Al volver a INICIO (o ir a otro estado fuera del monitoreo):  reiniciar()
//
//  Sensor: termistor NTC analógico (en Wokwi: NTC en A1; en físico: KY-013)
//  -> solo mide TEMPERATURA. La fórmula se elige con NTC_WOKWI en Pines.h.
//  La HUMEDAD queda en un valor fijo (HUMEDAD_FIJA) hasta que el equipo decida
//  qué sensor usar: solo hay que cambiar leerHumedad().
//
//  Librerías: AsyncTaskLib (tiempos), Servo (rejilla), LiquidCrystal (pantalla).
//  Average (promedio de lecturas) está DESACTIVADA por ahora, comentada con
//  "// [AVERAGE]" en cada lugar donde se usaba. Para reactivarla, buscar esas
//  marcas y descomentar. Mientras tanto la temperatura no se promedia entre
//  lecturas (aunque leerTemperatura() ya promedia 10 muestras).
//
//  IMPORTANTE: este es el archivo principal de la carpeta (se llama igual que
//  la carpeta), por eso Arduino lo une primero. No tiene setup() ni loop():
//  esos están en PruebaMonitoreo.ino. Al integrarlo con el proyecto de los
//  demás se BORRA la pestaña PruebaMonitoreo.ino.
// ============================================================================

#include <LiquidCrystal.h>
#include <Servo.h>
#include <AsyncTaskLib.h>
// #include <Average.h>      // [AVERAGE] desactivada: Wokwi no la tiene
#include "Pines.h"

// ------------------------- Parámetros del diagrama --------------------------
namespace ConfigMonitoreo {
  constexpr float         PMV_UMBRAL_ALTO       = 0.5f;   // PMV > 0.5  -> PMV_ALTO
  constexpr float         PMV_UMBRAL_BAJO       = -0.7f;  // PMV < -0.7 -> PMV_BAJO

  constexpr unsigned long T_MONITOR_MS          = 3000;   // MONITOR -> INICIO (PMV en rango)
  constexpr unsigned long T_PMV_ALTO_MS         = 3000;   // PMV_ALTO -> MONITOR
  constexpr unsigned long T_PMV_BAJO_MS         = 2000;   // PMV_BAJO -> MONITOR

  constexpr unsigned int  LED_ROJO_ON_MS        = 100;    // PMV_ALTO
  constexpr unsigned int  LED_ROJO_OFF_MS       = 500;
  constexpr unsigned int  LED_AZUL_ON_MS        = 100;    // PMV_BAJO
  constexpr unsigned int  LED_AZUL_OFF_MS       = 400;

  constexpr uint8_t       REJILLA_ABIERTA_GRADOS = 90;    // ajustar al montaje real
  constexpr uint8_t       REJILLA_CERRADA_GRADOS = 0;

  constexpr unsigned long PERIODO_LECTURA_MS    = 1000;   // cada cuánto se lee la temperatura
  constexpr uint16_t      VENTANA_PROMEDIO      = 5;      // [AVERAGE] lecturas que se promedian (sin uso por ahora)
  constexpr uint8_t       ERRORES_MAX           = 3;      // lecturas malas seguidas = sensor caído
  constexpr float         HUMEDAD_FIJA          = 50.0f;  // % (no hay sensor de humedad aún)
}
using namespace ConfigMonitoreo;

// ------------------ Parpadeo de LED sin bloquear (ON/OFF distintos) ---------
// Se hace a mano porque AsyncTask usa un único intervalo y aquí ON y OFF son
// distintos (100/500 ms y 100/400 ms).
class Parpadeo {
 public:
  void iniciar(uint8_t pin, unsigned int msOn, unsigned int msOff) {
    _pin = pin;
    _msOn = msOn;
    _msOff = msOff;
    _activo = true;
    _encendido = true;
    _t = millis();
    digitalWrite(_pin, LED_ON);
  }

  void actualizar() {                    // llamar en cada loop()
    if (!_activo) return;
    unsigned long espera = _encendido ? _msOn : _msOff;
    if (millis() - _t >= espera) {
      _encendido = !_encendido;
      _t = millis();
      digitalWrite(_pin, _encendido ? LED_ON : LED_OFF);
    }
  }

  void detener() {                       // apaga el LED y deja de parpadear
    if (_pin != 0) digitalWrite(_pin, LED_OFF);
    _activo = false;
    _encendido = false;
  }

 private:
  uint8_t       _pin       = 0;
  unsigned int  _msOn      = 0;
  unsigned int  _msOff     = 0;
  bool          _activo    = false;
  bool          _encendido = false;
  unsigned long _t         = 0;
};

// -------- AsyncTask solo acepta funciones simples (no métodos) como callback.
// -------- Estas funciones llaman a la única instancia de Monitoreo.
class Monitoreo;
Monitoreo *g_monitoreo = nullptr;
void cbLectura();
void cbFinMonitor();
void cbFinPmvAlto();
void cbFinPmvBajo();

class Monitoreo {
 public:
  // El LCD lo crea y comparte la clase principal (también lo usa LiquidMenu).
  Monitoreo(LiquidCrystal &lcd)
    : _lcd(lcd),
      // _promTemp(VENTANA_PROMEDIO),                   // [AVERAGE]
      _tLectura(PERIODO_LECTURA_MS, true, cbLectura),   // se repite cada 1 s
      _tMonitor(T_MONITOR_MS, false, cbFinMonitor),     // una sola vez
      _tPmvAlto(T_PMV_ALTO_MS, false, cbFinPmvAlto),
      _tPmvBajo(T_PMV_BAJO_MS, false, cbFinPmvBajo) {
    g_monitoreo = this;
  }

  // ------------------------------ Inicio -----------------------------------
  void begin() {                                 // llamar una vez en setup()
    pinMode(PIN_VENTILADOR, OUTPUT);
    pinMode(PIN_LED_ROJO, OUTPUT);
    pinMode(PIN_LED_AZUL, OUTPUT);
    digitalWrite(PIN_VENTILADOR, LOW);
    digitalWrite(PIN_LED_ROJO, LED_OFF);     // LED RGB de ánodo común: apagado = HIGH
    digitalWrite(PIN_LED_AZUL, LED_OFF);
    _servo.attach(PIN_SERVO_REJILLA);
    abrirRejilla();
  }

  // ------------------------- Lectura y cálculo -----------------------------

  // Temperatura del KY-013 en °C. Devuelve NAN si el sensor está desconectado.
  float leerTemperatura() {
    long suma = 0;
    for (int i = 0; i < 10; i++) {               // 10 muestras: el pin analógico es ruidoso
      suma += analogRead(PIN_TEMP);
      delay(2);
    }
    float raw = suma / 10.0f;
    if (raw < 1.0f || raw > 1022.0f) return NAN; // sensor suelto o en corto

#if NTC_WOKWI
    // NTC de Wokwi: termistor de 10k en serie con una resistencia de 10k, BETA 3950.
    const float BETA = 3950.0f;
    return 1.0f / (log(1.0f / (1023.0f / raw - 1.0f)) / BETA + 1.0f / 298.15f) - 273.15f;
#else
    // KY-013 real: Steinhart-Hart con valores típicos de un NTC de 10 kΩ.
    // Comparar con un termómetro y ajustar si hace falta.
    float r = log(10000.0f * (1023.0f / raw - 1.0f));
    float kelvin = 1.0f / (0.001129148f + (0.000234125f + (0.0000000876741f * r * r)) * r);
    return kelvin - 273.15f;
#endif
  }

  // Humedad en %. No hay sensor: valor fijo. Cambiar aquí cuando haya uno.
  float leerHumedad() { return HUMEDAD_FIJA; }

  // PMV aproximado (NO es el modelo completo de Fanger).
  //   PMV = 0.30*(T-24) + 0.01*(H-50)
  // Los coeficientes son un punto de partida: calibrarlos para que los
  // umbrales 0.5 y -0.7 se activen con valores razonables.
  float calcularPMV(float temp, float hum) {
    return 0.30f * (temp - 24.0f) + 0.01f * (hum - 50.0f);
  }

  // Lee sensores y actualiza temperatura, humedad y PMV.
  void actualizarLectura() {
    float t = leerTemperatura();
    float h = leerHumedad();
    if (isnan(t) || isnan(h)) {                  // lectura mala: se conserva la anterior
      if (_fallosSeguidos < 255) _fallosSeguidos++;
      return;
    }
    _fallosSeguidos = 0;

    // _promTemp.push(t);                // [AVERAGE]
    // _temp = _promTemp.mean();         // [AVERAGE]
    _temp = t;                           // sin promedio entre lecturas (por ahora)
    _hum  = h;
    _pmv  = calcularPMV(_temp, _hum);
    _pmvValido = true;
    ajustarActuadoresSegunPMV();
  }

  // SOLO PARA PRUEBAS: fija temp y hum sin sensor y recalcula el PMV.
  void simularLectura(float temp, float hum) {
    _temp = temp;
    _hum  = hum;
    _pmv  = calcularPMV(_temp, _hum);
    _pmvValido = true;
    _fallosSeguidos = 0;
  }

  // ------------------------------- Datos -----------------------------------
  float getTemperatura() { return _temp; }
  float getHumedad()     { return _hum; }
  float getPMV()         { return _pmv; }

  // ------------------------------ LCD --------------------------------------
  //   T:25.3°C
  //   H:50% P:0.4
  void mostrarEnLcd() {
    if (_fallosSeguidos >= ERRORES_MAX) {
      _lcd.setCursor(0, 0);
      _lcd.print("ERROR SENSOR    ");
      _lcd.setCursor(0, 1);
      _lcd.print("Revisar KY-013  ");
      return;
    }
    _lcd.setCursor(0, 0);
    _lcd.print("T:");
    _lcd.print(_temp, 1);
    _lcd.print((char)223);                       // símbolo de grados del HD44780
    _lcd.print("C     ");
    _lcd.setCursor(0, 1);
    _lcd.print("H:");
    _lcd.print(_hum, 0);
    _lcd.print("% P:");
    _lcd.print(_pmv, 1);
    _lcd.print("    ");
  }

  // ---------------------------- Estado MONITOR -----------------------------
  void entrarMonitor() {                         // SetOnEntering(MONITOR)
    _finMonitor = false;
    reiniciarTarea(_tMonitor);
    reiniciarTarea(_tLectura);
    actualizarLectura();                         // lectura inmediata
    mostrarEnLcd();
  }

  void actualizarMonitor() {                     // en loop() mientras estado == MONITOR
    _tLectura.Update();                          // cada 1 s -> tareaLectura()
    _tMonitor.Update();                          // a los 3 s -> _finMonitor = true
  }

  void salirMonitor() {                          // SetOnLeaving(MONITOR)
    // Solo se detienen las tareas. Los actuadores NO se tocan: al salir hacia
    // PMV_ALTO / PMV_BAJO justamente deben quedar activos.
    _tLectura.Stop();
    _tMonitor.Stop();
  }

  // ------------- Estado PMV_ALTO (ventilador ON, LED rojo 100/500) ---------
  void entrarPmvAlto() {
    _finPmvAlto = false;
    reiniciarTarea(_tPmvAlto);
    encenderVentilador();
    _ledRojo.iniciar(PIN_LED_ROJO, LED_ROJO_ON_MS, LED_ROJO_OFF_MS);
  }

  void actualizarPmvAlto() {
    _tPmvAlto.Update();
    _ledRojo.actualizar();
  }

  void salirPmvAlto() {
    _tPmvAlto.Stop();
    _ledRojo.detener();
    // El ventilador se apaga cuando el PMV vuelve al rango (en
    // actualizarLectura) o con reiniciar().
  }

  // ------------- Estado PMV_BAJO (servo cierra, LED azul 100/400) ----------
  void entrarPmvBajo() {
    _finPmvBajo = false;
    reiniciarTarea(_tPmvBajo);
    cerrarRejilla();
    _ledAzul.iniciar(PIN_LED_AZUL, LED_AZUL_ON_MS, LED_AZUL_OFF_MS);
  }

  void actualizarPmvBajo() {
    _tPmvBajo.Update();
    _ledAzul.actualizar();
  }

  void salirPmvBajo() {
    _tPmvBajo.Stop();
    _ledAzul.detener();
    // La rejilla se reabre cuando el PMV vuelve al rango o con reiniciar().
  }

  // ----------------- Condiciones para las transiciones ---------------------
  bool pmvAlto()    { return _pmvValido && _pmv > PMV_UMBRAL_ALTO; }   // PMV > 0.5
  bool pmvBajo()    { return _pmvValido && _pmv < PMV_UMBRAL_BAJO; }   // PMV < -0.7
  bool pmvEnRango() { return _pmvValido && !pmvAlto() && !pmvBajo(); }
  bool tiempoMonitorCumplido()   { return _finMonitor; }               // 3 s en MONITOR
  bool tiempoPmvAltoCumplido()   { return _finPmvAlto; }               // 3 s en PMV_ALTO
  bool tiempoPmvBajoCumplido()   { return _finPmvBajo; }               // 2 s en PMV_BAJO

  // ------------------------------ Actuadores -------------------------------
  void encenderVentilador() {
    digitalWrite(PIN_VENTILADOR, HIGH);
    _ventiladorOn = true;
  }
  void apagarVentilador() {
    digitalWrite(PIN_VENTILADOR, LOW);
    _ventiladorOn = false;
  }
  void cerrarRejilla() {
    _servo.write(REJILLA_CERRADA_GRADOS);
    _rejillaCerrada = true;
  }
  void abrirRejilla() {
    _servo.write(REJILLA_ABIERTA_GRADOS);
    _rejillaCerrada = false;
  }
  bool ventiladorEncendido() { return _ventiladorOn; }
  bool rejillaCerrada()      { return _rejillaCerrada; }

  // Apaga ventilador y LEDs, abre la rejilla y reinicia tiempos y lecturas.
  // Llamarlo al volver a INICIO (o al salir del monitoreo hacia otro estado).
  void reiniciar() {
    _tLectura.Stop();
    _tMonitor.Stop();
    _tPmvAlto.Stop();
    _tPmvBajo.Stop();
    _ledRojo.detener();
    _ledAzul.detener();
    apagarVentilador();
    abrirRejilla();
    _finMonitor = false;
    _finPmvAlto = false;
    _finPmvBajo = false;
    _pmvValido = false;          // hasta que haya una lectura nueva
    _fallosSeguidos = 0;
  }

  // ---- Uso interno: los llaman las tareas de AsyncTask (no usar desde fuera) ----
  void tareaLectura() {
    actualizarLectura();
    mostrarEnLcd();
  }
  void marcarFinMonitor() { _finMonitor = true; }
  void marcarFinPmvAlto() { _finPmvAlto = true; }
  void marcarFinPmvBajo() { _finPmvBajo = true; }

 private:
  // Hardware
  LiquidCrystal &_lcd;
  Servo          _servo;

  // Tareas (y promedio, desactivado)
  // Average<float> _promTemp;   // [AVERAGE]
  AsyncTask      _tLectura;     // lectura periódica de temperatura
  AsyncTask      _tMonitor;     // 3 s en MONITOR
  AsyncTask      _tPmvAlto;     // 3 s en PMV_ALTO
  AsyncTask      _tPmvBajo;     // 2 s en PMV_BAJO
  Parpadeo       _ledRojo;
  Parpadeo       _ledAzul;

  // Datos
  float   _temp     = 0.0f;
  float   _hum      = 0.0f;
  float   _pmv      = 0.0f;
  bool    _pmvValido      = false;   // false hasta la primera lectura buena
  uint8_t _fallosSeguidos = 0;

  // Banderas de tiempo (las pone AsyncTask)
  bool _finMonitor = false;
  bool _finPmvAlto = false;
  bool _finPmvBajo = false;

  // Estado de actuadores
  bool _ventiladorOn   = false;
  bool _rejillaCerrada = false;

  // Si el PMV ya volvió al rango, se apagan los actuadores que quedaron activos.
  void ajustarActuadoresSegunPMV() {
    if (_ventiladorOn   && _pmv <= PMV_UMBRAL_ALTO) apagarVentilador();
    if (_rejillaCerrada && _pmv >= PMV_UMBRAL_BAJO) abrirRejilla();
  }

  // Reinicia el conteo de una AsyncTask (parar, reiniciar y volver a iniciar).
  static void reiniciarTarea(AsyncTask &t) {
    t.Stop();
    t.Reset();
    t.Start();
  }
};

void cbLectura()    { if (g_monitoreo) g_monitoreo->tareaLectura();     }
void cbFinMonitor() { if (g_monitoreo) g_monitoreo->marcarFinMonitor(); }
void cbFinPmvAlto() { if (g_monitoreo) g_monitoreo->marcarFinPmvAlto(); }
void cbFinPmvBajo() { if (g_monitoreo) g_monitoreo->marcarFinPmvBajo(); }
