"""Check repeated occupancy events retain their order within each message key."""
from pathlib import Path
import sys
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/octoviz"))
from occupancy import _pairs


class PairingTest(unittest.TestCase):
    def test_interleaved_repeated_messages(self):
        events = np.array([
            (7, 1, 1), (8, 1, 1), (7, 1, 0), (8, 1, 0),
            (7, 1, 1), (7, 2, 1), (7, 1, 0), (7, 2, 0),
            (7, 1, 1), (8, 1, 1), (7, 1, 0), (8, 1, 0),
            (9, 1, 1), (10, 1, 0),
        ], dtype=[("msg_id", "i8"), ("kind", "i8"), ("enter", "?")])
        starts, ends, pending, orphan_count = _pairs(
            events, events["enter"], ~events["enter"], ["msg_id", "kind"])
        self.assertEqual(sorted(zip(starts.tolist(), ends.tolist())),
                         [(0, 2), (1, 3), (4, 6), (5, 7), (8, 10), (9, 11)])
        self.assertEqual(pending.tolist(), [12])
        self.assertEqual(orphan_count, 1)


if __name__ == "__main__":
    unittest.main()
