# Client Sensor

Simulador de sensores IoT en Python para el sistema distribuido de monitoreo.

## Características

- Simula múltiples sensores concurrentes
- Usa TCP para conectarse al servidor
- Usa resolución de nombres mediante dominio
- Envía mensajes con protocolo de texto
- Reintenta conexión automáticamente
- Permite generar anomalías controladas

## Protocolo de mensajes

### Registro

REGISTER|sensor-01|temperature|plant-a

### Envío de medición

DATA|sensor-01|temperature|24.52|C|2026-03-31T23:15:21Z

### Heartbeat

HEARTBEAT|sensor-01|2026-03-31T23:15:30Z

### Cierre

QUIT|sensor-01

## Variables de entorno

- `IOT_SERVER_HOST`: dominio o hostname del servidor
- `IOT_SERVER_PORT`: puerto del servidor
- `RECONNECT_DELAY`: segundos entre reintentos
- `SOCKET_TIMEOUT`: timeout del socket
- `SEND_INTERVAL_MIN`: intervalo mínimo general
- `SEND_INTERVAL_MAX`: intervalo máximo general
- `HEARTBEAT_EVERY`: cada cuántos mensajes enviar heartbeat
- `ANOMALY_MODE`: activa o desactiva anomalías
- `ANOMALY_PROBABILITY`: probabilidad general de anomalía
- `ENABLE_SERVER_RESPONSE_WAIT`: esperar respuesta del servidor

## Ejecución

### Linux / macOS

```bash
export IOT_SERVER_HOST=iot-monitoring.example.com
export IOT_SERVER_PORT=9000
python3 main.py
```

### Windows PowerShell

```bash
$env:IOT_SERVER_HOST="iot-monitoring.example.com"
$env:IOT_SERVER_PORT="9000"
python main.py
```

---

### Ejemplo de mensajes que enviará

```txt
REGISTER|sensor-01|temperature|plant-a
DATA|sensor-01|temperature|24.73|C|2026-03-31T23:15:21Z
DATA|sensor-03|vibration|12.41|Hz|2026-03-31T23:15:24Z
HEARTBEAT|sensor-01|2026-03-31T23:15:30Z
QUIT|sensor-01
```
