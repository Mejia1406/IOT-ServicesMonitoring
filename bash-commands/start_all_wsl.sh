#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
RUN_DIR="$PROJECT_DIR/.run"
MAIN_LOG_DIR="$PROJECT_DIR/logs"
SERVER_LOG_DIR="$PROJECT_DIR/server/logs"

mkdir -p "$RUN_DIR" "$MAIN_LOG_DIR" "$SERVER_LOG_DIR"

kill_if_running() {
  local name="$1"
  local pid_file="$RUN_DIR/${name}.pid"
  local expected=""

  case "$name" in
    server) expected="./server 9000 logs/server.log" ;;
    auth) expected="python3 auth.py" ;;
    sensors) expected="python3 main.py" ;;
    operator) expected="java OperatorClient" ;;
  esac

  if [[ -f "$pid_file" ]]; then
    local pid
    pid="$(cat "$pid_file")"
    if kill -0 "$pid" 2>/dev/null && ps -p "$pid" -o args= 2>/dev/null | grep -Fq "$expected"; then
      echo "Deteniendo proceso anterior: $name (PID $pid)"
      kill "$pid" 2>/dev/null || true
      sleep 0.5
    fi
    rm -f "$pid_file"
  fi
}

kill_legacy_processes() {
  # Clean up manually-started processes with no PID files.
  pkill -f "./server 9000 logs/server.log" 2>/dev/null || true
  pkill -9 -f "./server 9000 logs/server.log" 2>/dev/null || true
  pkill -f "python3 auth.py" 2>/dev/null || true
  pkill -9 -f "python3 auth.py" 2>/dev/null || true
  pkill -f "python3 main.py" 2>/dev/null || true
  pkill -9 -f "python3 main.py" 2>/dev/null || true
  pkill -f "java OperatorClient" 2>/dev/null || true
  pkill -9 -f "java OperatorClient" 2>/dev/null || true
}

free_port_9000() {
  # Force-kill any process still listening on port 9000.
  local pids
  pids="$(ss -ltnp 2>/dev/null | awk '/:9000 / {print $NF}' | sed -n 's/.*pid=\([0-9]\+\).*/\1/p' | sort -u)"
  if [[ -n "$pids" ]]; then
    echo "Liberando puerto 9000 (PIDs: $pids)"
    for pid in $pids; do
      kill "$pid" 2>/dev/null || true
      kill -9 "$pid" 2>/dev/null || true
    done
  fi
}

wait_port_free_9000() {
  local tries=10
  while (( tries > 0 )); do
    if ! ss -ltn 2>/dev/null | awk '{print $4}' | grep -qE '(^|:)9000$'; then
      return 0
    fi
    sleep 1
    tries=$((tries - 1))
  done
  return 1
}

start_bg() {
  local name="$1"
  local cmd="$2"
  local log_file="$3"

  kill_if_running "$name"

  echo "Iniciando $name..."
  bash -lc "cd '$PROJECT_DIR' && $cmd" >"$log_file" 2>&1 &
  local pid=$!
  sleep 1

  if ! kill -0 "$pid" 2>/dev/null; then
    echo "ERROR: $name no inicio correctamente. Revisar $log_file"
    tail -n 40 "$log_file" || true
    return 1
  fi

  echo "$pid" >"$RUN_DIR/${name}.pid"
  echo "  -> $name corriendo con PID $pid"
  return 0
}

echo "Limpiando procesos previos..."
kill_legacy_processes
free_port_9000
sleep 1

if ! wait_port_free_9000; then
  echo "ERROR: el puerto 9000 sigue ocupado."
  ss -ltnp 2>/dev/null | grep ':9000' || true
  exit 1
fi

# 1) Servidor (compila si falta binario)
if [[ ! -x "$PROJECT_DIR/server/server" ]]; then
  echo "Compilando servidor..."
  (cd "$PROJECT_DIR/server" && make)
fi

server_started=0
for _ in 1 2 3; do
  if start_bg "server" "cd server && ./server 9000 logs/server.log" "$MAIN_LOG_DIR/server.out"; then
    server_started=1
    break
  fi
  sleep 2
done

if [[ "$server_started" -ne 1 ]]; then
  echo "ERROR: no se pudo iniciar el servidor en el puerto 9000."
  exit 1
fi

# 2) Auth service
start_bg "auth" "cd auth-service && python3 auth.py" "$MAIN_LOG_DIR/auth.out"

# 3) Sensores
start_bg "sensors" "cd client-sensor && IOT_SERVER_HOST=localhost IOT_SERVER_PORT=9000 python3 main.py" "$MAIN_LOG_DIR/sensors.out"

# 4) Cliente operador
start_bg "operator" "cd client-operator && javac OperatorClient.java && java OperatorClient" "$MAIN_LOG_DIR/operator.out"

cat <<'EOF'

Servicios iniciados.

Logs:
- logs/server.out
- logs/auth.out
- logs/sensors.out
- logs/operator.out

Comandos utiles:
- Ver procesos: ps -ef | grep -E 'auth.py|OperatorClient|server 9000|main.py' | grep -v grep
- Ver logs en vivo: tail -f logs/sensors.out
- Healthcheck: ./bash-commands/healthcheck_wsl.sh
- Detener todo: ./bash-commands/stop_all_wsl.sh

EOF
