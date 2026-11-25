// ============================================================================
//  PruebaMonitoreo.ino  -  Clase que prueba TODOS los métodos de Monitoreo
//
//  Cómo usarla:
//    1. Compilar y subir a la placa.
//    2. Abrir el Monitor Serie (Tools -> Serial Monitor) a 9600 baudios.
//    3. Se imprime [ OK ] o [ FALLA ] por cada prueba y un resumen final.
//
//  Para las pruebas de PMV se usa simularLectura(), así no hace falta el
//  sensor conectado. Solo "probarSensores" necesita el NTC conectado (en el
//  diagrama de Wokwi: OUT a A1).
//
//  Esta pestaña tiene setup() y loop(): al integrar con el proyecto de los
//  demás se BORRA esta pestaña (y los objetos lcd y monitoreo de abajo).
// ============================================================================

class PruebaMonitoreo {
 public:
  explicit PruebaMonitoreo(Monitoreo &m) : _m(m) {}

  void ejecutarTodas() {
    Serial.println(F("===== PRUEBAS DE MONITOREO ====="));
    probarSensores();
    probarCalculoPMV();
    probarClasificacionPMV();
    probarLcd();
    probarActuadores();
    probarEstadoMonitor();
    probarEstadoPmvAlto();
    probarEstadoPmvBajo();
    probarReiniciar();
    resumen();
  }

 private:
  Monitoreo &_m;
  uint8_t _ok = 0;
  uint8_t _fallos = 0;

  // ------------------------------ Utilidades -------------------------------
  void titulo(const __FlashStringHelper *texto) {
    Serial.println();
    Serial.print(F("--- "));
    Serial.println(texto);
  }

  void verificar(const __FlashStringHelper *nombre, bool condicion) {
    Serial.print(condicion ? F("[ OK    ] ") : F("[ FALLA ] "));
    Serial.println(nombre);
    if (condicion) _ok++; else _fallos++;
  }

  // Esperan "ms" milisegundos llamando al actualizar del estado correspondiente
  // (las tareas por tiempo necesitan que se las actualice en cada vuelta).
  void esperarMonitor(unsigned long ms) {
    unsigned long t = millis();
    while (millis() - t < ms) _m.actualizarMonitor();
  }
  void esperarPmvAlto(unsigned long ms) {
    unsigned long t = millis();
    while (millis() - t < ms) _m.actualizarPmvAlto();
  }
  void esperarPmvBajo(unsigned long ms) {
    unsigned long t = millis();
    while (millis() - t < ms) _m.actualizarPmvBajo();
  }

  // -------------------------------- Pruebas --------------------------------
  // Necesita el KY-013 conectado (la humedad es un valor fijo).
  void probarSensores() {
    titulo(F("Sensores (necesita el NTC conectado)"));
    float t = _m.leerTemperatura();
    float h = _m.leerHumedad();
    Serial.print(F("   leerTemperatura() = ")); Serial.println(t);
    Serial.print(F("   leerHumedad()     = ")); Serial.println(h);
    verificar(F("leerTemperatura() entre 5 y 60 C"), t > 5.0f && t < 60.0f);
    verificar(F("leerHumedad() entre 1 y 100 %"),    h > 1.0f && h <= 100.0f);

    _m.actualizarLectura();
    verificar(F("actualizarLectura() -> getTemperatura() valida"), _m.getTemperatura() > 5.0f);
    verificar(F("actualizarLectura() -> getHumedad() valida"),     _m.getHumedad() > 1.0f);
  }

  void probarCalculoPMV() {
    titulo(F("calcularPMV (propiedades, no valores exactos)"));
    float neutro = _m.calcularPMV(24, 50);
    float calor  = _m.calcularPMV(30, 50);
    float frio   = _m.calcularPMV(18, 50);
    float humedo = _m.calcularPMV(24, 80);

    verificar(F("PMV neutro (24C, 50%) cerca de 0"), fabs(neutro) < 0.3f);
    verificar(F("mas calor     -> PMV mayor"), calor > neutro);
    verificar(F("mas frio      -> PMV menor"), frio < neutro);
    verificar(F("mas humedad   -> PMV mayor"), humedo > neutro);
  }

  void probarClasificacionPMV() {
    titulo(F("pmvAlto / pmvBajo / pmvEnRango (con simularLectura)"));

    _m.simularLectura(32, 70);
    verificar(F("32C 70% -> pmvAlto()"),                _m.pmvAlto());
    verificar(F("   y NO pmvBajo()"),                  !_m.pmvBajo());
    verificar(F("   y NO pmvEnRango()"),               !_m.pmvEnRango());

    _m.simularLectura(16, 40);
    verificar(F("16C 40% -> pmvBajo()"),                _m.pmvBajo());
    verificar(F("   y NO pmvAlto()"),                  !_m.pmvAlto());
    verificar(F("   y NO pmvEnRango()"),               !_m.pmvEnRango());

    _m.simularLectura(24, 50);
    verificar(F("24C 50% -> pmvEnRango()"),             _m.pmvEnRango());
    verificar(F("   y NO pmvAlto()"),                  !_m.pmvAlto());
    verificar(F("   y NO pmvBajo()"),                  !_m.pmvBajo());
  }

  void probarLcd() {
    titulo(F("LCD (revisar a ojo)"));
    _m.simularLectura(25.3f, 60.0f);
    _m.mostrarEnLcd();
    Serial.println(F("[ MANUAL ] mostrarEnLcd(): el LCD debe mostrar T:25.3C y H:60%"));
  }

