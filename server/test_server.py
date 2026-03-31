import socket
import threading
import logging
import os

logging.basicConfig(level=logging.INFO, format='%(asctime)s - SERVER - %(levelname)s - %(message)s')

def client_handler(conn, addr):
    logging.info(f"Nueva conexión desde {addr}")
    try:
        buffer = ""
        while True:
            data = conn.recv(1024)
            if not data:
                break
            
            buffer += data.decode("utf-8")
            
            while "\n" in buffer:
                line, buffer = buffer.split("\n", 1)
                line = line.strip()
                if line:
                    logging.info(f"Recibido de {addr}: {line}")
                    
                    # Respuesta simulada al cliente dependiendo del tipo de mensaje
                    if line.startswith("REGISTER"):
                        conn.sendall(b"OK REGISTERED\n")
                    elif line.startswith("DATA"):
                        conn.sendall(b"OK DATA\n")
                    elif line.startswith("HEARTBEAT"):
                        conn.sendall(b"OK HEARTBEAT\n")
                    elif line.startswith("QUIT"):
                        conn.sendall(b"OK BYE\n")
                        return
                    else:
                        conn.sendall(b"OK\n")
    except Exception as e:
        logging.error(f"Error gestionando cliente {addr}: {e}")
    finally:
        conn.close()
        logging.info(f"Conexión cerrada para {addr}")


def start_server():
    host = os.getenv("TEST_SERVER_HOST", "0.0.0.0")
    port = int(os.getenv("TEST_SERVER_PORT", "9000"))

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    
    try:
        server.bind((host, port))
        server.listen(10)
        logging.info(f"Servidor de prueba escuchando en {host}:{port}")
        
        while True:
            conn, addr = server.accept()
            thread = threading.Thread(target=client_handler, args=(conn, addr))
            thread.daemon = True
            thread.start()
    except KeyboardInterrupt:
        logging.info("Apagando servidor de prueba...")
    except Exception as e:
        logging.error(f"Error en el servidor: {e}")
    finally:
        server.close()

if __name__ == "__main__":
    start_server()
