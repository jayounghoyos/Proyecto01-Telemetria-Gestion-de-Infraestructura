# Guía de evidencia real y puertas de aceptación

Todos los comandos siguientes se ejecutan desde la raíz del repositorio en Linux/WSL,
salvo los equivalentes Windows indicados. No se proporcionan capturas, .pcapng ni
resultados remotos ficticios. El resultado esperado no debe copiarse como observado.

## Puertas funcionales antes de video

1. HTTP: cuatro rutas, datos reales, petición incompleta aislada, plazo absoluto,
   método/ruta/cabeceras inválidas. Tests `test_http_*`.
2. Concurrencia: dos operadores, alertas y UDP continuo; cola llena desconecta al
   lento sin bloquear receptor. Tests `test_two_operators_*`, `test_slow_subscriber_*`.
3. UDP: STATS, DROP conocido/sin DROP, duplicados, reordenamiento y paquete tardío
   que reduce pérdida estimada. Tests `test_node_*drop`, `test_stats_*`.
4. Robustez: validación TCP/UDP, recuperación tras reinicio de ensayo, DNS renovado
   y controlador GUI. Tests restantes. La prueba visual GUI se recoge aparte.

```bash
make -C server CFLAGS="-std=gnu11 -Wall -Wextra -Werror -O2 -pthread"
python3 tests/run.py
python3 tests/end_to_end.py --start-server --duration 120
```

Ambos deben salir con código 0. Conservar JSON, hashes de fuentes, fecha y logs.
No iniciar video definitivo si alguna puerta falla. Pasar localmente tampoco
sustituye los requisitos de nube, Docker, Wireshark y participación individual.

## Evidencia individual

Cada integrante abre su propia terminal y registra, sin cortar la secuencia:

```bash
whoami
hostname
date -Is
git rev-parse HEAD
python3 client/operator_cli.py --host telemetria-eafit.duckdns.org
```

En PowerShell: `whoami`, `hostname`, `Get-Date -Format o`, `git rev-parse HEAD`.
Después ejecutar el componente correspondiente. Para un nodo usar un ID exclusivo
por integrante y sesión de ensayo. Capturar nombre DNS, registro/conexión y datos.
No incluir claves, tokens, archivos .pem ni consola con secretos.

## Cinco nodos y dos operadores

Ensayo local automático de dos minutos: orden anterior `end_to_end.py --start-server`.
Para evidencia humana, servidor ya iniciado y dos terminales de operador:

```bash
export TELEP_SERVER_HOST=localhost
python3 client/operator_cli.py --host "$TELEP_SERVER_HOST"
# En otra terminal:
python3 client/operator_gui.py --host "$TELEP_SERVER_HOST"
# En una tercera terminal, con IDs nuevos para esta ejecución:
bash client/run_nodes.sh 5 "$TELEP_SERVER_HOST"
```

Observar 5 ACTIVE durante >=120 segundos, consultar un ID y últimas mediciones en
ambos operadores, abrir web. Detener los nodos con el script solo si se iniciaron
con él en esta misma sesión. Preferir el runner Python para evitar PIDs antiguos.
Para nube usar el DNS real únicamente tras despliegue autorizado compatible:

```bash
python3 tests/end_to_end.py --host telemetria-eafit.duckdns.org --duration 120
```

Esta orden genera datos/alertas; no ejecutarla sobre producción sin autorización.

## Docker y DNS

En máquina de ensayo:

```bash
mkdir -p evidence
docker version > evidence/docker-version.txt
docker compose build --progress plain 2>&1 | tee evidence/docker-build.txt
docker compose up -d
docker compose ps > evidence/docker-ps.txt
docker port telemetry-server > evidence/docker-ports.txt
docker inspect telemetry-server --format '{{.Image}} {{.State.Status}}' > evidence/docker-image.txt
docker compose logs --no-color > evidence/docker-logs.txt
nslookup telemetria-eafit.duckdns.org > evidence/dns.txt
```

Las salidas Docker locales prueban Docker local. Obtener además las correspondientes
en EC2, con identidad de instancia visible, para acreditar nube. Desde otro equipo:

