from __future__ import annotations

import time
from dataclasses import dataclass
from datetime import datetime
from typing import Callable, Iterator

from .protocol import MAGIC, TYPE_ACK, TYPE_RSSI_REPORT, ProtocolError, build_send_command, parse_frame, parse_text_message


@dataclass(frozen=True)
class RawSerialEvent:
    direction: str
    data: bytes
    timestamp: float


class FrameReader:
    def __init__(self):
        self.buffer = bytearray()

    def feed(self, data: bytes):
        self.buffer.extend(data)
        frames = []
        while True:
            try:
                frame = self._pop_next_frame()
            except ProtocolError:
                # 跳过不支持的坏帧(magic 已被消费),继续解析后续帧，不丢弃整批
                continue
            if frame is None:
                break
            frames.append(frame)
        return frames

    def _pop_next_frame(self) -> bytes | None:
        magic_index = self.buffer.find(MAGIC)
        if magic_index < 0:
            self.buffer.clear()
            return None
        if magic_index > 0:
            del self.buffer[:magic_index]
        if len(self.buffer) < 4:
            return None

        frame_type = self.buffer[3]
        if frame_type == TYPE_ACK:
            frame_len = 8
        elif frame_type == TYPE_RSSI_REPORT:
            if len(self.buffer) < 6:
                return None
            frame_len = 6 + self.buffer[5] * 2
        else:
            del self.buffer[:2]
            raise ProtocolError(f"unsupported frame type: 0x{frame_type:02X}")

        if len(self.buffer) < frame_len:
            return None
        frame = bytes(self.buffer[:frame_len])
        del self.buffer[:frame_len]
        return frame


class MixedMessageReader:
    def __init__(self):
        self.frame_reader = FrameReader()
        self.line_buffer = bytearray()

    def feed(self, data: bytes):
        messages = []
        for frame in self.frame_reader.feed(data):
            try:
                messages.append(parse_frame(frame))
            except ProtocolError:
                continue  # 单帧解析失败不影响同批其它帧

        self.line_buffer.extend(data)
        while True:
            newline_index = self.line_buffer.find(b"\n")
            if newline_index < 0:
                break
            raw_line = bytes(self.line_buffer[: newline_index + 1])
            del self.line_buffer[: newline_index + 1]
            line = raw_line.decode("ascii", errors="ignore")
            try:
                message = parse_text_message(line)
            except ProtocolError:
                message = None  # 单行(如 count 不匹配)解析失败不影响同批其它行
            if message is not None:
                messages.append(message)
        if len(self.line_buffer) > 4096:
            del self.line_buffer[:-512]
        return messages


class SerialClient:
    def __init__(
        self,
        port: str,
        baud: int = 115200,
        timeout: float = 0.1,
        raw_callback: Callable[[RawSerialEvent], None] | None = None,
    ):
        try:
            import serial
        except ImportError as exc:
            raise RuntimeError("pyserial is required for serial access: pip install pyserial") from exc
        self.serial = serial.Serial(
            port=port,
            baudrate=baud,
            timeout=timeout,
            write_timeout=1,
            xonxoff=False,
            rtscts=False,
            dsrdtr=False,
        )
        # DTR/RTS toggle to reset CH340 dongle
        self.serial.dtr = False
        self.serial.rts = False
        # Wait for dongle to settle
        time.sleep(2.0)
        # Flush any pending data
        self.serial.read(self.serial.in_waiting or 8192)
        self.reader = MixedMessageReader()
        self.raw_callback = raw_callback

    def close(self) -> None:
        self.serial.close()

    def read_available(self, size: int = 4096) -> list[object]:
        chunk = self.serial.read(size)
        if not chunk:
            return []
        self._emit_raw("rx", chunk)
        return self.reader.feed(chunk)

    def iter_messages(self) -> Iterator[object]:
        while True:
            for message in self.read_available(256):
                yield message

    def write_command(self, command: str) -> None:
        data = command.encode("ascii")
        self.serial.write(data)
        self.serial.flush()
        self._emit_raw("tx", data)

    def send_rssi_req(self) -> str:
        command = "RSSI_REQ\r\n"
        self.write_command(command)
        return command

    def send(self, dst: int, path: list[int], payload: str) -> str:
        command = build_send_command(dst, path, payload)
        self.write_command(command)
        return command

    def _emit_raw(self, direction: str, data: bytes) -> None:
        if self.raw_callback is None:
            return
        self.raw_callback(RawSerialEvent(direction=direction, data=data, timestamp=datetime.now().timestamp()))
