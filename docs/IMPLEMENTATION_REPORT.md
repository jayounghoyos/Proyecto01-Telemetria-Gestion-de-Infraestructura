# Informe de implementación local

## Estado inicial verificado

La carpeta de trabajo no contenía una copia Git del proyecto. Se clonó el remoto
indicado en un subdirectorio nuevo, conservando output/ y tmp/ existentes.
Rama main, árbol limpio, HEAD 994cdb8c52e6c620b701c07887ee8aa995c6be74.
El remoto seguía en el commit de la auditoría al clonar; no se asumió de antemano.
Sin AGENTS.md. No se hizo push, commit, despliegue, reinicio remoto ni cambio DNS.

Antes de editar se confirmó y comunicó que seguían vigentes: HTTP bloqueante,
ONLINE fijo, escritores concurrentes TCP, difusión bloqueante desde UDP,
contadores sin sesión, ausencia de STATS/DROP y validación/recuperación incompletas.

## A. Cambios por archivo

| Archivos | Cambio |
|---|---|
| server/http_status_server.c | Trabajadores limitados, plazo absoluto, límites de cabeceras, estados de error, JSON de estadísticas y datos reales |
| server/service_health.c, .h | Latidos de bucles TCP/UDP y detección de falta de progreso |
| server/socket_helpers.c, .h | Plazos de lectura/envío; encuadre completo incluido LF; rechazo de controles y exceso de longitud |
| server/alert_subscribers.c, .h | Colas acotadas, un escritor propietario, desconexión por saturación sin enviar desde UDP |
| server/tcp_command_server.c | Límites de clientes, validación de argumentos, STATS/REPORT, idempotencia HELLO/SUBSCRIBE |
| server/node_registry.c, .h | Sesiones, boot_id, bitmaps, duplicados/reordenamiento, reportes coherentes y estimación revisable |
| server/udp_telemetry_server.c | Rechazo de truncamiento, formato, sesión y secuencia inválidos; latido |
| server/telep_protocol.c, .h | Gramática estricta, números/rangos/variables/estado y error ID_IN_USE |
| server/response_format.c, .h | Resumen y estadísticas con la misma semántica para TCP/HTTP |
| server/config.h, main.c | Límites TELEP/2 y comprobación de creación de hilos |
| client/node.py | Sesión, DROP determinista, contadores separados, reporte final, DNS periódico y cambio de boot |
| client/telep.py | Encuadre con búfer propio, lectura acotada, listas STATS y alertas completas |
| client/operator_cli.py | Opción STATS y manejo de fallos de suscripción/conexión |
| client/operator_gui.py | Reintentos, preservación de ventana y manejo de error en cada consulta |
| Dockerfile | Misma distribución build/runtime, Werror y usuario sin privilegios; build Docker pendiente |
| .gitattributes, .gitignore | Finales de línea y exclusión de capturas/evidencia personal sin revisar |
| tests/test_system.py, run.py | Suite funcional con JSON real y hashes de fuentes |
| tests/end_to_end.py | Cinco procesos de nodo y dos operadores persistentes >=120s |
| tests/results/ | Resultados locales reales, logs y alcance explícito |
| README.md, docs/PROTOCOL.md, ARCHITECTURE.md, DEPLOY.md | Comportamiento final, incompatibilidad v1/v2 y límites |
| docs/EVIDENCE_GUIDE.md, REPORT_TEMPLATE.md | Comandos reales y plantillas pendientes de evidencia humana |
| scripts/capture_traffic.sh | Captura real solo al invocarlo; no se ejecutó ni generó pcap |

## B. Resultados exactos

Compilación con GCC 13.3.0 Ubuntu, GNU11, Wall/Wextra/Werror/O2/pthread: exit 0.
Suite final: 14 pruebas, éxito=True, 12.966 segundos según unittest.
Entorno: Linux-6.6.87.2-microsoft-standard-WSL2-x86_64-with-glibc2.39. Fecha UTC: 2026-09-20T02:08:06.801241+00:00.

