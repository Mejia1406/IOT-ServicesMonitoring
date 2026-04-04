# Protocolo de Aplicación — IoT Monitoring System

**Asignatura:** Internet: Arquitectura y Protocolos  
**Versión:** 1.0  
**Fecha:** 2026

---

## 1. Descripción General

El protocolo IoT-MP (IoT Monitoring Protocol) es un protocolo de la capa de aplicación
basado en texto diseñado para la comunicación entre tres tipos de entidades:

- **Sensores IoT simulados** — envían mediciones periódicas al servidor
- **Operadores del sistema** — supervisan el sistema en tiempo real
- **Servidor central de monitoreo** — recibe, procesa y redistribuye la información

El protocolo opera sobre **TCP (SOCK_STREAM)**, lo cual garantiza entrega ordenada y
confiable de los mensajes, requisito fundamental para un sistema de monitoreo donde
la pérdida de alertas críticas no es aceptable.

El servidor también expone una **interfaz HTTP/1.1** sobre el mismo puerto para
consultas desde navegadores web.

---

## 2. Capa de Transporte

| Conexión                | Protocolo      | Socket      | Justificación                        |
| ----------------------- | -------------- | ----------- | ------------------------------------ |
| Sensor → Servidor       | TCP            | SOCK_STREAM | Confiabilidad en envío de mediciones |
| Operador → Servidor     | TCP            | SOCK_STREAM | Recepción garantizada de alertas     |
| Servidor → Auth Service | TCP + HTTP/1.1 | SOCK_STREAM | Protocolo estándar de autenticación  |
| Navegador → Servidor    | TCP + HTTP/1.1 | SOCK_STREAM | Interfaz web estándar                |

---

## 3. Formato de Mensajes

### 3.1 Formato general (protocolo IoT-MP)

```
COMANDO|campo1|campo2|...|campoN\n
```

- Los campos se separan con el carácter `|` (pipe)
- Cada mensaje termina con `\n` (newline)
- Los mensajes son texto plano codificado en UTF-8
- El servidor procesa los mensajes línea a línea

### 3.2 Formato de respuestas

```
ESTADO campo1|campo2|...\n
```

o bien en caso de error:

```
ERROR|CODIGO_ERROR\n
```

---

## 4. Comandos del Protocolo

### 4.1 REGISTER — Registro de sensor

Enviado por el sensor al conectarse al servidor para identificarse.

**Formato:**

```
REGISTER|<sensor_id>|<sensor_type>|<location>\n
```

**Campos:**
| Campo | Tipo | Descripción | Ejemplo |
|---|---|---|---|
| sensor_id | string | Identificador único del sensor | `sensor-01` |
| sensor_type | string | Tipo de sensor | `temperature`, `vibration`, `energy`, `humidity`, `operational` |
| location | string | Ubicación física del sensor | `plant-a`, `boiler-room` |

**Respuesta exitosa:**

```
OK REGISTERED\n
```

**Ejemplo completo:**

```
→ REGISTER|sensor-01|temperature|plant-a
← OK REGISTERED
```

---

### 4.2 DATA — Envío de medición

Enviado periódicamente por el sensor con la lectura actual.

**Formato:**

```
DATA|<sensor_id>|<sensor_type>|<value>|<unit>|<timestamp>\n
```

**Campos:**
| Campo | Tipo | Descripción | Ejemplo |
|---|---|---|---|
| sensor_id | string | ID del sensor que reporta | `sensor-01` |
| sensor_type | string | Tipo de sensor | `temperature` |
| value | float \| string | Valor medido | `24.52`, `OK`, `FAIL`, `CRITICAL` |
| unit | string | Unidad de medida | `C`, `Hz`, `W`, `%`, `state` |
| timestamp | string ISO 8601 | Momento de la medición (UTC) | `2026-04-03T20:53:01Z` |

**Valores numéricos por tipo de sensor:**

| Tipo        | Rango normal                                 | Rango anomalía     | Unidad  |
| ----------- | -------------------------------------------- | ------------------ | ------- |
| temperature | 20 – 29 °C (planta A) / 45 – 68 °C (caldera) | > 80 °C / < -10 °C | `C`     |
| vibration   | 0.2 – 4.5                                    | > 7.5              | `Hz`    |
| energy      | 250 – 650                                    | > 800              | `W`     |
| humidity    | 35 – 60                                      | > 90               | `%`     |
| operational | `OK`, `WARNING`                              | `FAIL`, `CRITICAL` | `state` |

**Respuesta exitosa:**

```
OK DATA\n
```

**Efecto secundario:** el servidor evalúa el valor contra los umbrales de anomalía.
Si se supera un umbral, genera una alerta y la transmite a todos los operadores conectados.

**Ejemplo completo:**

```
→ DATA|sensor-01|temperature|24.52|C|2026-04-03T20:53:01Z
← OK DATA
```

---

### 4.3 HEARTBEAT — Señal de vida

Enviado periódicamente por el sensor para confirmar que sigue activo (cada 5 mensajes DATA).

