#!/usr/bin/env python3
"""
Host-side helper for the HMI comm protocol (UART backend).

Frame : 0xA5 | len(1) | id(1) | payload(len) | crc16-ccitt(2, LE)
CRC   : non-reflected CCITT, init 0xFFFF, poly 0x1021, over len+id+payload

Examples
--------
  python3 scripts/uart_host.py --port /dev/ttyUSB0                 # listen (telemetry)
  python3 scripts/uart_host.py --port /dev/ttyUSB0 --ping
  python3 scripts/uart_host.py --port /dev/ttyUSB0 --info
  python3 scripts/uart_host.py --port /dev/ttyUSB0 --set-gauge 2000

Requires pyserial:  pip install pyserial
"""

import argparse
import struct
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial is required: pip install pyserial")

SYNC = 0xA5
MAX_PAYLOAD = 64

MSG_PING = 0x01
MSG_GET_INFO = 0x02
MSG_SET_GAUGE = 0x03
MSG_PONG = 0x81
MSG_INFO = 0x82
MSG_ACK = 0x83
MSG_TELEMETRY = 0x80


def crc16(data, crc=0xFFFF):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def encode_frame(msg_id, payload):
    frame = bytearray([SYNC, len(payload), msg_id])
    frame += payload
    c = crc16(bytes(frame[1:]))
    frame += bytes((c & 0xFF, c >> 8))
    return bytes(frame)


class Parser:
    """Incremental frame parser mirroring the device-side state machine."""

    ST_SYNC, ST_LEN, ST_BODY, ST_CRC = range(4)

    def __init__(self, on_frame):
        self.on_frame = on_frame
        self.reset()

    def reset(self):
        self.state = self.ST_SYNC
        self.data = bytearray()
        self.pos = 0
        self.length = 0
        self.crc = 0xFFFF
        self.ncrc = 0
        self.crc_rx = 0

    def feed(self, b):
        if self.state == self.ST_SYNC:
            if b == SYNC:
                self.state = self.ST_LEN
        elif self.state == self.ST_LEN:
            if b > MAX_PAYLOAD:
                self.reset()
                return
            self.length = b
            self.data = bytearray([b])
            self.crc = crc16(bytes([b]), 0xFFFF)
            self.pos = 0
            self.state = self.ST_BODY
        elif self.state == self.ST_BODY:
            self.data.append(b)
            self.crc = crc16(bytes([b]), self.crc)
            self.pos += 1
            if self.pos >= self.length + 1:  # id + payload
                self.ncrc = 0
                self.crc_rx = 0
                self.state = self.ST_CRC
        elif self.state == self.ST_CRC:
            if self.ncrc == 0:
                self.crc_rx = b
            else:
                self.crc_rx |= b << 8
            self.ncrc += 1
            if self.ncrc == 2:
                if self.crc_rx == self.crc:
                    self.on_frame(self.data[1], bytes(self.data[2:]))
                else:
                    print(f"  [crc mismatch rx=0x{self.crc_rx:04x} want=0x{self.crc:04x}]")
                self.reset()
        return


def describe(msg_id, payload):
    if msg_id == MSG_TELEMETRY:
        if len(payload) == 9:  # u8 idx | u32 uptime_ms | u32 value
            idx, uptime, value = (
                payload[0],
                struct.unpack("<I", payload[1:5])[0],
                struct.unpack("<I", payload[5:9])[0],
            )
            return f"TELEMETRY gauge[{idx}] value={value} uptime={uptime}ms"
        return f"TELEMETRY raw {payload.hex()}"
    if msg_id == MSG_PONG:
        return f"PONG echo={payload.hex()}"
    if msg_id == MSG_INFO:
        ver = f"{payload[0]}.{payload[1]}" if len(payload) >= 2 else "?"
        name = payload[2:].decode("utf-8", "replace") if len(payload) > 2 else ""
        return f"INFO version={ver} name={name!r}"
    if msg_id == MSG_ACK:
        if len(payload) == 2:
            status = "ok" if payload[1] == 0 else f"error({payload[1]})"
            return f"ACK for_msg=0x{payload[0]:02x} status={status}"
        return f"ACK raw {payload.hex()}"
    return f"msg 0x{msg_id:02x} {payload.hex()}"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True, help="serial port, e.g. /dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--ping", action="store_true", help="send PING and wait for PONG")
    ap.add_argument("--info", action="store_true", help="request INFO")
    ap.add_argument("--set-gauge", type=int, metavar="VALUE", help="set gauge value")
    ap.add_argument("--timeout", type=float, default=15.0, help="listen timeout in s")
    args = ap.parse_args()

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.05)
    except serial.SerialException as exc:
        sys.exit(f"cannot open {args.port}: {exc}")
    ser.flushInput()

    framed_response = {}

    def on_frame(msg_id, payload):
        print(f"<- {describe(msg_id, payload)}")

    parser = Parser(on_frame)

    request_mutation = {"done": False}

    deadline = time.monotonic() + args.timeout
    if args.ping or args.info or args.set_gauge is not None:
        if args.ping:
            ser.write(encode_frame(MSG_PING, b"\x01"))
            expected = MSG_PONG
        elif args.info:
            ser.write(encode_frame(MSG_GET_INFO, b""))
            expected = MSG_INFO
        else:
            payload = bytes([0]) + struct.pack("<i", args.set_gauge)
            ser.write(encode_frame(MSG_SET_GAUGE, payload))
            expected = MSG_ACK

        print(f"-> {describe_expected(expected)}")

        def on_frame_req(msg_id, payload):
            print(f"<- {describe(msg_id, payload)}")
            if msg_id == expected:
                request_mutation["done"] = True

        parser.on_frame = on_frame_req
        while time.monotonic() < deadline and not request_mutation["done"]:
            b = ser.read(1)
            if b:
                parser.feed(b[0])
        if not request_mutation["done"]:
            sys.exit(f"no response to {describe_expected(expected)} within {args.timeout}s")
        return 0

    print(f"listening on {args.port} (@{args.baud}) for {args.timeout}s ...")
    while time.monotonic() < deadline:
        b = ser.read(1)
        if b:
            parser.feed(b[0])
    print("done")
    return 0


def describe_expected(msg_id):
    return {MSG_PONG: "PING -> PONG", MSG_INFO: "GET_INFO -> INFO",
            MSG_ACK: "SET_GAUGE -> ACK"}[msg_id]


if __name__ == "__main__":
    sys.exit(main())