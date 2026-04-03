# IOT Services Monitoring

Sistema distribuido de monitoreo de sensores IoT desarrollado para la asignatura **Internet: Arquitectura y Protocolos**.

## Descripción general

El proyecto implementa una plataforma de monitoreo distribuido donde:

- sensores simulados envían mediciones periódicas al servidor central
- operadores se autentican y supervisan el sistema en tiempo real
- un servicio externo de autenticación valida usuarios y roles
- el servidor ofrece también una interfaz HTTP básica para consultar el estado del sistema

## Arquitectura

El sistema está compuesto por:

- **server/**: servidor central implementado en C con Berkeley sockets
- **client-sensor/**: simulador de sensores en Python
- **client-operator/**: cliente operador con interfaz gráfica en Java
- **auth-service/**: servicio externo de autenticación en Flask
- **docs/**: documentación del protocolo, arquitectura y despliegue

## Tecnologías utilizadas

- C
- Python
- Java
- Flask
- Docker
- AWS EC2
- AWS Route 53

## Protocolo de aplicación

El sistema usa un protocolo de texto basado en mensajes separados por `|`.

Ejemplos:

```txt
REGISTER|sensor-01|temperature|plant-a
DATA|sensor-01|temperature|24.52|C|2026-03-31T23:15:21Z
HEARTBEAT|sensor-01|2026-03-31T23:15:30Z
AUTH_OPERATOR|OP-001|sara|1234
STATUS
LIST_SENSORS
LIST_ALERTS
QUIT|sensor-01
```

### Ejecución local

1. Auth service
   cd auth-service
   python3 auth.py

2. Servidor
   cd server
   make
   ./server 9000 server.log

3. Sensores
   cd client-sensor
   export IOT_SERVER_HOST=localhost
   export IOT_SERVER_PORT=9000
   python3 main.py

4. Operador
   cd client-operator
   javac OperatorClient.java
   java OperatorClient

5. Global
   Si quieres hacerlo desde PowerShell sin entrar manualmente a WSL, usa:

wsl bash -ic "source ~/.bashrc; iot-up"
wsl bash -ic "source ~/.bashrc; iot-check"
wsl bash -ic "source ~/.bashrc; iot-down"
