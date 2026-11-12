"""Exercise real replay decoding across nested inventory protocol versions."""
import importlib.util
from pathlib import Path
import struct
import io
from unittest.mock import patch
import unittest

spec = importlib.util.spec_from_file_location("replay", Path(__file__).parents[1] / "replay.py")
replay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(replay)


def i32(*values):
    return struct.pack("<" + "i" * len(values), *values)


def inventory(version, net_id, children=b"\0\0"):
    row = i32(net_id, 42, 1, 0)
    if version >= 3:
        row += i32(7, 29)
    if version >= 6:
        row += i32(4)
    if version >= 5:
        row += children
    return struct.pack("<H", 1) + row


class ReplayWireTest(unittest.TestCase):
    def decode(self, version, contents):
        mask = replay.D_INVENTORY | replay.D_FRAME | replay.D_LIGHT | replay.D_PID
        if version >= 6:
            mask |= 1 << 12
        body = i32(1) + struct.pack("<H", mask) + contents + i32(3, 5, 9000, 42)
        if version >= 6:
            body += i32(4, 17, 29)
        event = struct.pack("<BBH", replay.E_OBJECT_DELTA, 1, len(body)) + body
        header = b"F2NS" + struct.pack("<H", version) + i32(9)
        frame = struct.pack("<IIIH", 0, 0, len(event), 1)
        if version >= 4:
            frame += struct.pack("<I", 0)
        with patch("builtins.open", return_value=io.BytesIO(header + frame + event)):
            return replay.reconstruct_binary("fixture.net")[0][1]

    def test_v6_nested_inventory_does_not_displace_scalar_tail(self):
        obj = self.decode(6, inventory(6, 10, inventory(6, 11)))
        self.assertEqual(obj.inv, [(10, 42, 1, 0)])
        self.assertEqual((obj.frame, obj.light_distance, obj.light_intensity, obj.pid), (3, 5, 9000, 42))
        self.assertEqual((obj.instance_flags, obj.instance_value, obj.instance_aux), (4, 17, 29))

    def test_v5_nested_inventory_remains_readable(self):
        obj = self.decode(5, inventory(5, 10, inventory(5, 11)))
        self.assertEqual((obj.frame, obj.pid), (3, 42))

    def test_v4_flat_inventory_remains_readable(self):
        obj = self.decode(4, inventory(4, 10))
        self.assertEqual((obj.frame, obj.pid), (3, 42))

    def test_empty_v6_inventory_preserves_tail(self):
        obj = self.decode(6, b"\0\0")
        self.assertEqual(obj.inv, [])
        self.assertEqual(obj.instance_value, 17)


if __name__ == "__main__":
    unittest.main()
