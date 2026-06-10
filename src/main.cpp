#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <time.h>
#include <piezo-music.h>
#include <example-music.h>

// =================== PROTOTIPOS ===================
void tareaLCD(void* parametro);
void copiarSecuencia();
void ejecutarAccion(byte accion);
void sincronizarNTP(String ssid_ext, String pass_ext);
// ==================================================

// =================== PINES ===================
#define PIN_BUZZER 32
#define PIN_RELE   33
const int pinesLED[] = {25, 26, 27};
int canal = 1;
// =============================================

// =====================================================================
// TIPOS DE ACCIONES
// Úsalos en sec1[] y sec2[] para indicar qué hace cada evento.
// Puedes combinar hasta 10 acciones por entrada de alarma (una tras otra).
//
// PARA AGREGAR UNA NUEVA ACCIÓN:
//   1. Define una constante aquí con el siguiente número disponible.
//      Ejemplo: #define TONO_500HZ  12
//   2. Crea su función en la sección "FUNCIONES DE EVENTOS" más abajo.
//   3. Agrégala con su case en ejecutarAccion().
// =====================================================================
#define SIN_ACCION    0   // No hace nada. Valor por defecto al dejar espacios vacíos.
#define RELE          1   // Activa el relé.
#define LED_1         2   // Enciende el LED 1.
#define LED_2         3   // Enciende el LED 2.
#define LED_3         4   // Enciende el LED 3.
#define SEQ_LEDS      5   // Enciende los 3 LEDs uno por uno, en secuencia.
#define PARPADEO_LEDS 6   // Alterna patrón de parpadeo en los 3 LEDs.
#define TONO_800HZ    7   // Zumbido a 800 Hz.
#define TONO_1000HZ   8   // Zumbido a 1000 Hz.
#define TONO_1200HZ   9   // Zumbido a 1200 Hz.
#define TONO_1500HZ   10  // Zumbido a 1500 Hz.
#define MELODIA_ZELDA   11  // Melodía del tema principal de Zelda.
#define MELODIA_TWINKLE 12  // Melodía Twinkle Twinkle Little Star.
#define MELODIA_TETRIS  13  // Melodía del tema A de Tetris.
#define MELODIA_MARIO   14  // Melodía del tema principal de Super Mario.

// Duración de cada acción en milisegundos. Aplica a TODAS las acciones, incluidas las melodías.
// Cambiar este valor afecta a todas las acciones por igual.
#define DURACION_ACCION_MS 10000

// =================== OBJETOS PERIFÉRICOS ===================
RTC_DS3231 rtc;
LiquidCrystal_I2C lcd(0x27, 16, 2);
AsyncWebServer servidor(80);
// ===========================================================

// =================== WIFI AP ===================
const char* ssid     = "Alarma";
const char* password = "87654321";
IPAddress local_IP(192, 168, 4, 1);   // El AP debe estar en la misma IP que el gateway
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);
// ===============================================

byte Eleccion             = 0;
byte EleccionProximaSemana = 0;

// Variables para la sincronización NTP (la solicitud llega desde la web y se procesa en el loop).
volatile bool solicitudSyncNTP = false;
String ssidNTP = "";
String passNTP = "";
String resultadoSync = "";  // Muestra el resultado del último intento en la página web.

// =====================================================================
// STRUCT ALARMA
// Cada entrada define cuándo ocurre el evento y qué acciones ejecuta.
//   - dia:      1 = Lunes a Viernes  |  6 = Sábado
//   - hora:     0 - 23
//   - minuto:   0 - 59
//   - acciones: hasta 10 acciones en orden. SIN_ACCION (0) termina la lista
//               automáticamente, no hace falta completar los 10 campos.
// =====================================================================
struct Alarma {
  byte dia;
  byte hora;
  byte minuto;
  byte acciones[10];
};

// =====================================================================
// SECUENCIAS DE EVENTOS
// =====================================================================
// CÓMO AGREGAR O MODIFICAR EVENTOS:
//
//   Formato: {dia, hora, minuto, {ACCION1, ACCION2, ...}}
//
//   Ejemplos:
//     {1, 7,  0,  {RELE}}                    -> solo relé a las 7:00 L-V
//     {1, 8, 30,  {LED_1, TONO_1000HZ}}      -> LED 1 y luego buzzer a las 8:30
//     {6, 13, 0,  {SEQ_LEDS, MELODIA_ZELDA}} -> secuencia LEDs y luego melodía el sábado
//
//   PARA CAMBIAR EL NÚMERO DE EVENTOS: agrega o quita líneas libremente.
//   El sistema calcula el total automáticamente, no hay ningún número fijo que tocar.
// =====================================================================

