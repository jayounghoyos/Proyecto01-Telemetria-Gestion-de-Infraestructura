# TELEP/2.0 — especificación implementada

## Encuadre y transportes

ASCII imprimible sin espacios en mensajes TELEP; campos `|`, mediciones `;`,
pares `=`. Cada trama acaba en LF. TCP acepta también CRLF. Máximo **1024 bytes
incluido LF** (CRLF debe caber en el mismo límite). No se permiten campos vacíos,
NUL, campos extra, nombres desconocidos o mediciones duplicadas. UDP transporta
exactamente una trama por datagrama; se rechazan truncamiento, LF interno y CR.

TCP 5000 para consultas, registro y reportes del emisor; UDP 5001 para telemetría
periódica tolerante a pérdida; HTTP 8080 complementa las consultas. TCP entrega
bytes ordenados mientras existe la conexión: no constituye persistencia ni acuse
por la interfaz del operador. Las alertas almacenadas se consultan con GET_ALERTS.

## Identidad y límites

`id`: `[A-Za-z0-9_-]{1,15}`. `session`: 32 caracteres hexadecimales minúsculos.
Un ID pertenece a una sesión durante toda la vida del proceso servidor. HELLO
idéntico es idempotente; otra sesión para ese ID recibe 105. No hay autenticación:
conocer/copiar una sesión permite suplantarla. No usar este diseño como plataforma
pública de producción. No se autorregistran emisores por UDP.

Sesiones limitadas a 65536 intentos, secuencia 0..65535. Un bitmap acotado conserva
las recepciones de toda la sesión, sin una heurística de saltos de 100. Al alcanzar
el límite, el nodo termina y reporta; no rota silenciosamente sus estadísticas.

## Comandos TCP

| Solicitud | Respuesta |
|---|---|
| `HELLO|id|session` | `OK|REGISTERED` |
| `GET_STATUS` | `OK|clave=valor;...` (resumen numérico, ver contadores) |
| `GET_NODES` | `OK|n`, n líneas `id|ACTIVE/INACTIVE|edad_segundos`, `END` |
| `GET_LAST|id` | `OK|id|epoch|mediciones`, o `OK|id|0|NO_DATA` |
| `GET_ALERTS` | `OK|n`, n líneas `epoch|id|tipo|valor`, `END` |
| `STATS` o `STATS|id` | `OK|n`, n líneas `id|session|clave=valor;...`, `END` |
| `REPORT|id|session|attempts|sent|omitted|send_errors|final` | `OK|REPORTED` |
| `SUBSCRIBE` | `OK|SUBSCRIBED`; suscripción idempotente |
| `BYE` | `OK|BYE`, cierre |

REPORT tiene 7 argumentos; sus cuatro contadores son enteros 0..65536,
monótonos y `attempts=sent+omitted+send_errors`. `final` es 0 o 1.
Un reporte final no puede retroceder ni cambiar; sí admite repetición idéntica.
Después de final no se aceptan secuencias >= attempts. Los paquetes atrasados
con secuencia anterior siguen corrigiendo la estimación.

## Telemetría y validación

```text
HELLO|NODE01|0123456789abcdef0123456789abcdef
TELEMETRY|NODE01|0123456789abcdef0123456789abcdef|0|TEMP=24.8;HUM=60;POWER=120;VIB=0.5;STATUS=OK
REPORT|NODE01|0123456789abcdef0123456789abcdef|1|1|0|0|1
```

TELEMETRY tiene cuatro argumentos y al menos tres variables diferentes:

| Variable | Valores válidos | Alerta |
|---|---|---|
| TEMP | decimal finito, -100..200 | >40 |
| HUM | decimal finito, 0..100 | >85 |
| POWER | decimal finito, 0..1000000 | >500 |
| VIB | decimal finito, 0..1000 | >7 |
| STATUS | exactamente OK o FAIL | FAIL |

Se admite notación decimal científica finita dentro del rango; no hexadecimal,
NaN/Inf, desbordamiento o subdesbordamiento. Un datagrama inválido se descarta
sin respuesta UDP y aumenta `udp_invalid`. Una sesión desconocida aumenta
`udp_unknown`. Consultar HTTP/GET_STATUS para observar esos descartes.

## Alertas, actividad y concurrencia

`ALERT|NODE01|TEMP_HIGH|45.00`. Otros tipos: HUM_HIGH, POWER_HIGH, VIB_HIGH,
STATUS_FAIL. Se genera una alerta al entrar en anomalía, no por cada repetición.
El historial retiene 128; la web muestra las últimas 20. No hay persistencia.
La última medición solo se actualiza por una secuencia nueva mayor que la anterior.
Un duplicado o un paquete reordenado no reemplaza la última medición ni genera alerta.
ACTIVE significa telemetría única aceptada en los últimos 15 segundos.

