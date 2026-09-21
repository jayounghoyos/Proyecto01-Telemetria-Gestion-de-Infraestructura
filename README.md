# Plataforma de telemetría — TELEP/2.0

Servidor C POSIX, nodos y operadores Python 3.8+, TCP propio para control,
UDP propio para mediciones y HTTP de consulta. No se usa HTTP para sustituir TELEP.

## Informe

El informe técnico del proyecto está en
**[Informe_Proyecto01_telemetria.pdf](Informe_Proyecto01_telemetria.pdf)**, en la raíz
del repositorio. Contiene el diseño del protocolo, la arquitectura, el despliegue en AWS,
el análisis con Wireshark, las pruebas y las evidencias individuales.

## Inicio local (Linux, Ubuntu/WSL)

Requisitos: GCC, Make y Python 3; Tkinter para GUI (`python3-tk` en Ubuntu).
Desde la raíz del repositorio:

```bash
make -C server CFLAGS="-std=gnu11 -Wall -Wextra -Werror -O2 -pthread"
./server/telemetry_server
```

En otras terminales:

```bash
python3 client/node.py --id NODE01 --host localhost --interval 2
python3 client/operator_cli.py --host localhost
python3 client/operator_gui.py --host localhost
curl --max-time 5 http://localhost:8080/status
```

Puertos: 5000/TCP, 5001/UDP, 8080/TCP. El nombre se configura con `--host`
o `TELEP_SERVER_HOST`; el valor predeterminado es localhost. No hay IP pública fija
embebida. Cada nodo genera una sesión UUID. Un ID no puede cambiar de sesión
hasta reiniciar el servidor; use IDs distintos entre procesos y ejecuciones.
Reenviar HELLO con la misma sesión no borra datos ni estadísticas.

## Pruebas reproducibles

No debe haber otro servidor ocupando esos puertos. Los scripts se niegan a
reutilizar servicios existentes y detienen únicamente sus propios procesos.

```bash
python3 tests/run.py
python3 tests/end_to_end.py --start-server --duration 120
```

La primera orden prueba HTTP, sesiones/estadísticas, errores, alertas, dos
operadores, suscriptor lento, recuperación y controlador GUI sin pantalla.
La segunda ejecuta cinco procesos reales de `node.py` y dos conexiones persistentes
de operador durante al menos 120 segundos, comprueba actividad, alertas y conteos.
El resumen de la última ejecución, con los hashes de las fuentes, queda en
`tests/results/functional.json`.

## DROP y STATS

```bash
python3 client/node.py --host localhost --id DROP01 --count 100 --interval 0.05 --drop 25
python3 -c 'import sys; sys.path.insert(0,"client"); import telep; print(telep.send_one_request("STATS","DROP01",hostname="localhost"))'
```

DROP=25 omite exactamente 25 de 100 intentos **antes de sendto**. Se esperan
75 envíos exitosos y, sin pérdida de red, 75 recepciones únicas y pérdida estimada 0.
La omisión simulada se informa separadamente; no se presenta como pérdida en la red.
El operador CLI incluye STATS (opción 7) y comandos crudos (opción 6).

## Docker y despliegue

```bash
docker compose build --progress plain
docker compose up -d
docker compose ps
curl --max-time 5 http://localhost:8080/status
```

Véanse [DEPLOY](docs/DEPLOY.md), [guía de evidencias](docs/EVIDENCE_GUIDE.md),
[protocolo](docs/PROTOCOL.md), [arquitectura](docs/ARCHITECTURE.md) y
[plantilla de informe](docs/REPORT_TEMPLATE.md).

El servidor está desplegado en una instancia EC2 y se alcanza por su nombre DNS,
`telemetria-eafit.duckdns.org`. `deploy/deploy.sh` sube el código, construye la
imagen en la instancia y recrea el contenedor; el procedimiento completo, con las
reglas del grupo de seguridad, está en [DEPLOY](docs/DEPLOY.md).

## Límites explícitos

TELEP/2 cambia HELLO y TELEMETRY respecto de TELEP/1: actualizar servidor y clientes
juntos. Estado en memoria, sin autenticación, 64 IDs por proceso, 65536 intentos
por sesión, hasta 32 suscriptores, 64 clientes TCP y 16 trabajadores HTTP.
La GUI reintenta tras errores y conserva su ventana; como sus consultas son
síncronas, una operación de red puede ocupar el hilo gráfico hasta el timeout de
cinco segundos.
