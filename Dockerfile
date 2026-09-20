# Matching distributions avoid copying a binary linked to a newer glibc.
FROM debian:bookworm-slim AS build
# libc6-dev es obligatorio: con --no-install-recommends, el paquete gcc no lo
# arrastra y faltan las cabeceras de la libreria estandar (time.h, string.h...).
RUN apt-get update && apt-get install -y --no-install-recommends gcc make libc6-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY server/ .
RUN make CFLAGS="-std=gnu11 -Wall -Wextra -Werror -O2 -pthread"

FROM debian:bookworm-slim
WORKDIR /app
COPY --from=build /src/telemetry_server .
USER 10001:10001
EXPOSE 5000/tcp 5001/udp 8080/tcp
CMD ["./telemetry_server"]