Un único hilo escribe cada conexión; las alertas encoladas se envían entre respuestas
completas, sin intercalar bytes ni líneas dentro de una lista. Cola de 32 alertas por
suscriptor; si se llena, se desconecta. Plazo de envío de un mensaje: un segundo.
Recuperar historial con GET_ALERTS al reconectar. No hay garantía de entrega de push
si se desconecta el consumidor. `slow_disconnected` cuenta desbordamientos de cola.

## Contadores: significado exacto

STATS publica, por ID y sesión:

- `attempts`: último total de intentos informado por el emisor.
- `sent`: sendto completados localmente; **no** acuses de recepción.
- `omitted`: intentos no enviados por DROP.
- `send_errors`: intentos con error local de envío.
- `unique`: secuencias distintas aceptadas durante toda la sesión.
- `duplicates`: repeticiones de secuencias ya vistas.
- `reordered`: nuevas secuencias recibidas por debajo del máximo visto.
- `report_seen`: llegó algún REPORT (el inicial puede tener ceros).
- `final_report`: el emisor declaró terminado el envío.
- `loss_estimated`: max(0, sent_reportado - únicas_recibidas_con_seq<attempts_reportado).

La estimación depende del último reporte TCP y puede bajar si llega un paquete
atrasado, incluso después de final. No es una pérdida definitiva, ni una medida
fiable sin reporte final y un tiempo de drenaje documentado. No se cuentan como
pérdidas de red ni las omisiones ni los errores locales. Un reporte perdido deja
contadores de emisor desactualizados: comparar el JSON final del nodo con STATS.

GET_STATUS y `/status` suman estos contadores sobre las sesiones del proceso actual;
`reports` y `final_reports` indican cuántos nodos reportaron. También contienen:
`boot_id` (identidad del proceso), uptime, registered, active, alerts, tcp_ok, udp_ok,
udp_rx, udp_invalid, udp_unknown y slow_disconnected. `udp_rx` cuenta datagramas
sintácticamente válidos antes de comprobar la sesión; incluye duplicados y sesiones
desconocidas. `udp_invalid` incluye formato inválido y secuencia posterior a final;
por ello no debe sumarse ciegamente con udp_rx como total físico del socket.

## DROP y DNS

`--drop P`, P entre 0 y 100: se omite el intento k cuando
`floor((k+1)*P/100)>floor(k*P/100)`. No es azar ni emulación de un router.
100 intentos con DROP=25 producen 25 omisiones y, sin errores locales, 75 envíos.
Para demostrar pérdida posterior a sendto, usar datagramas de prueba retenidos
por un intermediario de ensayo o la prueba local de cola final pendiente;
no afirmar que DROP produjo pérdida de red.

Cada cinco segundos por defecto (`--dns-refresh`), el nodo abre control TCP
mediante nueva resolución DNS, repite HELLO y REPORT. Si falla, pausa intentos y
reintenta. Si cambia boot_id, registra el cierre del segmento anterior en su log,
genera nueva sesión y reinicia sus contadores. El estado no es comparable entre
boots; no hay recuperación de estadísticas anteriores. El DNS usa la caché del SO.

## Errores

100 BAD_FORMAT; 101 UNKNOWN_CMD; 102 UNKNOWN_NODE/sesión desconocida;
103 TOO_LONG; 104 SERVER_FULL; 105 ID_IN_USE. Formato: `ERR|código|nombre`.
Una trama completa inválida no termina el servidor ni la conexión TCP. Una trama
incompleta excediendo tres segundos termina esa conexión. Recursos agotados pueden
causar cierre inmediato. El cliente debe tolerarlo.

## HTTP

GET `/`: HTML real; `/status`: resumen JSON numérico; `/nodes`: ID, sesión,
actividad, mediciones y objeto statistics; `/alerts`: historial JSON.
404 ruta desconocida, 405 método distinto de GET, 400 petición inválida,
408 lectura incompleta, 431 límites de cabecera. `/status` devuelve 503 si TCP o
UDP no actualizan su latido durante tres segundos. Latidos miden progreso de los
bucles locales, no disponibilidad desde Internet. Sin nodos, active=0 es normal.

Máximo 16 trabajadores, 2 segundos totales para cabeceras, 64 cabeceras,
8192 bytes agregados y línea de menos de 1024 bytes. Con capacidad agotada se cierra
la nueva conexión. Límite de respuesta 65536 bytes, suficiente para los límites
fijos de nodos/variables/alertas del servidor.
