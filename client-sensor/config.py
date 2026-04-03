import os


SERVER_HOST = os.getenv("IOT_SERVER_HOST", "localhost")
SERVER_PORT = int(os.getenv("IOT_SERVER_PORT", "9000"))

RECONNECT_DELAY = float(os.getenv("RECONNECT_DELAY", "3"))
SOCKET_TIMEOUT = float(os.getenv("SOCKET_TIMEOUT", "8"))

SEND_INTERVAL_MIN = float(os.getenv("SEND_INTERVAL_MIN", "1.5"))
SEND_INTERVAL_MAX = float(os.getenv("SEND_INTERVAL_MAX", "4.0"))

HEARTBEAT_EVERY = int(os.getenv("HEARTBEAT_EVERY", "5"))

ANOMALY_MODE = os.getenv("ANOMALY_MODE", "true").lower() in (
    "true", "1", "yes", "on")
ANOMALY_PROBABILITY = float(os.getenv("ANOMALY_PROBABILITY", "0.12"))

ENABLE_SERVER_RESPONSE_WAIT = os.getenv("ENABLE_SERVER_RESPONSE_WAIT", "true").lower() in (
    "true",
    "1",
    "yes",
    "on",
)
