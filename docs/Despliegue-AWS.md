## GUÍA DE DESPLIEGUE EN AWS

> Esta guía está adaptada ESPECÍFICAMENTE al código de tu proyecto (puerto 9000, auth en 5000, etc.)

---

### FASE 0: Preparación previa (en tu máquina local)

```bash
# Asegúrate de tener Docker instalado localmente para probar
docker --version

# Entra a tu proyecto
cd IOT-ServicesMonitoring

# Crea el Dockerfile del auth-service si no existe
cat > auth-service/Dockerfile << 'EOF'
FROM python:3.11-slim
WORKDIR /app
RUN pip install flask
COPY auth.py .
EXPOSE 5000
CMD ["python3", "auth.py"]
EOF
```

---

### FASE 1: Crear instancia EC2 en AWS

1. Ve a **AWS Console → EC2 → Launch Instance**

2. Configura:
   - **Name:** `iot-monitoring-server`
   - **AMI:** Ubuntu Server 24.04 LTS (Free Tier eligible) ← usar esta
   - **Instance type:** `t2.micro` (gratis con Free Tier)
   - **Key pair:** crea uno nuevo → llámalo `iot-key` → descarga `iot-key.pem`

3. En **Network settings → Security Group**, agrega estas reglas de entrada:

   | Tipo       | Puerto | Origen    | Para qué                                   |
   | ---------- | ------ | --------- | ------------------------------------------ |
   | SSH        | 22     | My IP     | Conectarte                                 |
   | Custom TCP | 9000   | 0.0.0.0/0 | Servidor IoT + HTTP                        |
   | Custom TCP | 5000   | 0.0.0.0/0 | Auth service (opcional, solo para pruebas) |

4. Click **Launch Instance** y espera que aparezca "Running"

5. **Copia la IP pública** (la necesitarás para Route 53). Ejemplo: `54.123.45.67`

---

### FASE 2: Conectarse a EC2

```bash
# En tu máquina local
chmod 400 iot-key.pem

# Conectar
ssh -i "iot-key.pem" ubuntu@54.123.45.67
```

---

### FASE 3: Instalar dependencias en EC2

```bash
# Actualizar paquetes
sudo apt update && sudo apt upgrade -y

# Instalar Docker
sudo apt install -y docker.io docker-compose git

# Habilitar Docker
sudo systemctl enable docker
sudo systemctl start docker

# Agregar ubuntu al grupo docker
sudo usermod -aG docker ubuntu

# IMPORTANTE: cerrar sesión y reconectar para que aplique
exit
# Vuelve a conectar con SSH
```

---

### FASE 4: Subir el código al servidor

**Opción A — desde GitHub (si el repo es público o usas token):**

```bash
git clone https://github.com/Mejia1406/IOT-ServicesMonitoring.git
cd IOT-ServicesMonitoring
```

**Opción B — subir el ZIP con SCP (desde tu PC local):**

```bash
# En tu máquina local:
scp -i "iot-key.pem" IOT-ServicesMonitoring.zip ubuntu@54.123.45.67:~/

# En EC2:
unzip IOT-ServicesMonitoring.zip
cd IOT-ServicesMonitoring
```

---

### FASE 5: Crear docker-compose.yml

Crea este archivo en la raíz del proyecto en EC2:

```bash
cat > docker-compose.yml << 'EOF'
version: '3.8'

services:
  auth-service:
    build:
      context: ./auth-service
      dockerfile: Dockerfile
    container_name: iot-auth
    ports:
      - "5000:5000"
    restart: unless-stopped

  iot-server:
    build:
      context: .
      dockerfile: Dockerfile
    container_name: iot-server
    ports:
      - "9000:9000"
    environment:
      - AUTH_HOST=auth-service
      - AUTH_PORT=5000
    depends_on:
      - auth-service
    volumes:
      - ./server/logs:/app/logs
    restart: unless-stopped

EOF
```

---

### FASE 6: Construir y ejecutar los contenedores

```bash
# Construir ambas imágenes (en EC2, dentro del proyecto)
docker-compose build

# Verificar que se construyeron
docker images

# Lanzar los servicios
docker-compose up -d

# Verificar que están corriendo
docker-compose ps

# Ver logs del servidor
docker-compose logs -f iot-server

# Ver logs del auth
docker-compose logs -f iot-auth
```

**Verificación rápida:**

```bash
# Prueba el servidor HTTP integrado
curl http://localhost:9000/api/status
# Debe responder: {"sensors":0,"operators":0,"alerts":0}

# Prueba el auth service
curl -X POST http://localhost:5000/auth \
  -H "Content-Type: application/json" \
  -d '{"username":"sara","password":"1234"}'
# Debe responder: {"role":"operator","status":"ok"}
```

---

### FASE 7: Configurar DNS en AWS Route 53

1. Ve a **AWS Console → Route 53 → Hosted zones → Create hosted zone**

2. Configura:
   - **Domain name:** `iot-monitoring.ddns.net`
   - **Type:** Public hosted zone
   - Click **Create**

3. Dentro de la hosted zone, click **Create record:**
   - **Record name:** (deja vacío para el dominio raíz, o escribe `server`)
   - **Record type:** A
   - **Value:** `54.123.45.67` ← tu IP pública de EC2
   - **TTL:** 300
   - Click **Create**


   Puedes usar ese hostname en tus variables de entorno.

4. **Actualizar variables en los clientes:**

   Para el sensor (Python):

   ```bash
   export IOT_SERVER_HOST=iot-monitoring.ddns.net
   export IOT_SERVER_PORT=9000
   python3 main.py
   ```

   Para el operador (Java):

   ```bash
   export IOT_SERVER_HOST=iot-monitoring.ddns.net
   export IOT_SERVER_PORT=9000
   java OperatorClient
   ```

---

### FASE 8: Verificar acceso desde Internet

**Desde tu máquina local (fuera de AWS):**

```bash
# 1. Verificar resolución DNS
nslookup iot-monitoring.ddns.net
# Debe devolver: 54.123.45.67

# 2. Verificar interfaz web
curl http://iot-monitoring.ddns.net:9000/
# Debe devolver HTML del dashboard

# 3. Verificar API
curl http://iot-monitoring.ddns.net:9000/api/status

# 4. Probar conexión socket directamente
python3 -c "
import socket
s = socket.create_connection(('iot-monitoring.ddns.net', 9000))
s.send(b'STATUS\n')
print(s.recv(1024).decode())
s.close()
"
# Debe responder: STATUS|sensors=0|operators=0|alerts=0
```

---

### FASE 9: Conectar los clientes al servidor en la nube

**Desde tu máquina local:**

```bash
# Sensores Python
cd client-sensor
pip install -r requirements.txt  # (está vacío, no necesita nada extra)
IOT_SERVER_HOST=iot-monitoring.ddns.net IOT_SERVER_PORT=9000 python3 main.py

# Operador Java (en otra terminal)
cd client-operator
javac OperatorClient.java
IOT_SERVER_HOST=iot-monitoring.ddns.net IOT_SERVER_PORT=9000 java OperatorClient
```

---

### COMANDOS RÁPIDOS DE MANTENIMIENTO EN EC2

```bash
# Ver logs en vivo
docker-compose logs -f

# Reiniciar solo el servidor
docker-compose restart iot-server

# Entrar al contenedor del servidor
docker exec -it iot-server bash

# Ver el archivo de logs dentro del contenedor
docker exec iot-server cat /app/logs/server.log

# Detener todo
docker-compose down

# Actualizar código y reconstruir
git pull
docker-compose build
docker-compose up -d
```

---