**Formato:**

```
HEARTBEAT|<sensor_id>|<timestamp>\n
```

**Respuesta exitosa:**

```
OK HEARTBEAT\n
```

**Ejemplo:**

```
→ HEARTBEAT|sensor-03|2026-04-03T20:54:00Z
← OK HEARTBEAT
```

---

### 4.4 QUIT — Desconexión controlada

Enviado por el sensor antes de cerrar la conexión.

**Formato:**

```
QUIT|<sensor_id>\n
```

**Respuesta:**

```
OK BYE\n
```

El servidor marca el sensor como inactivo en su registro.

---

### 4.5 AUTH_OPERATOR — Autenticación de operador

Enviado por el cliente operador para autenticarse en el sistema.

**Formato:**

```
AUTH_OPERATOR|<op_id>|<username>|<password>\n
```

**Campos:**
| Campo | Tipo | Descripción | Ejemplo |
|---|---|---|---|
| op_id | string | Identificador del operador en sesión | `OP-001` |
| username | string | Nombre de usuario | `sara` |
| password | string | Contraseña en texto plano | `1234` |

**Respuesta exitosa:**

```
OK AUTH_OPERATOR|<op_id>|<role>\n
```

**Respuesta de error:**

```
ERROR|AUTH_FAILED\n
```

**Flujo de autenticación:**
El servidor NO almacena usuarios localmente. Al recibir `AUTH_OPERATOR`, el servidor
consulta el **servicio externo de autenticación** (auth-service) mediante una petición
HTTP POST al endpoint `/auth`. El auth-service devuelve el rol del usuario.

```
Operador ──AUTH_OPERATOR──► Servidor C ──HTTP POST /auth──► Auth Service (Flask)
                             Servidor C ◄──{"role":"operator"}── Auth Service
Operador ◄──OK AUTH_OPERATOR|OP-001|operator──
```

**Ejemplo:**

```
→ AUTH_OPERATOR|OP-001|sara|1234
← OK AUTH_OPERATOR|OP-001|operator
```

---

### 4.6 STATUS — Consulta de estado del sistema

Enviado por el operador para obtener un resumen del sistema.

**Formato:**

```
STATUS\n
```

**Respuesta:**

```
STATUS|sensors=<N>|operators=<N>|alerts=<N>\n
```

**Ejemplo:**

```
→ STATUS
← STATUS|sensors=6|operators=1|alerts=3
```

--- 

### 4.7 LIST_SENSORS — Listar sensores activos

Enviado por el operador para obtener la lista de sensores activos con su último valor.

**Formato:**

```
LIST_SENSORS\n
```

**Respuesta:**

```
SENSORS|<id>,<type>,<location>,<last_value>,<unit>|...\n
```

**Ejemplo:**

```
→ LIST_SENSORS
← SENSORS|sensor-01,temperature,plant-a,24.52,C|sensor-03,vibration,motor-room,3.10,Hz|...
```

---

### 4.8 LIST_ALERTS — Listar alertas recientes

Solicita las últimas 20 alertas registradas en el servidor.

**Formato:**

```
LIST_ALERTS\n
```

**Respuesta:**

```
ALERTS|<sensor_id>,<alert_type>,<value>|...\n
```

**Ejemplo:**

```
→ LIST_ALERTS
← ALERTS|sensor-02,temperature,87.30|sensor-04,energy,912.50|...
```

---

## 5. Mensajes de Alerta (Push del servidor a operadores)

Las alertas son mensajes **no solicitados** que el servidor envía automáticamente a todos
los operadores conectados cuando detecta un valor anómalo. No requieren respuesta del
operador.

**Formato general:**

```
ALERT|<alert_type>|<sensor_id>|<value>|<unit>|threshold=<umbral>\n
```

**Tipos de alerta:**

| Tipo               | Condición                | Ejemplo                                                       |
| ------------------ | ------------------------ | ------------------------------------------------------------- |
| `HIGH_TEMP`        | temperatura > 80.0 °C    | `ALERT\|HIGH_TEMP\|sensor-02\|87.30\|C\|threshold=80.0`       |
| `LOW_TEMP`         | temperatura < -10.0 °C   | `ALERT\|LOW_TEMP\|sensor-01\|-12.50\|C\|threshold=-10.0`      |
| `HIGH_VIBRATION`   | vibración > 7.5 Hz       | `ALERT\|HIGH_VIBRATION\|sensor-03\|11.20\|Hz\|threshold=7.5`  |
| `HIGH_ENERGY`      | energía > 800.0 W        | `ALERT\|HIGH_ENERGY\|sensor-04\|912.50\|W\|threshold=800.0`   |
| `HIGH_HUMIDITY`    | humedad > 90.0 %         | `ALERT\|HIGH_HUMIDITY\|sensor-05\|93.10\|%\|threshold=90.0`   |
| `OPERATIONAL_FAIL` | estado = FAIL o CRITICAL | `ALERT\|OPERATIONAL_FAIL\|sensor-06\|FAIL\|2026-04-03T20:53Z` |