Alarma sec1[] = {
  {1, 7,  0,  {RELE}},
  {1, 7,  30, {LED_1}},
  {1, 8,  0,  {TONO_1000HZ}},
  {1, 8,  30, {LED_2}},
  {1, 9,  0,  {TONO_1200HZ}},
  {1, 9,  30, {LED_3}},
  {1, 13, 0,  {RELE}},
  {1, 13, 30, {SEQ_LEDS}},
  {1, 14, 0,  {TONO_800HZ}},
  {1, 14, 30, {PARPADEO_LEDS}},
  {1, 15, 0,  {TONO_1500HZ}},
  {1, 15, 30, {MELODIA_ZELDA}},
  {1, 22, 27,  {RELE}},
  {1, 22, 28, {LED_2,PARPADEO_LEDS}},
  {1, 22, 29,  {TONO_1000HZ,TONO_1200HZ}},
  {1, 22, 30, {MELODIA_TETRIS}},
  {1, 22, 35,  {MELODIA_MARIO,MELODIA_TETRIS,MELODIA_MARIO}},
  {6, 7,  0,  {RELE}},
  {6, 7,  30, {LED_1}},
  {6, 8,  0,  {TONO_1000HZ}},
  {6, 8,  30, {LED_2}},
  {6, 13, 0,  {RELE}},
  {6, 13, 30, {LED_3}},
  {6, 14, 0,  {TONO_1200HZ}},
  {6, 14, 30, {SEQ_LEDS}},
  {6, 19, 0,  {RELE}},
  {6, 19, 30, {PARPADEO_LEDS}},
  {6, 20, 0,  {MELODIA_ZELDA}},
};

Alarma sec2[] = {
  {1, 16, 55, {MELODIA_ZELDA}},
  {1, 6,  30, {LED_3}},
  {1, 7,  0,  {TONO_800HZ}},
  {1, 7,  30, {SEQ_LEDS}},
  {1, 8,  0,  {TONO_1500HZ}},
  {1, 8,  30, {PARPADEO_LEDS}},
  {1, 12, 0,  {RELE}},
  {1, 12, 30, {MELODIA_ZELDA}},
  {1, 13, 0,  {LED_1}},
  {1, 13, 30, {TONO_1000HZ}},
  {1, 14, 0,  {LED_2}},
  {1, 14, 30, {TONO_1200HZ}},
  {1, 18, 0,  {RELE}},
  {1, 22, 10, {MELODIA_ZELDA}},
  {1, 22, 12,  {MELODIA_TWINKLE}},
  {1, 22, 14, {MELODIA_TETRIS}},
  {1, 20, 0,  {MELODIA_ZELDA}},
  {6, 6,  0,  {RELE}},
  {6, 6,  30, {LED_1}},
  {6, 7,  0,  {TONO_1000HZ}},
  {6, 7,  30, {LED_2}},
  {6, 12, 0,  {RELE}},
  {6, 12, 30, {LED_3}},
  {6, 13, 0,  {TONO_1200HZ}},
  {6, 13, 30, {SEQ_LEDS}},
  {6, 18, 0,  {RELE}},
  {6, 18, 30, {PARPADEO_LEDS}},
  {1, 22,6,  {MELODIA_ZELDA,SEQ_LEDS}},
};

// Totales calculados automáticamente a partir del tamaño real de cada arreglo.
const int N_SEC1      = sizeof(sec1) / sizeof(sec1[0]);
const int N_SEC2      = sizeof(sec2) / sizeof(sec2[0]);
const int MAX_ALARMAS = (N_SEC1 > N_SEC2) ? N_SEC1 : N_SEC2;

Alarma elegida[MAX_ALARMAS];
int    totalAlarmas = 0;  // Se actualiza en copiarSecuencia().


// =====================================================================
// FUNCIONES DE EVENTOS
// Cada función encapsula un tipo de acción. Duran DURACION_ACCION_MS ms,
// excepto las melodías que duran lo que dura la canción.
// ======================T===============================================

void activarRele() {
  digitalWrite(PIN_RELE, LOW);
  delay(DURACION_ACCION_MS);
  digitalWrite(PIN_RELE, HIGH);
}

