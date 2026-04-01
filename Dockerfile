FROM gcc:13-bookworm AS builder

WORKDIR /build
COPY server.c .
COPY Makefile .
RUN make

FROM debian:bookworm-slim

RUN apt-get update && \
    apt-get install -y --no-install-recommends ca-certificates && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /build/server .
RUN mkdir -p /app/logs

EXPOSE 9000

ENV AUTH_HOST=auth-service \
    AUTH_PORT=5000

CMD ["./server", "9000", "/app/logs/server.log"]