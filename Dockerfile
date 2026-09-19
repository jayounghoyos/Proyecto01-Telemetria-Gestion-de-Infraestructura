# Stage 1: build the C server
FROM gcc:14 AS build
WORKDIR /src
COPY server/ .
RUN make

# Stage 2: minimal runtime image with just the binary
FROM debian:bookworm-slim
WORKDIR /app
COPY --from=build /src/telemetry_server .
EXPOSE 5000/tcp 5001/udp 8080/tcp
CMD ["./telemetry_server"]
