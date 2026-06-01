#!/usr/bin/env python3
"""Host-side helper for the STM32 UART binary protocol.

Frame format:
    AA 55 SEQ LEN CMD DATA CHECKSUM

Checksum:
    low 8 bits of SEQ + LEN + CMD + all DATA bytes.

The host owns SEQ generation. Retries reuse the same SEQ. A response is accepted
only when its SEQ matches the pending request.
"""

from __future__ import annotations

import argparse
import sys
import time
from dataclasses import dataclass


HEAD_1 = 0xAA
HEAD_2 = 0x55
MAX_DATA_LEN = 16

CMD_PING = 0x01
CMD_LED = 0x02
CMD_STATUS = 0x03
CMD_BUZZER = 0x04

CMD_ACK = 0x80
CMD_STATUS_RSP = 0x83

ERR_OK = 0x00


@dataclass
class Frame:
    seq: int
    cmd: int
    data: bytes


class ProtocolError(Exception):
    pass


def calc_checksum(seq: int, cmd: int, data: bytes) -> int:
    return (seq + len(data) + cmd + sum(data)) & 0xFF


def build_frame(seq: int, cmd: int, data: bytes = b"") -> bytes:
    if len(data) > MAX_DATA_LEN:
        raise ValueError("data too long")

    check = calc_checksum(seq, cmd, data)
    return bytes([HEAD_1, HEAD_2, seq, len(data), cmd]) + data + bytes([check])


def read_exact(port, size: int, deadline: float) -> bytes:
    buf = bytearray()

    while len(buf) < size:
        if time.monotonic() >= deadline:
            raise TimeoutError("read timeout")

        chunk = port.read(size - len(buf))
        if chunk:
            buf.extend(chunk)

    return bytes(buf)


def read_frame(port, timeout_s: float) -> Frame:
    deadline = time.monotonic() + timeout_s

    while True:
        b = read_exact(port, 1, deadline)[0]
        if b != HEAD_1:
            continue

        b = read_exact(port, 1, deadline)[0]
        if b == HEAD_2:
            break

        if b == HEAD_1:
            continue

    header = read_exact(port, 3, deadline)
    seq = header[0]
    length = header[1]
    cmd = header[2]

    if length > MAX_DATA_LEN:
        raise ProtocolError(f"invalid length: {length}")

    data = read_exact(port, length, deadline)
    check = read_exact(port, 1, deadline)[0]
    expected = calc_checksum(seq, cmd, data)

    if check != expected:
        raise ProtocolError(f"bad checksum: got 0x{check:02X}, expected 0x{expected:02X}")

    return Frame(seq=seq, cmd=cmd, data=data)


class UartProtocolHost:
    def __init__(self, port, timeout_s: float, retries: int) -> None:
        self.port = port
        self.timeout_s = timeout_s
        self.retries = retries
        self.next_seq = 0

    def alloc_seq(self) -> int:
        seq = self.next_seq
        self.next_seq = (self.next_seq + 1) & 0xFF
        return seq

    def transact(self, cmd: int, data: bytes = b"") -> Frame:
        seq = self.alloc_seq()
        packet = build_frame(seq, cmd, data)

        for attempt in range(self.retries + 1):
            self.port.write(packet)
            self.port.flush()

            try:
                while True:
                    rsp = read_frame(self.port, self.timeout_s)
                    if rsp.seq != seq:
                        print(
                            f"ignore response with wrong SEQ: got 0x{rsp.seq:02X}, "
                            f"want 0x{seq:02X}",
                            file=sys.stderr,
                        )
                        continue

                    return rsp
            except TimeoutError:
                if attempt >= self.retries:
                    raise

                print(f"timeout, retry {attempt + 1}/{self.retries}", file=sys.stderr)

        raise TimeoutError("unreachable retry state")


def expect_ack(frame: Frame, origin_cmd: int) -> int:
    if frame.cmd != CMD_ACK:
        raise ProtocolError(f"expected ACK 0x80, got 0x{frame.cmd:02X}")

    if len(frame.data) != 2:
        raise ProtocolError(f"bad ACK data length: {len(frame.data)}")

    if frame.data[0] != origin_cmd:
        raise ProtocolError(
            f"ACK origin cmd mismatch: got 0x{frame.data[0]:02X}, want 0x{origin_cmd:02X}"
        )

    return frame.data[1]


def print_ack_status(status: int) -> None:
    if status == ERR_OK:
        print("ACK OK")
    else:
        print(f"ACK error: 0x{status:02X}")


def main() -> int:
    parser = argparse.ArgumentParser(description="STM32 UART binary protocol host")
    parser.add_argument("port", help="serial port, for example COM3")
    parser.add_argument("command", choices=["ping", "led", "status", "buzzer"])
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=0.2, help="response timeout in seconds")
    parser.add_argument("--retries", type=int, default=2)
    parser.add_argument("--value", type=int, default=0, help="command value for led or buzzer")
    args = parser.parse_args()

    try:
        import serial
    except ImportError:
        print("pyserial is required: pip install pyserial", file=sys.stderr)
        return 2

    with serial.Serial(args.port, args.baud, timeout=0.01) as port:
        host = UartProtocolHost(port, timeout_s=args.timeout, retries=args.retries)

        if args.command == "ping":
            rsp = host.transact(CMD_PING)
            print_ack_status(expect_ack(rsp, CMD_PING))
        elif args.command == "led":
            rsp = host.transact(CMD_LED, bytes([args.value & 0xFF]))
            print_ack_status(expect_ack(rsp, CMD_LED))
        elif args.command == "buzzer":
            rsp = host.transact(CMD_BUZZER, bytes([args.value & 0xFF]))
            print_ack_status(expect_ack(rsp, CMD_BUZZER))
        elif args.command == "status":
            rsp = host.transact(CMD_STATUS)
            print_ack_status(expect_ack(rsp, CMD_STATUS))

            status_rsp = read_frame(port, args.timeout)
            if status_rsp.seq != rsp.seq:
                raise ProtocolError(
                    f"status response SEQ mismatch: got 0x{status_rsp.seq:02X}, want 0x{rsp.seq:02X}"
                )
            if status_rsp.cmd != CMD_STATUS_RSP:
                raise ProtocolError(f"expected status response 0x83, got 0x{status_rsp.cmd:02X}")

            print("STATUS:", status_rsp.data.hex(" ").upper())

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (TimeoutError, ProtocolError, OSError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
