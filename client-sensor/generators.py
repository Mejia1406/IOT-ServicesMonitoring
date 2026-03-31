import random
from typing import Any

from config import ANOMALY_MODE, ANOMALY_PROBABILITY


def should_trigger_anomaly(profile: dict[str, Any]) -> bool:
    if not ANOMALY_MODE:
        return False
    probability = profile.get("anomaly_probability", ANOMALY_PROBABILITY)
    return random.random() < probability


def generate_temperature(profile: dict[str, Any]) -> tuple[float, str]:
    normal_min = profile.get("normal_min", 20.0)
    normal_max = profile.get("normal_max", 30.0)
    anomaly_min = profile.get("anomaly_min", 70.0)
    anomaly_max = profile.get("anomaly_max", 95.0)

    if should_trigger_anomaly(profile):
        value = random.uniform(anomaly_min, anomaly_max)
    else:
        value = random.uniform(normal_min, normal_max)

    return round(value, 2), "C"


def generate_vibration(profile: dict[str, Any]) -> tuple[float, str]:
    normal_min = profile.get("normal_min", 0.1)
    normal_max = profile.get("normal_max", 5.0)
    anomaly_min = profile.get("anomaly_min", 8.0)
    anomaly_max = profile.get("anomaly_max", 15.0)

    if should_trigger_anomaly(profile):
        value = random.uniform(anomaly_min, anomaly_max)
    else:
        value = random.uniform(normal_min, normal_max)

    return round(value, 2), "Hz"


def generate_energy(profile: dict[str, Any]) -> tuple[float, str]:
    normal_min = profile.get("normal_min", 120.0)
    normal_max = profile.get("normal_max", 600.0)
    anomaly_min = profile.get("anomaly_min", 850.0)
    anomaly_max = profile.get("anomaly_max", 1400.0)

    if should_trigger_anomaly(profile):
        value = random.uniform(anomaly_min, anomaly_max)
    else:
        value = random.uniform(normal_min, normal_max)

    return round(value, 2), "W"


def generate_humidity(profile: dict[str, Any]) -> tuple[float, str]:
    normal_min = profile.get("normal_min", 35.0)
    normal_max = profile.get("normal_max", 65.0)
    anomaly_min = profile.get("anomaly_min", 80.0)
    anomaly_max = profile.get("anomaly_max", 95.0)

    if should_trigger_anomaly(profile):
        value = random.uniform(anomaly_min, anomaly_max)
    else:
        value = random.uniform(normal_min, normal_max)

    return round(value, 2), "%"


def generate_operational(profile: dict[str, Any]) -> tuple[str, str]:
    normal_states = profile.get("normal_states", ["OK", "OK", "OK", "WARNING"])
    anomaly_states = profile.get("anomaly_states", ["FAIL", "CRITICAL"])

    if should_trigger_anomaly(profile):
        value = random.choice(anomaly_states)
    else:
        value = random.choice(normal_states)

    return value, "state"


GENERATOR_BY_TYPE = {
    "temperature": generate_temperature,
    "vibration": generate_vibration,
    "energy": generate_energy,
    "humidity": generate_humidity,
    "operational": generate_operational,
}
