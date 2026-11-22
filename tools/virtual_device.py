"""In-memory JPT serial device for Linux PTY integration tests."""

import argparse
import os
import struct
import tty


class VirtualDevice:
    writes = {
        0x01: (0x39, 0, 4095), 0x02: (0x3B, 1, 4000), 0x03: (0x3A, 1, 500),
        0x04: (0x32, 0, 1000), 0x05: (0x33, 1, 500), 0x06: (0x34, 1, 4000),
        0x07: (0x35, 0, 31), 0x08: (0x36, 0, 31), 0x17: (0x47, 0, 4095),
        0x18: (0x48, 0, 100), 0x19: (0x49, 0, 3),
    }
    flags = {0x09: 1, 0x0A: 2, 0x0B: 4}

    def __init__(self, fault=None):
        self.fault = fault
        self.address = bytes((192, 168, 0, 10))
        self.port = 5000
        self.buffer = bytearray()
        self.resync = False
        self.registers = {
            0x32: 0, 0x33: 20, 0x34: 100, 0x35: 0, 0x36: 0x1C,
            0x38: 0x18, 0x39: 0, 0x3A: 20, 0x3B: 100, 0x40: 0x10,
            0x47: 0, 0x48: 0, 0x49: 0, 0x28: 0,
        }
        self.blocks = {
            0x3C: bytes.fromhex("a6 04 c3 01 fd 00 00 00"),
            0x3F: b"AIAJP0248999",
            0x42: struct.pack("<8H", 0, 0, 0, 0, 1, 0, 0, 0),
            0x3D: b"SIM-MCU1", 0x3E: b"SIM-FPGA", 0x9C: b"VIRTUAL-M7",
            0x46: bytes.fromhex("78 00 00 0a 00 00"),
        }

    def reply(self, command, value):
        if command in self.writes:
            register, minimum, maximum = self.writes[command]
            if not minimum <= value <= maximum:
                return None
            self.registers[register] = (value * 100 + 2047) // 4095 if command == 0x01 else value
        elif command in self.flags:
            if value not in (0, 1):
                return None
            if command == 0x0B and value == 0:
                if self.fault == "reject-stop":
                    return None
                if self.fault == "bad-ack":
                    return struct.pack("<H", 1)
            mask = self.flags[command]
            self.registers[0x38] = (self.registers[0x38] & ~mask) | (value * mask)
        elif command == 0x6F and value:
            self.port = value
        elif value != 0:
            return None
        elif command in self.registers:
            value = self.registers[command]
            if command == 0x38 and self.fault == "stuck-status":
                value |= 4
        else:
            return self.blocks.get(command)
        return struct.pack("<H", value)

    def feed(self, data):
        self.buffer.extend(data)
        output = bytearray()
        while self.buffer:
            if self.resync or self.buffer[0] != 0x24:
                next_start = self.buffer.find(b"$")
                if not self.resync:
                    output.append(0x45)
                self.resync = next_start < 0
                del self.buffer[:next_start if next_start >= 0 else len(self.buffer)]
                if self.resync:
                    break
            if len(self.buffer) < 4:
                break
            command, length = struct.unpack_from("<HB", self.buffer, 1)
            if length != (4 if command == 0x6E else 2):
                del self.buffer[:4]
                self.resync = True
                output.append(0x45)
                continue
            end = 4 + length
            if len(self.buffer) <= end:
                break
            if self.buffer[end] != 0x2A:
                next_start = self.buffer.find(b"$", 1)
                del self.buffer[:next_start if next_start >= 0 else end + 1]
                output.append(0x45)
                continue
            if command == 0x6E:
                self.address = bytes(self.buffer[4:end])
                payload = self.address
            else:
                payload = self.reply(command, struct.unpack_from("<H", self.buffer, 4)[0])
            del self.buffer[:end + 1]
            if payload is None:
                output.append(0x45)
            else:
                output.extend(b"$" + struct.pack("<HB", command, len(payload)) + payload + b"*")
        return bytes(output)


def serve(device):
    master, slave = os.openpty()
    try:
        tty.setraw(slave)
        print(os.ttyname(slave), flush=True)
        while True:
            chunk = os.read(master, 4096)
            if not chunk:
                break
            remaining = memoryview(device.feed(chunk))
            while remaining:
                remaining = remaining[os.write(master, remaining):]
    finally:
        os.close(master)
        os.close(slave)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fault", choices=("reject-stop", "bad-ack", "stuck-status"))
    try:
        serve(VirtualDevice(parser.parse_args().fault))
    except KeyboardInterrupt:
        pass
