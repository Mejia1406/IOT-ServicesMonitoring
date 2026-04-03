#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
RUN_DIR="$PROJECT_DIR/.run"

stop_by_pid_file() {
  local name="$1"
  local pid_file="$RUN_DIR/${name}.pid"

  if [[ -f "$pid_file" ]]; then
    local pid
    pid="$(cat "$pid_file")"
    if kill -0 "$pid" 2>/dev/null; then
      echo "Deteniendo $name (PID $pid)"
      kill "$pid" 2>/dev/null || true
    else
      echo "$name no estaba corriendo"
    fi
    rm -f "$pid_file"
  else
    echo "Sin PID file para $name"
  fi
}

stop_by_pid_file "operator"
stop_by_pid_file "sensors"
stop_by_pid_file "auth"
stop_by_pid_file "server"

# Also stop processes that might have been started manually.
pkill -f "./server 9000 logs/server.log" 2>/dev/null || true
pkill -9 -f "./server 9000 logs/server.log" 2>/dev/null || true
pkill -f "python3 auth.py" 2>/dev/null || true
pkill -9 -f "python3 auth.py" 2>/dev/null || true
pkill -f "python3 main.py" 2>/dev/null || true
pkill -9 -f "python3 main.py" 2>/dev/null || true
pkill -f "java OperatorClient" 2>/dev/null || true
pkill -9 -f "java OperatorClient" 2>/dev/null || true

echo "Listo: servicios detenidos."