  void probarActuadores() {
    titulo(F("Actuadores"));
    _m.apagarVentilador();
    verificar(F("apagarVentilador() -> ventiladorEncendido() == false"), !_m.ventiladorEncendido());
    _m.encenderVentilador();
    verificar(F("encenderVentilador() -> ventiladorEncendido() == true"), _m.ventiladorEncendido());
    verificar(F("   pin del ventilador en HIGH"), digitalRead(PIN_VENTILADOR) == HIGH);
    _m.apagarVentilador();
    verificar(F("   pin del ventilador en LOW al apagar"), digitalRead(PIN_VENTILADOR) == LOW);

    _m.abrirRejilla();
    verificar(F("abrirRejilla() -> rejillaCerrada() == false"), !_m.rejillaCerrada());
    _m.cerrarRejilla();
    verificar(F("cerrarRejilla() -> rejillaCerrada() == true"), _m.rejillaCerrada());
    _m.abrirRejilla();
  }

  void probarEstadoMonitor() {
    titulo(F("Estado MONITOR (dura 3 s)"));
    _m.reiniciar();
    _m.entrarMonitor();
    verificar(F("entrarMonitor() -> tiempoMonitorCumplido() == false"), !_m.tiempoMonitorCumplido());
    esperarMonitor(2500);
    verificar(F("a los 2.5 s todavia NO se cumple"), !_m.tiempoMonitorCumplido());
    esperarMonitor(700);
    verificar(F("a los 3.2 s SI se cumple"), _m.tiempoMonitorCumplido());
    _m.salirMonitor();
  }

  void probarEstadoPmvAlto() {
    titulo(F("Estado PMV_ALTO (ventilador, LED rojo 100/500, dura 3 s)"));
    _m.reiniciar();
    _m.entrarPmvAlto();
    verificar(F("entrarPmvAlto() -> ventilador encendido"), _m.ventiladorEncendido());
    verificar(F("   LED rojo encendido al entrar"), digitalRead(PIN_LED_ROJO) == LED_ON);
    verificar(F("   tiempoPmvAltoCumplido() == false"), !_m.tiempoPmvAltoCumplido());

    esperarPmvAlto(150);
    verificar(F("a los 150 ms el LED rojo esta apagado"), digitalRead(PIN_LED_ROJO) == LED_OFF);
    esperarPmvAlto(500);
    verificar(F("a los 650 ms el LED rojo vuelve a encender"), digitalRead(PIN_LED_ROJO) == LED_ON);
    verificar(F("a los 650 ms todavia NO se cumple el tiempo"), !_m.tiempoPmvAltoCumplido());

    esperarPmvAlto(2500);
    verificar(F("a los 3.1 s SI se cumple el tiempo"), _m.tiempoPmvAltoCumplido());

    _m.salirPmvAlto();
    verificar(F("salirPmvAlto() -> LED rojo apagado"), digitalRead(PIN_LED_ROJO) == LED_OFF);
  }

  void probarEstadoPmvBajo() {
    titulo(F("Estado PMV_BAJO (servo, LED azul 100/400, dura 2 s)"));
    _m.reiniciar();
    _m.entrarPmvBajo();
    verificar(F("entrarPmvBajo() -> rejilla cerrada"), _m.rejillaCerrada());
    verificar(F("   LED azul encendido al entrar"), digitalRead(PIN_LED_AZUL) == LED_ON);
    verificar(F("   tiempoPmvBajoCumplido() == false"), !_m.tiempoPmvBajoCumplido());

    esperarPmvBajo(150);
    verificar(F("a los 150 ms el LED azul esta apagado"), digitalRead(PIN_LED_AZUL) == LED_OFF);
    esperarPmvBajo(400);
    verificar(F("a los 550 ms el LED azul vuelve a encender"), digitalRead(PIN_LED_AZUL) == LED_ON);

    esperarPmvBajo(1000);
    verificar(F("a los 1.5 s todavia NO se cumple el tiempo"), !_m.tiempoPmvBajoCumplido());
    esperarPmvBajo(600);
    verificar(F("a los 2.1 s SI se cumple el tiempo"), _m.tiempoPmvBajoCumplido());

    _m.salirPmvBajo();
    verificar(F("salirPmvBajo() -> LED azul apagado"), digitalRead(PIN_LED_AZUL) == LED_OFF);
  }

  void probarReiniciar() {
    titulo(F("reiniciar()"));
    _m.encenderVentilador();
    _m.cerrarRejilla();
    _m.reiniciar();
    verificar(F("ventilador apagado"),       !_m.ventiladorEncendido());
    verificar(F("rejilla abierta"),          !_m.rejillaCerrada());
    verificar(F("LED rojo apagado"),          digitalRead(PIN_LED_ROJO) == LED_OFF);
    verificar(F("LED azul apagado"),          digitalRead(PIN_LED_AZUL) == LED_OFF);
  }

  void resumen() {
    Serial.println();
    Serial.println(F("===== RESUMEN ====="));
    Serial.print(F("OK:     ")); Serial.println(_ok);
    Serial.print(F("FALLAS: ")); Serial.println(_fallos);
  }
};

// ------------------------- Objetos y arranque (solo de prueba) --------------
LiquidCrystal lcd(LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7);
Monitoreo monitoreo(lcd);
PruebaMonitoreo prueba(monitoreo);

void setup() {
  Serial.begin(9600);
  lcd.begin(LCD_COLUMNAS, LCD_FILAS);
  monitoreo.begin();
  delay(500);
  prueba.ejecutarTodas();
}

void loop() {
  // Las pruebas corren una sola vez. Para repetirlas, pulsar RESET en la placa.
}
