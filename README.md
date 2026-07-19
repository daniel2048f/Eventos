# Eventos — Programador de eventos por horario con ESP32

Controlador ESP32 que dispara relé, LEDs y zumbador según dos secuencias semanales de horarios configurables, con hora mantenida por un RTC DS3231 y una interfaz web propia para elegir la secuencia activa y sincronizar la hora por NTP.

## Tabla de contenido

- [Componentes de hardware](#componentes-de-hardware)
- [Conexiones físicas](#conexiones-físicas)
  - [Bus I2C (RTC + LCD)](#bus-i2c-rtc--lcd)
  - [Actuadores (GPIO directo)](#actuadores-gpio-directo)
  - [Red WiFi](#red-wifi)
- [Instalación / build](#instalación--build)
- [Configuración inicial](#configuración-inicial)
- [Interfaz web (rutas HTTP)](#interfaz-web-rutas-http)
- [Indicadores de estado](#indicadores-de-estado)
- [Comportamiento ante fallas y reinicios](#comportamiento-ante-fallas-y-reinicios)
- [Tareas automáticas](#tareas-automáticas)
- [Solución de problemas](#solución-de-problemas)

## Componentes de hardware

| Componente        | Detalle                                              |
|-------------------|-------------------------------------------------------|
| Microcontrolador  | ESP32 (board `esp32dev` en PlatformIO)                |
| RTC               | DS3231, bus I2C, dirección fija `0x68`                |
| Pantalla          | LCD 20×4 con backpack I2C, dirección `0x27`            |
| Relé              | 1 canal, pin GPIO 33                                   |
| LEDs              | 3 unidades, pines GPIO 25, 26, 27                      |
| Zumbador          | Piezoeléctrico pasivo, pin GPIO 32, manejado con `tone()`/`noTone()` |

> Nota sobre una versión anterior de esta documentación: describía la pantalla como "LCD 16×2" y el relé como "activo en bajo". Ambos datos no coinciden con el código actual: la pantalla se inicializa como 20 columnas × 4 filas (`LiquidCrystal_I2C lcd(0x27, 20, 4)`), y la tarea del LCD escribe efectivamente en las 4 filas. Sobre el relé: el patrón de señales del firmware deja el pin en `LOW` en reposo y lo pone en `HIGH` durante los pulsos de activación — esto corresponde a un módulo de relé **activo en alto**, no en bajo; si el relé físico fuera activo en bajo, quedaría energizado casi todo el tiempo (justo lo contrario de lo que busca el diseño). Se corrige aquí bajo esa evidencia; si el módulo instalado es distinto, ajústelo en el hardware o avise para adaptar el firmware.

El equipo no usa tarjeta SD ni sistema de archivos: no hay dependencias de almacenamiento externo más allá de la memoria flash interna del ESP32.

## Conexiones físicas

### Bus I2C (RTC + LCD)

| Señal | Pin ESP32 |
|-------|-----------|
| SDA   | GPIO 21   |
| SCL   | GPIO 22   |

El RTC (`0x68`) y el LCD (`0x27`) comparten este mismo bus. El acceso está serializado por software (un mutex interno) porque tres tareas distintas del firmware lo usan de forma concurrente: el ciclo principal, la tarea que refresca la pantalla y el servidor web — ver [Comportamiento ante fallas y reinicios](#comportamiento-ante-fallas-y-reinicios). Este bus es completamente independiente de los pines de los actuadores (relé/LEDs/zumbador) y de la radio WiFi: una falla en el I2C no afecta el disparo de eventos por horario, solo la lectura de hora y la pantalla.

### Actuadores (GPIO directo)

| Actuador   | Pin(es)        | Notas                                              |
|------------|-----------------|-----------------------------------------------------|
| Relé       | 33              | Activo en alto (ver nota más arriba)                |
| LED 1      | 25              | Activo en alto                                       |
| LED 2      | 26              | Activo en alto                                       |
| LED 3      | 27              | Activo en alto                                       |
| Zumbador   | 32              | Se maneja con `tone()`/`noTone()`, no `digitalWrite` |

Estos pines no comparten bus ni protocolo con el I2C ni con el WiFi: son líneas GPIO independientes, así que un problema en el RTC/LCD nunca bloquea al relé, los LEDs o el zumbador (salvo el caso general de watchdog, ver más abajo).

### Red WiFi

El ESP32 opera siempre en modo combinado **AP + STA** (`WIFI_AP_STA`):

| Parámetro       | Valor                          |
|-----------------|----------------------------------|
| Modo            | Punto de acceso (AP) permanente + cliente (STA) bajo demanda |
| SSID (AP)       | `Alarma`                        |
| Contraseña (AP) | `87654321`                      |
| IP del AP       | `192.168.4.1`                   |
| Canal WiFi      | `1` (constante `canal` en el código) |

> Corrección respecto a documentación anterior: se había documentado el SSID como `AlarmaESP32` y la contraseña como `123456789`. El código actual define `ssid = "Alarma"` y `password = "87654321"`. Use estos valores para conectarse.

El rol STA solo se activa transitoriamente cuando se solicita una sincronización NTP (manual desde la web, automática al arrancar si hay credenciales guardadas, o la resincronización diaria a las 03:00 — ver [Tareas automáticas](#tareas-automáticas)); el punto de acceso permanece disponible en todo momento, según el propio comentario del código: "El ESP32 ya está en WIFI_AP_STA desde setup(), así que solo conecta el STA sin tocar el AP".

## Instalación / build

Requisitos: [PlatformIO](https://platformio.org/) (CLI o extensión de VS Code/IDE compatible).

Dependencias declaradas en `platformio.ini` (`lib_deps`), verificadas contra el manifiesto y contra la última compilación exitosa:

| Librería                          | Rango declarado | Versión resuelta | ¿Usada en el código actual? |
|------------------------------------|------------------|-------------------|-------------------------------|
| `adafruit/Adafruit PN532`          | `^1.2.4`         | 1.3.4             | No — sin ningún `#include` en el proyecto |
| `marcoschwartz/LiquidCrystal_I2C`  | `^1.1.4`         | 1.1.4             | Sí — control del LCD          |
| `mobizt/ESP Mail Client`           | `^3.4.19`        | 3.4.24            | No — sin ningún `#include` en el proyecto |
| `adafruit/RTClib`                  | `^2.1.4`         | 2.1.4             | Sí — control del RTC DS3231   |
| `me-no-dev/AsyncTCP`               | `^1.1.1`         | 1.1.1             | Sí — base del servidor web     |
| `me-no-dev/ESPAsyncWebServer`      | `^1.2.3`         | 1.2.4             | Sí — interfaz web              |
| `Preferences`, `WiFi`, `Wire`      | (incluidas en el framework arduino-esp32, no son dependencias externas) | 2.0.0 | Sí |

`Adafruit PN532` (lector NFC/RFID) y `ESP Mail Client` (envío de correo) están declaradas pero ningún archivo del proyecto las incluye ni las usa; no forman parte de ninguna función actual del equipo.

Flag de build relevante en `platformio.ini`:

```
board_build.partitions = min_spiffs.csv
```

Esta tabla de particiones reduce al mínimo la partición SPIFFS (que el proyecto no usa) para maximizar el espacio disponible para el firmware. Con la tabla `min_spiffs.csv` cada partición de aplicación (`app0`/`app1`) tiene 0x1E0000 bytes (~1.9 MB), frente a un firmware compilado que hoy ocupa unos 850 KB (~43 %). Si se quita este flag, PlatformIO usa la tabla de particiones por defecto del framework, con menos espacio de aplicación y una partición SPIFFS más grande que nunca se usaría — no es obligatorio, pero da menos margen para agregar código.

Pasos:

```bash
# Compilar
pio run

# Compilar y subir al ESP32 conectado por USB
pio run --target upload

# Ver el log por serial (115200 baudios)
pio device monitor

# Subir y luego abrir el monitor en un solo paso
pio run --target upload && pio device monitor

# Compilación limpia
pio run --target clean
```

## Configuración inicial

1. Compilar y subir el firmware (ver arriba).
2. Conectarse por WiFi a la red `Alarma` con la contraseña `87654321`.
3. Abrir `http://192.168.4.1/` en el navegador. La página muestra la secuencia activa, la secuencia programada para la próxima semana y el próximo evento calculado.
4. Elegir la secuencia para "esta semana" y para "la próxima semana" en los selectores y pulsar "Aplicar". Este cambio se guarda solo en memoria RAM (no persiste si el equipo se reinicia — ver [Comportamiento ante fallas y reinicios](#comportamiento-ante-fallas-y-reinicios)).
5. Para sincronizar la hora: en la sección "Sincronizar Hora (NTP)", ingresar el SSID y contraseña de una red WiFi **de 2.4 GHz** con salida a internet, y pulsar "Sincronizar". La página redirige automáticamente a los ~35 s. Si tiene éxito, las credenciales quedan guardadas para reconexión automática en próximos arranques y para la resincronización diaria (ver más abajo).

Los horarios y acciones de cada evento (`sec1[]` y `sec2[]` en el código) **no son editables desde la web**: la interfaz solo permite elegir cuál de las dos secuencias predefinidas está activa. Para cambiar horarios o acciones hay que modificar el código fuente y volver a subir el firmware — ver `CODIGO.md`.

## Interfaz web (rutas HTTP)

Rutas registradas en el servidor (verificadas contra el código; no hay ninguna otra ruta ni manejador de archivos estáticos):

| Método | Ruta          | Parámetros                        | Descripción |
|--------|---------------|-------------------------------------|--------------|
| GET    | `/`           | —                                    | Página principal: selector de secuencias, próximo evento y formulario de sincronización NTP. |
| GET    | `/set`        | `ahora` (0/1), `luego` (0/1)         | Cambia la secuencia activa (`ahora`) y/o la programada para la próxima semana (`luego`). Redirige a `/`. |
| GET    | `/wifi-sync`  | `ssid`, `pass`                       | Solicita una sincronización NTP con la red indicada. El trabajo real ocurre en el ciclo principal (no en el propio request); la página responde de inmediato con un mensaje de espera que se autorredirige a `/` en ~35 s. |

Cualquier otra ruta recibe la respuesta 404 por defecto del servidor (no hay un manejador `onNotFound` personalizado).

## Indicadores de estado

El sistema tiene tres canales de retroalimentación independientes, que no dependen entre sí para funcionar (uno puede fallar sin afectar a los otros):

**Pantalla LCD** (4 filas, se actualiza una vez por segundo):

| Fila | Contenido |
|------|-----------|
| 1    | Fecha actual `DD/MM/AAAA` |
| 2    | Hora actual `HH:MM:SS` |
| 3    | Día y hora del próximo evento (`Prox: <día> HH:MM`) o `Sin proximos eventos` |
| 4    | Nombre de la acción del próximo evento |

El backlight de la pantalla parpadea continuamente en un ciclo de 80 ms encendido / 10 ms apagado (pensado para reducir el desgaste del panel). Además, el controlador del LCD se reinicializa automáticamente cada 30 s como autocorrección ante caracteres corruptos — ver [Comportamiento ante fallas y reinicios](#comportamiento-ante-fallas-y-reinicios).

**Monitor serie** (115200 baudios): registra cada acción disparada, cada intento de sincronización NTP, el estado del RTC al arrancar y los reintentos de recuperación del bus I2C. Es el canal más detallado para diagnóstico, pero requiere el cable USB conectado.

**Actuadores físicos** (relé, LEDs, zumbador): son la salida "de producción" del sistema — ver la tabla de duraciones en `CODIGO.md` para el tiempo exacto de cada acción.

## Comportamiento ante fallas y reinicios

| Situación                                                   | Comportamiento |
|----------------------------------------------------------------|-----------------|
| El RTC no responde al arrancar                                  | Hasta 5 reintentos, recuperando el bus I2C entre cada uno. Si sigue sin responder, el equipo se reinicia solo (ya no queda congelado indefinidamente). |
| El RTC perdió la hora (batería de respaldo agotada o ausente)   | Se restaura desde el último respaldo guardado en memoria flash (NVS) si es más reciente que la fecha de compilación del firmware; si no, se usa la fecha de compilación como último recurso. Se muestra un aviso en el LCD durante 4 s al arrancar. **Esto no reemplaza una pila de RTC funcional**: sin ella, el equipo no puede seguir contando el tiempo mientras está desenergizado. |
| Una lectura del RTC llega corrupta durante la operación normal | Se descarta esa lectura (no se actualiza la pantalla ni se evalúan eventos en ese ciclo); se reintenta automáticamente en el siguiente ciclo. |
| El LCD muestra caracteres corruptos ("?", símbolos extraños)    | Se autocorrige solo: el controlador del LCD se reinicializa cada 30 s como máximo. |
| Cualquier tarea queda bloqueada más de 20 s                     | Un watchdog por hardware reinicia el equipo automáticamente. |
| Credenciales WiFi usadas en una sincronización NTP exitosa      | Persisten en memoria flash (NVS) y se reutilizan automáticamente en el siguiente arranque y en la resincronización diaria. |
| Hora del RTC                                                    | Persiste en el propio chip DS3231 (con su pila de respaldo) mientras el equipo está apagado; además se respalda una copia en NVS cada 15 minutos y tras cada sincronización NTP exitosa, para minimizar la pérdida si la pila del RTC falla. |
| Secuencia activa / secuencia de la próxima semana (`Eleccion` / `EleccionProximaSemana`) | **No persisten**: son variables solo en RAM. Un reinicio del equipo (corte de luz, reset, actualización de firmware) siempre vuelve a "Secuencia 1" en ambos selectores, sin importar lo que se haya elegido antes desde la web. |

## Tareas automáticas

| Tarea | Condición de disparo | Condición del efecto (qué pasa realmente) |
|-------|------------------------|-----------------------------------------------|
| Disparo de eventos programados | Cada vez que cambia el minuto leído del RTC, para cada entrada de la secuencia activa cuyo día/hora/minuto coincida exactamente | Solo se ejecuta la **primera** entrada del arreglo que coincida (el código corta la búsqueda con `break` apenas encuentra una); si hay dos entradas con el mismo día/hora/minuto, la segunda nunca se ejecuta. Esto ocurre hoy en `sec2[]`: la entrada `{6, 18, 0, {RELE}}` está duplicada — la segunda copia es datos muertos que nunca dispara nada. |
| Respaldo de hora en NVS | Cada 15 minutos (por tiempo transcurrido, sin ninguna otra condición) | Siempre escribe la hora actual del RTC en la memoria flash, sin excepciones. |
| Resincronización NTP diaria | A las 03:00 en punto, una sola vez por día | Solo intenta sincronizar si hay credenciales WiFi guardadas de una sincronización previa; si no las hay, el sistema igual marca el día como "ya intentado" y no vuelve a evaluarlo hasta el día siguiente, aunque más tarde ese mismo día se guarden credenciales nuevas por sincronización manual. |
| Cambio automático de secuencia semanal | Cada lunes a las 00:00, solo si la secuencia "activa" es distinta de la programada para "la próxima semana" | Copia la secuencia programada como la nueva activa y pausa el ciclo principal 60 s (alimentando el watchdog durante la espera) para no reevaluar la condición varias veces dentro del mismo minuto. |

No hay eventos programados los domingos: ninguna entrada de `sec1[]`/`sec2[]` puede coincidir un domingo, ya que el campo `dia` solo admite `1` (lunes a viernes) o `6` (sábado).

## Solución de problemas

### RTC (hora incorrecta o inestable)

- **Síntoma:** el equipo siempre muestra la misma hora fija (ej. la hora de compilación del firmware) después de cada reinicio.
  **Causa más probable:** la pila de respaldo (CR2032) del módulo DS3231 está agotada o no está bien conectada — el RTC pierde la hora cada vez que se corta la alimentación principal.
  **Solución:** revisar/reemplazar la pila del módulo RTC. El firmware avisa esta condición en el LCD al arrancar ("AVISO: batería RTC agotada o ausente") y respalda la hora en flash cada 15 minutos para minimizar el salto hacia atrás mientras se resuelve el hardware.
- **Síntoma:** eventos que se disparan a una hora equivocada de forma esporádica (no siempre).
  **Causa más probable:** una lectura I2C puntualmente corrupta.
  **Solución:** no requiere acción — el firmware descarta lecturas con fecha/hora fuera de rango y reintenta en el siguiente ciclo (~200 ms después), sin disparar eventos con datos inválidos.

### LCD (pantalla en blanco, caracteres extraños o congelada)

- **Síntoma:** aparecen signos de interrogación ("?") u otros caracteres sin sentido en la pantalla.
  **Causa más probable:** ruido eléctrico en el bus I2C o una transacción interrumpida.
  **Solución:** se autocorrige solo en un máximo de 30 s (reinicialización periódica del controlador del LCD). Si persiste más de ese tiempo, revisar el cableado SDA/SCL (GPIO 21/22) y la alimentación del módulo.
- **Síntoma:** la pantalla queda congelada y el equipo deja de responder (web, eventos) hasta apagarlo y prenderlo manualmente.
  **Causa:** este era el comportamiento antes de las mejoras de robustez del bus I2C. Con el firmware actual, el acceso al bus está serializado con un mutex entre las tres tareas que lo usan (ciclo principal, tarea de pantalla y servidor web) y hay un watchdog de 20 s que reinicia el equipo solo si algo llega a bloquearse de verdad — ya no debería requerir intervención manual.

### WiFi / interfaz web

- **Síntoma:** no aparece la red `Alarma` para conectarse.
  **Causa probable:** el equipo no terminó de arrancar (posible reinicio por watchdog o por fallo repetido del RTC) o hay interferencia en el canal WiFi 1.
  **Solución:** esperar el ciclo de arranque completo; si el problema persiste, cambiar la constante `canal` en el código (ver `CODIGO.md`).
- **Síntoma:** la sincronización NTP falla ("Sin conexion" o "WiFi OK pero NTP no respondio").
  **Causa probable:** la red indicada no es de 2.4 GHz, la contraseña es incorrecta, o no tiene salida a internet / acceso al servidor `pool.ntp.org`.
  **Solución:** verificar que la red sea 2.4 GHz y tenga internet; reintentar desde el formulario web.

### Relé / LEDs / zumbador

- **Síntoma:** un evento no se disparó a la hora esperada.
  **Causa probable 1:** hay otra entrada con exactamente el mismo día/hora/minuto antes en el arreglo de la secuencia — solo la primera se ejecuta (ver [Tareas automáticas](#tareas-automáticas)).
  **Causa probable 2:** el equipo estaba ejecutando las acciones de un evento anterior tan largo que el ciclo principal no llegó a leer el minuto del evento siguiente. Con los datos de fábrica esto no ocurre (el combo más largo actual dura unos 32 s, muy por debajo del margen disponible), pero es una restricción a tener en cuenta al agregar más acciones a una misma entrada — ver `CODIGO.md`.
  **Solución:** revisar `sec1[]`/`sec2[]` en el código en busca de entradas duplicadas o combinaciones de acciones demasiado largas.
- **Síntoma:** el relé queda energizado la mayor parte del tiempo en vez de solo pulsar durante el evento.
  **Causa probable:** el módulo de relé físico es activo en bajo, pero el firmware está diseñado para uno activo en alto (ver nota en [Componentes de hardware](#componentes-de-hardware)).
  **Solución:** confirmar la polaridad del módulo instalado; si es activo en bajo, hay que invertir la lógica en el firmware (`digitalWrite` de `activarRele()` y el reset por ciclo en `loop()`).