| Prueba | Resultado | Segundos |
|---|---|---:|
| test_dns_resolved_on_every_connection | PASS | 0.029 |
| test_gui_mid_refresh_failures | PASS | 0.048 |
| test_gui_reconnect_controller | PASS | 0.027 |
| test_http_absolute_deadline | PASS | 2.026 |
| test_http_routes_and_slow_headers | PASS | 2.031 |
| test_invalid_client_arguments | PASS | 0.376 |
| test_node_known_drop | PASS | 0.303 |
| test_node_recovers_after_server_restart | PASS | 4.626 |
| test_node_without_drop | PASS | 0.316 |
| test_slow_subscriber_queue_does_not_block_udp | PASS | 0.458 |
| test_stats_duplicates_reordering_and_late_tail | PASS | 0.03 |
| test_tcp_validation_and_recovery | PASS | 0.028 |
| test_two_operators_alerts_while_udp_continues | PASS | 2.64 |
| test_udp_validation | PASS | 0.027 |

Integral final: 120.092407278 segundos con 5 nodos y 2 operadores.
602 intentos/envíos/recepciones únicas por nodo; total 3010. Omisiones=0,
errores de envío=0, duplicados=0, reordenamientos=0, pérdida estimada=0.
241 muestras por operador; 5 alertas recibidas por cada uno. Final REPORT confirmado
en los 5 nodos. Los hashes guardados coinciden con las fuentes actuales de servidor,
clientes y pruebas. Hubo otra ejecución integral anterior aprobada, conservada con
su propia fecha y hashes; no sustituirla ni presentarla como nube.

Prueba DROP: 20 intentos, DROP=25, 15 envíos/únicos, 5 omisiones, 0 errores, pérdida
estimada 0. Sin DROP: 20 intentos, 20 envíos/únicos, 0 omisiones.
Prueba sintética de reordenamiento: 0,2,1,3,2 produce 4 únicos, 1 duplicado,
1 reordenamiento; al reportar 5 envíos, estimación 1; al llegar 4 baja a 0.
Estos son resultados de laboratorio controlado, no pérdida espontánea de Internet.

`docker compose config --quiet`: exit 0. Motor Docker no disponible; no build/run
verificado. `bash -n scripts/capture_traffic.sh`: exit 0, sin ejecutar captura.
`git diff --check`: exit 0. Sin pruebas remotas de esta versión.

## C. Cobertura del PDF

Cubierto localmente con pruebas: servidor C y clientes Python; sockets TCP/UDP;
protocolo textual; variables/periocidad; 5 nodos y 2 operadores concurrentes;
consultas, alertas, cuatro rutas HTTP; errores/recuperación; contadores UDP.
DNS: llamadas reales de resolución local y renovación probadas; cambio de IP pública
pendiente. GUI: controlador probado sin pantalla; validación visual pendiente.
Docker: configuración validada, construcción/ejecución pendiente.
Nube/Internet/DNS público de nueva versión: no verificable en esta ejecución.
Wireshark, informe final, capturas individuales y video: preparados como guía y
plantilla, todavía pendientes. Diagramas existentes identificados como históricos.

## D. Comandos

Ver EVIDENCE_GUIDE.md y DEPLOY.md; comandos de suite:

```bash
make -C server CFLAGS="-std=gnu11 -Wall -Wextra -Werror -O2 -pthread"
python3 tests/run.py
python3 tests/end_to_end.py --start-server --duration 120
```

## E. Bloqueos y límites

Motor Docker detenido; responsable debe habilitarlo y construir/probar la imagen.
Despliegue de TELEP/2 requiere actualizar servidor y clientes juntos; fuera del alcance
autorizado. Evidencia nube/puertos/DNS público e individual debe obtenerse desde
los equipos y cuenta reales. Sin autenticación ni persistencia; IDs/sesiones y
capacidad acotados, explicados en PROTOCOL.md. GUI aún usa red síncrona con timeout.

## F. Puerta de video

Las pruebas funcionales locales de los puntos 1–4 están aprobadas. No se produjo
ni se inició video definitivo. Antes de la grabación completa aún se deben validar
Docker, despliegue externo, GUI visual y reunir capturas reales y evidencia individual.