---

## 6. Mensajes de Error

| Código                    | Causa                                        |
| ------------------------- | -------------------------------------------- |
| `ERROR\|EMPTY_MESSAGE`    | Se recibió una línea vacía                   |
| `ERROR\|INVALID_REGISTER` | REGISTER con menos de 4 campos               |
| `ERROR\|INVALID_DATA`     | DATA con menos de 6 campos                   |
| `ERROR\|INVALID_AUTH`     | AUTH_OPERATOR con menos de 4 campos          |
| `ERROR\|AUTH_FAILED`      | Credenciales inválidas según el auth service |
| `ERROR\|UNKNOWN_COMMAND`  | Comando no reconocido                        |

---

## 7. Interfaz HTTP/1.1

El servidor atiende también peticiones HTTP en el mismo puerto (9000). Detecta la
presencia de métodos HTTP (`GET`, `POST`, `HEAD`) en el primer recv del cliente.

| Método | Ruta           | Content-Type       | Descripción                              |
| ------ | -------------- | ------------------ | ---------------------------------------- |
| GET    | `/`            | `text/html`        | Dashboard HTML con resumen del sistema   |
| GET    | `/index.html`  | `text/html`        | Mismo que `/`                            |
| GET    | `/api/status`  | `application/json` | `{"sensors":N,"operators":N,"alerts":N}` |
| GET    | `/api/sensors` | `application/json` | Array JSON de sensores activos           |
| GET    | `/api/alerts`  | `application/json` | Array JSON de últimas 20 alertas         |
| \*     | cualquier otra | —                  | HTTP 404 Not Found                       |

---

## 8. Diagrama de Flujo Completo

```
SENSOR                      SERVIDOR C                   OPERADOR            AUTH SERVICE
  |                             |                            |                     |
  |──REGISTER|sensor-01|...────►|                            |                     |
  |◄──OK REGISTERED─────────────|                            |                     |
  |                             |                            |                     |
  |──DATA|sensor-01|...|24.52──►|                            |                     |
  |                             |──(broadcast DATA)─────────►|                     |
  |◄──OK DATA───────────────────|                            |                     |
  |                             |                            |                     |
  |──DATA|sensor-02|...|87.30──►|                            |                     |
  |                             |──ALERT|HIGH_TEMP|..────────►|                     |
  |◄──OK DATA───────────────────|                            |                     |
  |                             |                            |                     |
  |──HEARTBEAT|sensor-01|...───►|                            |                     |
  |◄──OK HEARTBEAT──────────────|                            |                     |
  |                             |                            |──AUTH_OPERATOR|────►|
  |                             |                            |                     |
  |                             |◄────────────── AUTH_OPERATOR|OP-001|sara|1234────|
  |                             |──POST /auth {"username":"sara",...}──────────────►|
  |                             |◄──{"status":"ok","role":"operator"}───────────────|
  |                             |──OK AUTH_OPERATOR|OP-001|operator────────────────►|
  |                             |                            |                     |
  |                             |◄─────────────────── STATUS |                     |
  |                             |──STATUS|sensors=6|...─────►|                     |
  |                             |                            |                     |
  |──QUIT|sensor-01────────────►|                            |                     |
  |◄──OK BYE────────────────────|                            |                     |
```

---

## 9. Umbrales de Anomalía (Servidor)

| Tipo sensor | Variable               | Umbral alerta | Tipo alerta      |
| ----------- | ---------------------- | ------------- | ---------------- |
| temperature | `THRESH_TEMP_HIGH`     | 80.0 °C       | HIGH_TEMP        |
| temperature | `THRESH_TEMP_LOW`      | -10.0 °C      | LOW_TEMP         |
| vibration   | `THRESH_VIB_HIGH`      | 7.5 Hz        | HIGH_VIBRATION   |
| energy      | `THRESH_ENERGY_HIGH`   | 800.0 W       | HIGH_ENERGY      |
| humidity    | `THRESH_HUMIDITY_HIGH` | 90.0 %        | HIGH_HUMIDITY    |
| operational | estado FAIL/CRITICAL   | —             | OPERATIONAL_FAIL |

---

## 10. Resolución de Nombres

El sistema no utiliza direcciones IP codificadas. Todos los servicios se localizan
mediante nombres de dominio resueltos en tiempo de ejecución:

| Variable de entorno | Servicio que la usa              | Valor por defecto |
| ------------------- | -------------------------------- | ----------------- |
| `IOT_SERVER_HOST`   | sensor (Python), operador (Java) | `localhost`       |
| `IOT_SERVER_PORT`   | sensor (Python), operador (Java) | `9000`            |
| `AUTH_HOST`         | servidor (C)                     | `auth-service`    |
| `AUTH_PORT`         | servidor (C)                     | `5000`            |

Si la resolución DNS falla, el sistema maneja la excepción (`socket.gaierror` en Python,
`getaddrinfo()` en C) y reintenta la conexión sin terminar su ejecución.
