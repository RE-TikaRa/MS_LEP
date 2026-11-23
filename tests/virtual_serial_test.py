from pathlib import Path
import select
import subprocess
import sys
import tempfile
import time
import unittest


program = str(Path(__file__).resolve().parents[1] / "tools" / "virtual_device.py")
client = sys.argv.pop(1)


class SerialFixture:
    def __init__(self, fault=None):
        self.args = [sys.executable, program]
        if fault == "empty-cache":
            self.args.append("--empty-alarm-cache")
        elif fault:
            self.args.extend(["--fault", fault])

    def __enter__(self):
        self.process = subprocess.Popen(self.args, stdout=subprocess.PIPE)
        try:
            if not select.select([self.process.stdout], [], [], 5)[0]:
                raise RuntimeError("virtual device did not start")
            path = self.process.stdout.readline().decode("ascii").strip()
            if not path.startswith("/dev/pts/"):
                raise RuntimeError("virtual device did not publish a PTY")
            self.path = path
            return self
        except BaseException:
            self.close()
            raise

    def close(self):
        self.process.terminate()
        self.process.wait(timeout=5)
        self.process.stdout.close()

    def __exit__(self, exc_type, exc_value, trace):
        self.close()


def receive(port, count):
    data = bytearray()
    deadline = time.monotonic() + 3
    while len(data) < count:
        if not select.select([port], [], [], max(0, deadline - time.monotonic()))[0]:
            raise TimeoutError("incomplete serial reply: " + data.hex())
        block = port.read(count - len(data))
        if not block:
            raise EOFError("virtual port closed")
        data.extend(block)
    return bytes(data)


class VirtualSerialTests(unittest.TestCase):
    def test_manual_examples(self):
        examples = [
            ("24 02 03 02 00 00 2A", "45"),
            ("24 36 00 02 00 00 2A", "24 36 00 02 1C 00 2A"),
            ("24 3F 00 02 00 00 2A", "24 3F 00 0C 41 49 41 4A 50 30 32 34 38 39 39 39 2A"),
            ("24 38 00 02 00 00 2A", "24 38 00 02 18 00 2A"),
            ("24 3C 00 02 00 00 2A", "24 3C 00 08 A6 04 C3 01 FD 00 00 00 2A"),
            ("24 40 00 02 00 00 2A", "24 40 00 02 10 00 2A"),
            ("24 42 00 02 00 00 2A", "24 42 00 10 00 00 00 00 00 00 00 00 01 00 00 00 00 00 00 00 2A"),
            ("24 01 00 02 FF 07 2A", "24 01 00 02 FF 07 2A"),
            ("24 39 00 02 00 00 2A", "24 39 00 02 32 00 2A"),
            ("24 08 00 02 10 00 2A", "24 08 00 02 10 00 2A"),
            ("24 36 00 02 00 00 2A", "24 36 00 02 10 00 2A"),
            ("24 07 00 02 08 00 2A", "24 07 00 02 08 00 2A"),
            ("24 07 00 02 3F 00 2A", "24 07 00 02 3F 00 2A"),
            ("24 35 00 02 00 00 2A", "24 35 00 02 3F 00 2A"),
            ("24 19 00 02 01 00 2A", "24 19 00 02 01 00 2A"),
        ]
        with SerialFixture() as device, open(device.path, "r+b", buffering=0) as port:
            for request, answer in examples:
                with self.subTest(request=request):
                    port.write(bytes.fromhex(request))
                    expected = bytes.fromhex(answer)
                    self.assertEqual(receive(port, len(expected)), expected)

    def test_stream(self):
        with SerialFixture() as device, open(device.path, "r+b", buffering=0) as port:
            port.write(bytes.fromhex("24 02 00 02 2a"))
            self.assertFalse(select.select([port], [], [], 0.05)[0])
            port.write(bytes.fromhex("00 2a 24 3b 00 02 00 00 2a"))
            self.assertEqual(receive(port, 14), bytes.fromhex("24 02 00 02 2a 00 2a 24 3b 00 02 2a 00 2a"))
            port.write(bytes.fromhex("24 02 00 02 00 00 2a 24 3b 00 01 00 2a 24 3b 00 02 00 00 00"))
            self.assertEqual(receive(port, 3), b"EEE")
            port.write(bytes.fromhex("24 3b 00 02 00 00 2a"))
            self.assertEqual(receive(port, 7), bytes.fromhex("24 3b 00 02 2a 00 2a"))
            port.write(bytes.fromhex("24 3b 00 ff"))
            self.assertEqual(receive(port, 1), b"E")
            port.write(bytes.fromhex("00 00 2a 24 3b 00 02 00 00 2a"))
            self.assertEqual(receive(port, 7), bytes.fromhex("24 3b 00 02 2a 00 2a"))

    def test_core_client(self):
        for scenario in ("normal", "empty-cache", "reject-stop", "bad-ack", "stuck-status", "history-error"):
            fault = None if scenario in ("normal", "history-error") else scenario
            with self.subTest(scenario=scenario), SerialFixture(fault) as device, tempfile.TemporaryDirectory() as folder:
                journal = Path(folder) / "alarms"
                if scenario == "history-error":
                    journal.touch()
                subprocess.run([client, device.path, scenario, str(journal)], check=True, timeout=15)


unittest.main()
