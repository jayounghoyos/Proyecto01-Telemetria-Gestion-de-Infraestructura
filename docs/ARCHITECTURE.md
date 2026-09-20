# Arquitectura implementada — TELEP/2.0

Python node.py → UDP 5001 → receptor C → registro protegido por mutex.
Python CLI/GUI → TCP 5000 → un hilo por cliente → snapshots del registro.
Navegador → HTTP 8080 → hasta 16 trabajadores → snapshot/JSON/HTML.
Los diagramas en diagrams/ son históricos de TELEP/1; deben actualizarse antes
del informe final para incluir colas, sesiones y REPORT. No acreditan ejecución.

## Hilos y propiedad

- El hilo UDP valida, registra y encola alertas. Nunca envía por TCP.
- El aceptador TCP mantiene su latido y limita a 64 conexiones.
- Cada conexión tiene un único hilo lector/escritor. Termina toda la respuesta
  antes de vaciar alertas. Cada mensaje incluye LF antes de entrar al envío.
- Las colas tienen 32 mensajes y hasta 32 suscriptores. Desbordamiento implica
  shutdown, no close desde el productor; el propietario retira la suscripción y
  cierra, evitando reutilización insegura del descriptor.
- Envíos tienen plazo absoluto de un segundo. Una entrada TCP parcial tiene
  tres segundos; una conexión ociosa puede seguir suscrita.
- HTTP tiene trabajadores separados, límites de cabeceras y plazo absoluto.
  Un cliente lento no monopoliza el aceptador. Respuestas se generan fuera del mutex.

## Estado y medición

64 nodos, 128 alertas en anillo, bitmap de 65536 bits por nodo (512 KiB total)
fuera de los snapshots. Memoria acotada; no hay almacenamiento persistente.
Un ID está ligado a su sesión hasta terminar el proceso. Duplicados no actualizan
mediciones; reordenamientos corrigen recepción/pérdida sin retroceder datos.
REPORT lleva contadores del emisor. STATS y HTTP muestran exactamente su alcance.
Los latidos de los bucles TCP/UDP sustituyen el rótulo fijo ONLINE. No garantizan
conectividad pública, salud del DNS ni persistencia.

## Recuperación

Nodo: re-resuelve DNS y renueva el plano de control periódicamente, pausa al fallar,
y abre otra sesión si cambia boot_id. CLI sale de forma controlada ante desconexión;
GUI conserva ventana y reintenta cada tres segundos. La GUI todavía usa llamadas
síncronas con timeout: el renderizado visual/latencia se valida manualmente.

## Elección de transportes

UDP evita una conexión por flujo periódico; una lectura más reciente reemplaza a la
anterior. TCP proporciona flujo ordenado para comandos y reportes. La aplicación
sigue necesitando encuadre, límites, sesiones y detección de desconexión. El push
TCP no reemplaza el historial ni garantiza que un usuario haya visto la alerta.

## Límites y seguridad

Sin autenticación/TLS, rate limiting por identidad ni almacenamiento duradero.
La política de ID evita colisiones accidentales, no atacantes que conozcan la sesión.
Las reglas de nube y evidencia de acceso externo se verifican por separado;
los tests locales no las sustituyen.