```bash
for route in / /status /nodes /alerts; do
  curl --fail --max-time 5 "http://telemetria-eafit.duckdns.org:8080$route"
done
```

## STATS/DROP y conteos

Con servidor de ensayo local:

```bash
python3 client/node.py --id SIN_DROP --host localhost --count 100 --interval 0.05 --drop 0 | tee evidence/sin-drop.txt
python3 client/node.py --id CON_DROP --host localhost --count 100 --interval 0.05 --drop 25 | tee evidence/con-drop.txt
python3 -c 'import sys; sys.path.insert(0,"client"); import telep; print(telep.send_one_request("STATS",hostname="localhost"))' | tee evidence/stats.txt
curl --fail --max-time 5 http://localhost:8080/status > evidence/status.json
```

Esperado, no resultado inventado: 100/100/0 y 100/75/25 para intentos/envíos/omisiones.
Comparar JSON final del nodo, STATS y secuencias de captura. Esperar drenaje y repetir
STATS; anotar cuánto se esperó. `loss_estimated` puede disminuir con paquetes tardíos.
La prueba automatizada de reordenamiento inyecta secuencias 0,2,1,3,2 y una cola
posterior; no es una medición de pérdidas espontáneas de Internet.

## Captura Wireshark propia

1. Elegir interfaz: loopback para localhost, interfaz de red real para nube; en
   WSL las interfaces no siempre coinciden con las de Windows. En Docker observar
   desde el cliente externo simplifica distinguir direcciones/NAT.
2. Iniciar captura **antes** de abrir una conexión de operador. Filtro de captura:
   `tcp port 5000 or udp port 5001` (agregar `or port 53` si se analiza DNS).
3. Abrir un operador, ejecutar GET_STATUS/GET_NODES/STATS; arrancar un nodo y generar
   una anomalía con `--spike TEMP=45`. Terminar la captura después del reporte final.
4. Guardar como .pcapng desde Wireshark. Alternativa Linux con tcpdump, archivo pcap real:

```bash
sudo tcpdump -i lo -nn -s 0 -w evidence/telep-local.pcap 'tcp port 5000 or udp port 5001'
```

Usar la interfaz correcta en lugar de lo para nube. Detener con Ctrl-C. No renombrar
un archivo de texto a pcapng. El script `scripts/capture_traffic.sh` ofrece lo mismo.

Filtros de visualización:

- `tcp.port == 5000 && tcp.flags.syn == 1`: localizar SYN y SYN-ACK.
- `tcp.stream eq N`: cambiar N por el stream real; identificar el ACK que completa
  el handshake y usar Follow TCP Stream para solicitudes/respuestas TELEP.
- `udp.port == 5001`: mostrar datagramas con ID, sesión, secuencia y mediciones.
- `dns`: si se capturó DNS. Puede no aparecer por caché; documentarlo, no inventarlo.

Anotar **números reales de paquetes**, hora, IP origen/destino, puertos, flags,
secuencia/ACK TCP, longitud UDP y payload. Relacionar Ethernet/interfaz, IP,
TCP/UDP y TELEP; explicar NAT si las direcciones difieren entre puntos de captura.
Adjuntar un pantallazo del handshake, Follow TCP Stream y un datagrama expandido.

## Recuperación

En entorno local preparado: enviar un comando desconocido desde opción 6 del CLI
y luego GET_STATUS; detener un nodo propio y esperar 15 segundos para INACTIVE.
Para retomar exactamente ese nodo, conservar sesión/contadores mediante el proceso
original; un nuevo proceso usa otro ID. Probar reinicio del servidor/contendor solo
en ensayo. La suite prueba el reinicio de su servidor local y separación por boot_id.
No confundir esa prueba con reinicio validado en nube.

## Registro de resultado

Usar REPORT_TEMPLATE.md. Separar siempre: ejecutado y aprobado, fallo observado,
procedimiento preparado y pendiente de ejecutar. Guardar fecha, commit, hashes,
equipo, responsable y salida completa. No presentar plantillas como evidencia.
