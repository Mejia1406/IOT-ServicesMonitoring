import signal
import sys
import time

from sensor import Sensor


def build_sensors() -> list[Sensor]:
    return [
        Sensor(
            sensor_id="sensor-01",
            sensor_type="temperature",
            location="plant-a",
            profile={
                "normal_min": 20.0,
                "normal_max": 29.0,
                "anomaly_min": 75.0,
                "anomaly_max": 95.0,
                "anomaly_probability": 0.08,
                "interval_min": 1.5,
                "interval_max": 3.0,
            },
        ),
        Sensor(
            sensor_id="sensor-02",
            sensor_type="temperature",
            location="boiler-room",
            profile={
                "normal_min": 45.0,
                "normal_max": 68.0,
                "anomaly_min": 85.0,
                "anomaly_max": 110.0,
                "anomaly_probability": 0.15,
                "interval_min": 1.0,
                "interval_max": 2.5,
            },
        ),
        Sensor(
            sensor_id="sensor-03",
            sensor_type="vibration",
            location="motor-room",
            profile={
                "normal_min": 0.2,
                "normal_max": 4.5,
                "anomaly_min": 9.0,
                "anomaly_max": 17.0,
                "anomaly_probability": 0.10,
                "interval_min": 1.0,
                "interval_max": 2.5,
            },
        ),
        Sensor(
            sensor_id="sensor-04",
            sensor_type="energy",
            location="substation",
            profile={
                "normal_min": 250.0,
                "normal_max": 650.0,
                "anomaly_min": 900.0,
                "anomaly_max": 1500.0,
                "anomaly_probability": 0.14,
                "interval_min": 2.0,
                "interval_max": 4.0,
            },
        ),
        Sensor(
            sensor_id="sensor-05",
            sensor_type="humidity",
            location="warehouse",
            profile={
                "normal_min": 35.0,
                "normal_max": 60.0,
                "anomaly_min": 80.0,
                "anomaly_max": 96.0,
                "anomaly_probability": 0.09,
                "interval_min": 2.0,
                "interval_max": 4.5,
            },
        ),
        Sensor(
            sensor_id="sensor-06",
            sensor_type="operational",
            location="assembly-line",
            profile={
                "normal_states": ["OK", "OK", "OK", "WARNING"],
                "anomaly_states": ["FAIL", "CRITICAL"],
                "anomaly_probability": 0.10,
                "interval_min": 3.0,
                "interval_max": 5.0,
            },
        ),
    ]


def main() -> None:
    sensors = build_sensors()

    def shutdown_handler(signum, frame):
        print("\nCerrando simulador de sensores...")
        for sensor in sensors:
            sensor.stop()
        time.sleep(1)
        sys.exit(0)

    signal.signal(signal.SIGINT, shutdown_handler)
    signal.signal(signal.SIGTERM, shutdown_handler)

    print("Iniciando simulador de sensores IoT...")
    for sensor in sensors:
        sensor.start()

    while True:
        time.sleep(1)


if __name__ == "__main__":
    main()
