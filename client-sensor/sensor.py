import random
import socket
import threading
import time
from datetime import UTC, datetime
from typing import Any

from config import (
    ENABLE_SERVER_RESPONSE_WAIT,
    HEARTBEAT_EVERY,
    RECONNECT_DELAY,
    SEND_INTERVAL_MAX,
    SEND_INTERVAL_MIN,
    SERVER_HOST,
    SERVER_PORT,
    SOCKET_TIMEOUT,
)
from generators import GENERATOR_BY_TYPE


def utc_now_iso() -> str:
    return datetime.now(UTC).strftime("%Y-%m-%dT%H:%M:%SZ")


class Sensor(threading.Thread):
    def __init__(
        self,
        sensor_id: str,
        sensor_type: str,
        location: str,
        profile: dict[str, Any],
    ):
        super().__init__(daemon=True)
        self.sensor_id = sensor_id
        self.sensor_type = sensor_type
        self.location = location
        self.profile = profile
        self.sock: socket.socket | None = None
        self.running = True
        self.sent_messages = 0

    def log(self, message: str) -> None:
        print(f"[{self.sensor_id}][{self.sensor_type}] {message}")

    def build_register_message(self) -> str:
        return f"REGISTER|{self.sensor_id}|{self.sensor_type}|{self.location}\n"

    def build_data_message(self) -> str:
        generator = GENERATOR_BY_TYPE[self.sensor_type]
        value, unit = generator(self.profile)
        timestamp = utc_now_iso()
        return f"DATA|{self.sensor_id}|{self.sensor_type}|{value}|{unit}|{timestamp}\n"

    def build_heartbeat_message(self) -> str:
        return f"HEARTBEAT|{self.sensor_id}|{utc_now_iso()}\n"

    def build_quit_message(self) -> str:
        return f"QUIT|{self.sensor_id}\n"

    def connect(self) -> None:
        self.log(f"Resolviendo {SERVER_HOST}:{SERVER_PORT}")
        self.sock = socket.create_connection(
            (SERVER_HOST, SERVER_PORT),
            timeout=SOCKET_TIMEOUT,
        )
        self.sock.settimeout(SOCKET_TIMEOUT)
        self.log("Conexión establecida")

    def close_socket(self) -> None:
        if self.sock is not None:
            try:
                self.sock.close()
            except OSError:
                pass
            finally:
                self.sock = None

    def send_line(self, line: str) -> None:
        if self.sock is None:
            raise ConnectionError("Socket no disponible")
        self.sock.sendall(line.encode("utf-8"))

    def receive_line(self) -> str:
        if self.sock is None:
            raise ConnectionError("Socket no disponible")

        buffer = b""
        while not buffer.endswith(b"\n"):
            chunk = self.sock.recv(1024)
            if not chunk:
                raise ConnectionError("El servidor cerró la conexión")
            buffer += chunk

        return buffer.decode("utf-8").strip()

    def register(self) -> None:
        request = self.build_register_message()
        self.log(f"-> {request.strip()}")
        self.send_line(request)

        if ENABLE_SERVER_RESPONSE_WAIT:
            response = self.receive_line()
            self.log(f"<- {response}")

    def send_measurement(self) -> None:
        request = self.build_data_message()
        self.log(f"-> {request.strip()}")
        self.send_line(request)
        self.sent_messages += 1

        if ENABLE_SERVER_RESPONSE_WAIT:
            try:
                response = self.receive_line()
                self.log(f"<- {response}")
            except socket.timeout:
                self.log("Sin respuesta inmediata del servidor para DATA")

    def send_heartbeat(self) -> None:
        request = self.build_heartbeat_message()
        self.log(f"-> {request.strip()}")
        self.send_line(request)

        if ENABLE_SERVER_RESPONSE_WAIT:
            try:
                response = self.receive_line()
                self.log(f"<- {response}")
            except socket.timeout:
                self.log("Sin respuesta inmediata del servidor para HEARTBEAT")

    def send_quit(self) -> None:
        try:
            request = self.build_quit_message()
            self.log(f"-> {request.strip()}")
            self.send_line(request)
        except Exception:
            pass

    def wait_next_interval(self) -> None:
        sensor_min = self.profile.get("interval_min", SEND_INTERVAL_MIN)
        sensor_max = self.profile.get("interval_max", SEND_INTERVAL_MAX)
        interval = random.uniform(sensor_min, sensor_max)
        time.sleep(interval)

    def run(self) -> None:
        while self.running:
            try:
                self.connect()
                self.register()

                while self.running:
                    self.send_measurement()

                    if self.sent_messages % HEARTBEAT_EVERY == 0:
                        self.send_heartbeat()

                    self.wait_next_interval()

            except socket.gaierror as error:
                self.log(f"Error de resolución DNS: {error}")
                time.sleep(RECONNECT_DELAY)

            except (ConnectionError, socket.timeout, OSError) as error:
                self.log(f"Error de conexión/red: {error}")
                time.sleep(RECONNECT_DELAY)

            except Exception as error:
                self.log(f"Error inesperado: {error}")
                time.sleep(RECONNECT_DELAY)

            finally:
                self.close_socket()

    def stop(self) -> None:
        self.running = False
        self.send_quit()
        self.close_socket()
