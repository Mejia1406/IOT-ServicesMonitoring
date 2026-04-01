# ── Etapa 1: compilar ────────────────────────────────────────────
FROM gcc:13-bookworm AS builder

WORKDIR /build
COPY server.c .
COPY Makefile .
RUN make

# ── Etapa 2: imagen final liviana ────────────────────────────────
FROM debian:bookworm-slim

RUN apt-get update && \
    apt-get install -y --no-install-recommends ca-certificates && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /build/server .
RUN mkdir -p /app/logs

EXPOSE 9000

# AUTH_HOST apunta al contenedor del auth-service (Flask)
ENV AUTH_HOST=auth-service \
    AUTH_PORT=5000

CMD ["./server", "9000", "/app/logs/server.log"]