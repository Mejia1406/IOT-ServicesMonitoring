#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

ok=0
fail=0

check_process() {
  local label="$1"
  local pattern="$2"

  if pgrep -af "$pattern" >/dev/null 2>&1; then
    echo "[OK]   $label"
    ok=$((ok + 1))
  else
    echo "[FAIL] $label"
    fail=$((fail + 1))
  fi
}

echo "Healthcheck IoT Services"
echo "Proyecto: $PROJECT_DIR"
echo

check_process "Server (./server 9000 logs/server.log)" "./server 9000 logs/server.log"
check_process "Auth service (python3 auth.py)" "python3 auth.py"
check_process "Sensor client (python3 main.py)" "python3 main.py"
check_process "Operator client (java OperatorClient)" "java OperatorClient"

echo
if [[ "$fail" -eq 0 ]]; then
  echo "Resultado: OK ($ok/4 servicios arriba)"
  exit 0
fi

echo "Resultado: FAIL ($ok OK, $fail FAIL)"
echo "Tip: ejecuta ./bash-commands/start_all_wsl.sh y vuelve a correr este healthcheck."
exit 1