void encenderLED(int index) {
  digitalWrite(pinesLED[index], HIGH);
  delay(DURACION_ACCION_MS);
  digitalWrite(pinesLED[index], LOW);
}

void secuenciaLEDs() {
  int tiempoPorLED = DURACION_ACCION_MS / 3;
  for (int i = 0; i < 3; i++) {
    digitalWrite(pinesLED[i], HIGH);
    delay(tiempoPorLED);
    digitalWrite(pinesLED[i], LOW);
  }
}

void parpadeoLEDs() {
  unsigned long inicio = millis();
  bool patronA = true;
  while (millis() - inicio < (unsigned long)DURACION_ACCION_MS) {
    for (int i = 0; i < 3; i++) {
      // Patrón A: LEDs 0 y 2 encendidos, LED 1 apagado. Patrón B: al revés.
      digitalWrite(pinesLED[i], patronA ? (i % 2 == 0) : (i % 2 == 1));
    }
    patronA = !patronA;
    delay(300);
  }
  for (int i = 0; i < 3; i++) digitalWrite(pinesLED[i], LOW);
}

void sonarTono(int frecuencia) {
  int ciclos = DURACION_ACCION_MS / 2000;  // beep de 1 s + 1 s de silencio
  for (int i = 0; i < ciclos; i++) {
    tone(PIN_BUZZER, frecuencia, 1000);
    delay(2000);
  }
  noTone(PIN_BUZZER);
}

// Igual que playSong() pero se detiene al llegar a DURACION_ACCION_MS.
// Si la canción es más corta que ese tiempo, termina naturalmente antes.
void tocarMelodia(int* melodia, int* ritmo, int n, int tempo) {
  int tempoMillis = 60000 / tempo;
  unsigned long inicio = millis();
  for (int i = 0; i < n; i++) {
    if (millis() - inicio >= (unsigned long)DURACION_ACCION_MS) break;
    int duraNota = tempoMillis / ritmo[i];
    tone(PIN_BUZZER, melodia[i], duraNota);
    delay((int)(duraNota * 1.30));
    noTone(PIN_BUZZER);
  }
}

// Tempos elegidos para que cada canción quepa en ~10 s (DURACION_ACCION_MS).
// Las canciones cortas (Twinkle, Tetris) terminan antes de 10 s y se detienen solas.
void reproducirMelodia_Zelda() {
  int n = sizeof(zelda_main_theme_melody) / sizeof(zelda_main_theme_melody[0]);
  tocarMelodia(zelda_main_theme_melody, zelda_main_theme_rythm, n, 74);
}

void reproducirMelodia_Twinkle() {
  int n = sizeof(twinkle_twinkle_melody) / sizeof(twinkle_twinkle_melody[0]);
  tocarMelodia(twinkle_twinkle_melody, twinkle_twinkle_rythm, n, 120);
}

void reproducirMelodia_Tetris() {
  int n = sizeof(tetris_theme_melody) / sizeof(tetris_theme_melody[0]);
  tocarMelodia(tetris_theme_melody, tetris_theme_rythm, n, 120);
}

void reproducirMelodia_Mario() {
  int n = sizeof(mario_main_theme_melody) / sizeof(mario_main_theme_melody[0]);
  tocarMelodia(mario_main_theme_melody, mario_main_theme_rythm, n, 52);
}


// =====================================================================
// DISPATCHER: llama a la función de la acción indicada.
// =====================================================================
void ejecutarAccion(byte accion) {
  switch (accion) {
    case RELE:          activarRele();           break;
    case LED_1:         encenderLED(0);          break;
    case LED_2:         encenderLED(1);          break;
    case LED_3:         encenderLED(2);          break;
    case SEQ_LEDS:      secuenciaLEDs();         break;
    case PARPADEO_LEDS: parpadeoLEDs();          break;
    case TONO_800HZ:    sonarTono(800);          break;
    case TONO_1000HZ:   sonarTono(1000);         break;
    case TONO_1200HZ:   sonarTono(1200);         break;
    case TONO_1500HZ:   sonarTono(1500);         break;
    case MELODIA_ZELDA:   reproducirMelodia_Zelda();   break;
    case MELODIA_TWINKLE: reproducirMelodia_Twinkle(); break;
    case MELODIA_TETRIS:  reproducirMelodia_Tetris();  break;
    case MELODIA_MARIO:   reproducirMelodia_Mario();   break;
    default:            break;
  }
}


