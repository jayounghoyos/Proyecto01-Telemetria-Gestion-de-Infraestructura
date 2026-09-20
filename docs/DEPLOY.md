# Docker, nube y DNS: procedimiento pendiente de ejecución autorizada

Este documento prescribe pasos; no afirma que se hayan ejecutado en nube.
No se hizo push, despliegue remoto ni actualización DNS durante esta implementación.

El Dockerfile usa Debian bookworm en ambas etapas para evitar desajustes de glibc,
compila con advertencias como errores y ejecuta como UID/GID 10001. La imagen
aún debe construirse y probarse con un motor Docker disponible.

## Construcción local

```bash
docker compose build --progress plain 2>&1 | tee docker-build.log
docker compose up -d
docker compose ps
docker inspect telemetry-server --format '{{.Image}} {{.State.Status}}'
docker port telemetry-server
docker compose logs --no-color > docker-runtime.log
curl --fail --max-time 5 http://localhost:8080/status
```

Registrar versión Docker/Compose y commit. Publicados: 5000/TCP, 5001/UDP y
8080/TCP. `restart: unless-stopped` no demuestra recuperación: probarla en un
contenedor de ensayo. No reiniciar producción para obtener evidencias.

## EC2 y preparación humana

Ubuntu con Docker/Compose y acceso SSH. Registrar región, tipo de instancia,
reglas de seguridad y versión de imagen, ocultando claves y tokens.
Limitar SSH a IPs autorizadas. Permitir los tres puertos desde los orígenes de prueba.

`deploy/ec2-user-data.sh` instala Docker y crea `/opt/telemetria/READY`.
`deploy/deploy.sh` espera el marcador, copia archivos y **recrea el contenedor**.
También puede actualizar DNS; no ejecutarlo como parte de pruebas locales.

`deploy/duckdns-update.sh` consulta IMDSv2 y necesita `/etc/duckdns.conf` con
DUCKDNS_SUBDOMAIN y DUCKDNS_TOKEN, creado por el responsable en la instancia
con permisos restringidos. No publicar tokens. La unidad oneshot al arrancar
no garantiza por sí sola que se haya actualizado el registro público.

## Verificación desde equipo externo

Después de desplegar de forma autorizada servidor y clientes TELEP/2 compatibles:

```bash
export TELEP_SERVER_HOST=telemetria-eafit.duckdns.org
nslookup "$TELEP_SERVER_HOST"
for route in / /status /nodes /alerts; do
  curl --fail --max-time 5 "http://$TELEP_SERVER_HOST:8080$route"
done
python3 client/operator_cli.py --host "$TELEP_SERVER_HOST"
```

`python3 tests/end_to_end.py --host "$TELEP_SERVER_HOST" --duration 120` crea
cinco nodos y alertas; solo usarlo en un entorno de ensayo autorizado. Guarda
resultados externos, pero no acredita Docker ni identidad individual por sí solo.
Cada integrante debe seguir EVIDENCE_GUIDE.md en su propio computador.

## Diagnóstico HTTP

Comparar curl local en la instancia con acceso externo; revisar contenedor,
publicación de puertos, logs y reglas. Los cambios resuelven cabeceras incompletas
reproducidas localmente, sin acreditar la causa del timeout histórico de producción.
