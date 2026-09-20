# Plantilla de informe técnico — PENDIENTE DE COMPLETAR

Los campos [PENDIENTE] requieren datos originales del equipo. No son resultados.

## 1. Portada e integrantes

Título, asignatura, fecha, docente, integrantes, usuario GitHub, roles: [PENDIENTE].
Repositorio/visibilidad autorizada, commit final y fecha límite: [PENDIENTE].

## 2. Introducción y problema

Contexto, alcance, objetivo, variables y criterios de éxito: [PENDIENTE].

## 3. Arquitectura

Figura 1: diagrama actualizado de componentes y transportes [PENDIENTE].
Figura 2: hilos, colas y mutex [PENDIENTE]. Explicar ambas figuras.

## 4. Protocolo

Referenciar PROTOCOL.md; incluir intercambio real con ID/sesión/seq,
encuadre, errores, política de IDs y semántica de contadores [PENDIENTE].

## 5. Sockets TCP/UDP

Tabla de archivos/líneas en la versión final y muestras de ejecución [PENDIENTE].

## 6. Concurrencia y robustez

Dos operadores + cinco nodos >=120s, cliente lento, inválidos y recuperación.
Referencias a resultados locales reales y límites de su extrapolación [PENDIENTE].

## 7. Nube, DNS y Docker

Instancia/región, SO, commit desplegado, imagen/digest, puertos/reglas, resolución
DNS y cliente externo [PENDIENTE]. Evidencia Docker en nube separada de local.
No incluir secretos. Figura 3: contenedor en nube; Figura 4: DNS y acceso externo.

## 8. Wireshark

Archivo de captura original: [PENDIENTE]. Equipo/interfaz/fecha: [PENDIENTE].

| Paquete real | Hora | IP origen/destino | Puertos | Transporte/flags | Payload | Explicación |
|---|---|---|---|---|---|---|
| [PENDIENTE] | | | | | | |

Figuras 5–7: handshake TCP, Follow TCP Stream, datagrama UDP [PENDIENTE].
Explicar relación entre aplicación, transporte y red, y limitaciones de captura.

## 9. Pruebas y resultados

| Caso | Entorno y versión | Comando/entrada | Esperado | Observado | Archivo de evidencia | Estado |
|---|---|---|---|---|---|---|
| HTTP / /status /nodes /alerts | [PENDIENTE] | | | | | |
| Cinco nodos y dos operadores | [PENDIENTE] | | | | | |
| Alerta | [PENDIENTE] | | | | | |
| STATS/DROP | [PENDIENTE] | | | | | |
| Duplicados/reordenamiento | [PENDIENTE] | | | | | |
| Error y recuperación | [PENDIENTE] | | | | | |
| DNS e Internet | [PENDIENTE] | | | | | |

Tabla de mensajes por ID/sesión: intentos, enviados, omitidos, errores, únicos,
duplicados, reordenados, pérdida estimada, reporte final y tiempo de drenaje.

## 10. Evidencias individuales

Repetir por integrante: nombre, whoami, hostname, fecha, componente, DNS de destino,
captura de su propio computador, explicación y referencia a contribución [PENDIENTE].

## 11. Problemas, soluciones y límites

HTTP bloqueante, escrituras concurrentes, cola lenta, pérdida estimada, recuperación,
compatibilidad TELEP/2, memoria acotada, ausencia de autenticación [PENDIENTE].
Distinguir defectos reproducidos de posibles causas del antiguo fallo remoto.

## 12. Conclusiones

Resultados demostrados, límites, aprendizaje por integrante [PENDIENTE].

## 13. Video y sustentación

Estado: NO GRABAR DEFINITIVO antes de puertas funcionales 1–4 aprobadas.
Enlace real y duración 15–18 min: [PENDIENTE]. Participación de todos: [PENDIENTE].
Guion: arquitectura 1m15, nube/Docker 1m45, DNS 1m, nodos 2m, operadores 1m45,
sockets/protocolo 1m15, alerta 1m15, STATS/DROP 2m, Wireshark 2m,
recuperación 2m, resultados 45s. Ajustar a 17 minutos con ejecución real.