// Copia la secuencia elegida al arreglo activo y actualiza totalAlarmas.
void copiarSecuencia() {
  if (Eleccion == 0) {
    totalAlarmas = N_SEC1;
    for (int i = 0; i < N_SEC1; i++) elegida[i] = sec1[i];
  } else {
    totalAlarmas = N_SEC2;
    for (int i = 0; i < N_SEC2; i++) elegida[i] = sec2[i];
  }
}


// Sincroniza el RTC con NTP (Colombia UTC-5).
// El ESP32 ya está en WIFI_AP_STA desde setup(), así que solo conecta el STA
// sin tocar el AP — igual que en cualquier proyecto con AP+STA simultáneo.
void sincronizarNTP(String ssid_ext, String pass_ext) {
  resultadoSync = "";
  Serial.println("NTP: conectando a " + ssid_ext);

  WiFi.begin(ssid_ext.c_str(), pass_ext.c_str());

  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 20000UL) {
    delay(500);
  }

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect();
    resultadoSync = "Sin conexion: verifica SSID, clave y que sea 2.4 GHz.";
    Serial.println("NTP: " + resultadoSync);
    return;
  }

  Serial.println("NTP: WiFi OK, obteniendo hora...");
  configTime(-5 * 3600, 0, "pool.ntp.org");  // UTC-5, sin horario de verano

  struct tm timeinfo;
  memset(&timeinfo, 0, sizeof(timeinfo));
  unsigned long esperaInicio = millis();
  while (!getLocalTime(&timeinfo) && millis() - esperaInicio < 10000UL) {
    delay(500);
  }

  if (getLocalTime(&timeinfo) && timeinfo.tm_year > 100) {
    rtc.adjust(DateTime(
      timeinfo.tm_year + 1900,
      timeinfo.tm_mon + 1,
      timeinfo.tm_mday,
      timeinfo.tm_hour,
      timeinfo.tm_min,
      timeinfo.tm_sec
    ));
    char buf[20];
    sprintf(buf, "%02d/%02d/%04d %02d:%02d",
      timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
      timeinfo.tm_hour, timeinfo.tm_min);
    resultadoSync = "Hora sincronizada: " + String(buf);
    Serial.println("NTP: " + resultadoSync);
  } else {
    resultadoSync = "WiFi OK pero NTP no respondio. Reintenta.";
    Serial.println("NTP: " + resultadoSync);
  }

  WiFi.disconnect();
}


