# Arquitectura implementada — TELEP/2.0

Python node.py → UDP 5001 → receptor C → registro protegido por mutex.
Python CLI/GUI → TCP 5000 → un hilo por cliente → snapshots del registro.
Navegador → HTTP 8080 → hasta 16 trabajadores → snapshot/JSON/HTML.

![Figura 1. Arquitectura de la plataforma](diagrams/01-architecture.png)

**Figura 1. Arquitectura de la plataforma.** Los nodos simulados y los operadores
se ejecutan en los equipos del equipo y localizan al servidor por su nombre DNS,
nunca por IP. La telemetría viaja por UDP 5001 porque perder una medición es
tolerable; el registro, las consultas y las alertas van por TCP 5000 porque deben
llegar completas. El servidor en C corre dentro de un contenedor Docker en una
instancia EC2 y publica además una página de estado en HTTP 8080.

## Hilos y propiedad

![Figura 2. Modelo de concurrencia del servidor](diagrams/02-server-threads.png)

**Figura 2. Modelo de concurrencia.** `main()` abre los tres sockets de escucha y
crea un hilo por servicio. Cada conexión TCP aceptada obtiene su propio hilo, y
HTTP dispone de un grupo acotado de trabajadores. Todo el estado compartido vive
en `node_registry.c` detrás de un solo mutex; los lectores copian una instantánea
bajo el candado y formatean la respuesta fuera de él, de modo que un operador
lento nunca retrasa la ingesta de telemetría.

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
Los latidos de los bucles TCP/UDP sustituyen el rótulo fijo ONLINE: indican que
cada bucle sigue progresando, no que el servicio sea alcanzable desde Internet.

## Flujo de una alerta

![Figura 3. Flujo de una alerta](diagrams/03-alert-flow.png)

**Figura 3. De un datagrama UDP a la consola del operador.** Un operador que envió
`SUBSCRIBE` queda registrado en `alert_subscribers`. Cuando llega una medición
fuera de umbral, el hilo UDP la registra bajo el mutex y **encola** la alerta; el
envío por TCP lo hace el hilo propietario de esa conexión. Ese paso intermedio es
lo que impide que un suscriptor lento bloquee la recepción de telemetría. La
alerta se emite una sola vez mientras el valor sigue siendo anómalo.

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

## Despliegue

![Figura 4. Red y despliegue en AWS](diagrams/04-network-deployment.png)

**Figura 4. Red y despliegue.** El grupo de seguridad abre 22/tcp para
administración y los tres puertos del servicio; la regla **5001/udp** es la que
suele olvidarse, y sin ella el contenedor arranca, la web responde y no entra un
solo datagrama. Como el Learner Lab asigna una IP pública nueva en cada arranque,
`duckdns.service` reapunta el registro A al iniciar la instancia, de modo que los
clientes siguen usando el mismo nombre sin cambiar nada.

## Límites y seguridad

Sin autenticación/TLS, rate limiting por identidad ni almacenamiento duradero.
La política de ID evita colisiones accidentales, no atacantes que conozcan la sesión.
Las reglas de nube y evidencia de acceso externo se verifican por separado;
los tests locales no las sustituyen.
