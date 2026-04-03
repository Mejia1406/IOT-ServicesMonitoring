FROM python:3.11-slim

WORKDIR /app

# Instalar Flask
RUN pip install --no-cache-dir flask

# Copiar el servicio de autenticación
COPY auth.py .

EXPOSE 5000

CMD ["python3", "auth.py"]