void setup() {
  Serial.begin(115200);
  Wire.begin();
  lcd.init();
  lcd.backlight();
  copiarSecuencia();

  if (!rtc.begin()) {
    Serial.println("No se detecta el RTC");
    while (true);
  }

  // Ajusta el RTC solo si la batería falló o el año está claramente mal.
  // Con la sincronización NTP ya no hace falta ajustarlo manualmente.
  DateTime now = rtc.now();
  if (rtc.lostPower() || now.year() < 2024) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_RELE, OUTPUT);
  digitalWrite(PIN_RELE, HIGH);

  for (int i = 0; i < 3; i++) {
    pinMode(pinesLED[i], OUTPUT);
    digitalWrite(pinesLED[i], LOW);
  }

  xTaskCreatePinnedToCore(tareaLCD, "MostrarHora", 2048, NULL, 1, NULL, 1);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP(ssid, password, canal);
  Serial.print("IP AP: ");
  Serial.println(WiFi.softAPIP());

  // =================== PÁGINA PRINCIPAL ===================
  servidor.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {

    // Calcular el próximo evento
    String nombresDia[7] = {"Domingo","Lunes","Martes","Miércoles","Jueves","Viernes","Sábado"};
    DateTime ahora = rtc.now();
    int dA = ahora.dayOfTheWeek();
    int hA = ahora.hour();
    int mA = ahora.minute();
    String proximoEvento = "No hay eventos programados.";
    int dProx = -1, hProx = -1, mProx = -1;
    byte tipoProx = 255;

    for (int off = 0; off < 7; off++) {
      int dC = (dA + off) % 7;
      bool hayEnEsteDia = false;
      int mejorHora = 99, mejorMin = 99;
      byte mejorTipo = 255;

      for (int i = 0; i < totalAlarmas; i++) {
        Alarma a = elegida[i];
        bool diaOk = (a.dia == 1 && dC >= 1 && dC <= 5) || (a.dia == 6 && dC == 6);
        if (!diaOk) continue;
        if (off == 0) {
          if (a.hora < hA) continue;
          if (a.hora == hA && a.minuto <= mA) continue;
        }
        if (!hayEnEsteDia || a.hora < mejorHora || (a.hora == mejorHora && a.minuto < mejorMin)) {
          hayEnEsteDia = true;
          mejorHora = a.hora;
          mejorMin  = a.minuto;
          mejorTipo = a.acciones[0];
        }
      }

      if (hayEnEsteDia) {
        dProx = dC; hProx = mejorHora; mProx = mejorMin; tipoProx = mejorTipo;
        break;
      }
    }

    if (tipoProx != 255) {
      String nombres[] = {"","Relé","LED1","LED2","LED3","Seq.LEDs","Parpadeo","800Hz","1kHz","1.2kHz","1.5kHz","Zelda","Twinkle","Tetris","Mario"};
      String nombreEv = (tipoProx <= 14) ? nombres[tipoProx] : "Evento";
      String HH = (hProx < 10 ? "0" : "") + String(hProx);
      String MM = (mProx < 10 ? "0" : "") + String(mProx);
      proximoEvento = nombresDia[dProx] + " " + HH + ":" + MM + " — " + nombreEv;
    }

    String css = "body{font-family:sans-serif;text-align:center;background:#0f0f0f;color:#39ff14;}"
                 "h2{margin-top:20px;}form{margin:10px;}"
                 "select,button,input{font-size:16px;padding:5px;border-radius:5px;}"
                 "button{background:#39ff14;color:#0f0f0f;border:none;margin-top:10px;cursor:pointer;}"
                 "button:hover{background:#2fdc0a;}"
                 "input{background:#1a1a1a;color:#39ff14;border:1px solid #39ff14;width:200px;margin:4px;}"
                 ".box{border:1px solid #39ff14;display:inline-block;padding:10px;"
                      "margin:15px auto;border-radius:5px;background:#111;}"
                 "hr{border-color:#39ff14;margin:20px auto;width:80%;}";

    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
                  "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
                  "<title>Selector de Eventos</title>"
                  "<style>" + css + "</style></head><body>";

    html += "<h2>Selector de Eventos</h2>"
            "<form action='/set' method='GET'>"
            "<label>Secuencia esta semana: </label>"
            "<select name='ahora'>"
            "<option value='0'" + String(Eleccion == 0 ? " selected" : "") + ">Secuencia 1</option>"
            "<option value='1'" + String(Eleccion == 1 ? " selected" : "") + ">Secuencia 2</option>"
            "</select><br>"
            "<label>Secuencia pr&oacute;xima semana: </label>"
            "<select name='luego'>"
            "<option value='0'" + String(EleccionProximaSemana == 0 ? " selected" : "") + ">Secuencia 1</option>"
            "<option value='1'" + String(EleccionProximaSemana == 1 ? " selected" : "") + ">Secuencia 2</option>"
            "</select><br><br>"
            "<button type='submit'>Aplicar</button></form>"
            "<p>Secuencia activa: " + String(Eleccion == 0 ? "Secuencia 1" : "Secuencia 2") + "</p>"
            "<p>Secuencia pr&oacute;xima: " + String(EleccionProximaSemana == 0 ? "Secuencia 1" : "Secuencia 2") + "</p>"
            "<div class='box'>Pr&oacute;ximo evento:<br>" + proximoEvento + "</div>"
            "<hr>"
            "<h2>Sincronizar Hora (NTP)</h2>"
            + (resultadoSync.length() > 0
                ? "<div class='box'>" + resultadoSync + "</div><br>"
                : "")
            + "<form action='/wifi-sync' method='GET'>"
            "<label>Red WiFi (solo 2.4 GHz):</label><br>"
            "<input type='text' name='ssid' placeholder='Nombre de la red'><br>"
            "<input type='password' name='pass' placeholder='Contrase&ntilde;a'><br><br>"
            "<button type='submit'>Sincronizar</button>"
            "</form>"
            "</body></html>";

    request->send(200, "text/html", html);
  });

  // =================== CAMBIO DE SECUENCIA ===================
  servidor.on("/set", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("ahora")) {
      Eleccion = request->getParam("ahora")->value().toInt();
      copiarSecuencia();
    }
    if (request->hasParam("luego")) {
      EleccionProximaSemana = request->getParam("luego")->value().toInt();
    }
    request->redirect("/");
  });

  // =================== SOLICITUD DE SYNC NTP ===================
  // Solo pone la bandera; el trabajo real ocurre en el loop() para poder usar delay().
  servidor.on("/wifi-sync", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("ssid") && request->hasParam("pass")) {
      ssidNTP = request->getParam("ssid")->value();
      passNTP = request->getParam("pass")->value();
      solicitudSyncNTP = true;
    }
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
                  "<meta http-equiv='refresh' content='35;url=/'></head>"
                  "<body style='background:#0f0f0f;color:#39ff14;font-family:sans-serif;text-align:center;'>"
                  "<h2>Sincronizando hora...</h2>"
                  "<p>Conectando a la red WiFi y obteniendo hora NTP...</p>"
                  "<p style='color:#aaa;font-size:13px;'>Esta p&aacute;gina redirige en ~35 s.</p>"
                  "</body></html>";
    request->send(200, "text/html", html);
  });

  servidor.begin();
}


