from __future__ import annotations

from dataclasses import asdict, dataclass


@dataclass(frozen=True)
class MeshDefaults:
    gateway: int = 0x00
    nodes: tuple[int, ...] = (0x01, 0x02, 0x03, 0x04)
    command_interval: float = 1.0
    ack_timeout: float = 2.0
    k_paths: int = 4
    demands: tuple[int, ...] = (8, 32, 64)
    capacity: float = 200.0
    bw_allocated: float = 0.0
    packet_loss: float = 0.0
    delay: float = 0.0
    queueing_delay: float = 0.0
    humidity: float = 50.0
    temperature: float = 25.0
    air_pressure: float = 101.325

    def to_dict(self) -> dict:
        data = asdict(self)
        data["nodes"] = list(self.nodes)
        data["demands"] = list(self.demands)
        return data


DEFAULTS = MeshDefaults()


def default_field(value, unit: str | None = None) -> dict:
    field = {"value": value, "source": "default"}
    if unit is not None:
        field["unit"] = unit
    return field


def real_field(value, source: str, unit: str | None = None) -> dict:
    field = {"value": value, "source": source}
    if unit is not None:
        field["unit"] = unit
    return field


def default_simulation_params(defaults: MeshDefaults = DEFAULTS) -> dict:
    return {
        "capacity": default_field(defaults.capacity, "capacity_unit"),
        "bw_allocated": default_field(defaults.bw_allocated, "capacity_unit"),
        "packet_loss": default_field(defaults.packet_loss, "ratio"),
        "delay": default_field(defaults.delay, "seconds"),
        "queueing_delay": default_field(defaults.queueing_delay, "seconds"),
        "humidity": default_field(defaults.humidity, "percent"),
        "temperature": default_field(defaults.temperature, "celsius"),
        "air_pressure": default_field(defaults.air_pressure, "kPa"),
    }