void loop() {
  DateTime ahora     = rtc.now();
  int diaActual      = ahora.dayOfTheWeek();  // 0=Domingo … 6=Sábado
  int horaActual     = ahora.hour();
  int minutoActual   = ahora.minute();

  // Reset de actuadores al inicio de cada ciclo.
  digitalWrite(PIN_RELE, HIGH);
  for (int i = 0; i < 3; i++) digitalWrite(pinesLED[i], LOW);
  noTone(PIN_BUZZER);

  // Ignorar alarmas durante los primeros 65 s tras el encendido.
  // Evita disparar el evento del minuto actual al arrancar el dispositivo.
  if (millis() < 65000UL) {
    delay(200);
    return;
  }

  // Procesar sincronización NTP si fue solicitada desde la web.
  if (solicitudSyncNTP) {
    solicitudSyncNTP = false;
    sincronizarNTP(ssidNTP, passNTP);
  }

  // Recorrer las alarmas de la secuencia activa.
  for (int i = 0; i < totalAlarmas; i++) {
    Alarma a = elegida[i];

    // dia=1 cubre Lunes(1) a Viernes(5). dia=6 es solo Sábado.
    bool diaCoincide = (a.dia == 1 && diaActual >= 1 && diaActual <= 5) ||
                       (a.dia == 6 && diaActual == 6);

    if (diaCoincide && a.hora == horaActual && a.minuto == minutoActual) {
      // Ejecutar cada acción del evento en orden hasta encontrar SIN_ACCION.
      for (int j = 0; j < 10; j++) {
        if (a.acciones[j] == SIN_ACCION) break;
        ejecutarAccion(a.acciones[j]);
      }
      // Esperar hasta que cambie el minuto para no volver a disparar este mismo evento.
      while (rtc.now().minute() == minutoActual) delay(200);
      break;
    }
  }

  delay(200);

  // Cambio automático de secuencia los lunes a las 00:00.
  if (diaActual == 1 && horaActual == 0 && minutoActual == 0 && Eleccion != EleccionProximaSemana) {
    Eleccion = EleccionProximaSemana;
    copiarSecuencia();
    delay(60000);
  }
}


// TAREA LCD: actualiza el display cada segundo y parpadea el backlight
// (300 ms encendido / 50 ms apagado) para evitar desgaste del panel.
void tareaLCD(void* parametro) {
  unsigned long ultimaActualizacion = 0;
  unsigned long ultimoParpadeo      = 0;
  bool luzEncendida = true;

  for (;;) {
    unsigned long t = millis();

    // Actualizar fecha y hora en pantalla cada segundo.
    if (t - ultimaActualizacion >= 1000) {
      ultimaActualizacion = t;
      DateTime ahora = rtc.now();

      lcd.setCursor(0, 0);
      char fecha[17];
      sprintf(fecha, "%02d/%02d/%04d", ahora.day(), ahora.month(), ahora.year());
      lcd.print(fecha);

      lcd.setCursor(0, 1);
      char hora[17];
      sprintf(hora, "%02d:%02d:%02d", ahora.hour(), ahora.minute(), ahora.second());
      lcd.print(hora);
    }

    // Parpadeo del backlight: 300 ms encendido, 50 ms apagado.
    unsigned long tiempoEnEstado = t - ultimoParpadeo;
    if (luzEncendida && tiempoEnEstado >= 300) {
      lcd.noBacklight();
      luzEncendida = false;
      ultimoParpadeo = t;
    } else if (!luzEncendida && tiempoEnEstado >= 50) {
      lcd.backlight();
      luzEncendida = true;
      ultimoParpadeo = t;
    }

    delay(10);
  }
}
