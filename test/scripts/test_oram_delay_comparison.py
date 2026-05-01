import csv
import importlib.util
import io
import subprocess
import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT_PATH = REPO_ROOT / "scripts" / "oram_delay_comparison.py"


def load_module():
    spec = importlib.util.spec_from_file_location("oram_delay_comparison", SCRIPT_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ORAMDelayComparisonTest(unittest.TestCase):
    def test_default_scenario_uses_ten_ring_eviction_windows(self):
        model = load_module()

        scenario = model.default_scenario()

        self.assertEqual(scenario.accesses, 480)

    def test_default_path_oram_4k_delay_matches_network_model(self):
        model = load_module()
        scenario = model.default_scenario()

        result = model.path_oram_delay(
            block_size_bytes=4 * 1024,
            accesses=scenario.accesses,
            network=scenario.network,
            config=scenario.path,
        )

        self.assertEqual(result.evictions, 0)
        self.assertAlmostEqual(result.total_seconds, 45.0653184)
        self.assertAlmostEqual(result.amortized_ms, 93.88608)

    def test_default_ring_oram_4k_delay_matches_path_batched_eviction_model(self):
        model = load_module()
        scenario = model.default_scenario()

        result = model.ring_oram_delay(
            block_size_bytes=4 * 1024,
            accesses=scenario.accesses,
            network=scenario.network,
            config=scenario.ring,
            eviction_rtt_mode="path",
        )

        self.assertEqual(result.evictions, 10)
        self.assertAlmostEqual(result.total_seconds, 14.7683392)
        self.assertAlmostEqual(result.amortized_ms, 30.767373333333335)

    def test_csv_cli_outputs_all_default_block_sizes_for_both_algorithms(self):
        completed = subprocess.run(
            [sys.executable, str(SCRIPT_PATH), "--format", "csv"],
            check=True,
            capture_output=True,
            text=True,
        )

        rows = list(csv.DictReader(io.StringIO(completed.stdout)))

        self.assertEqual(len(rows), 6)
        self.assertEqual(
            [(row["algorithm"], row["block_size_kib"]) for row in rows],
            [
                ("Path ORAM", "4"),
                ("Ring ORAM", "4"),
                ("Path ORAM", "8"),
                ("Ring ORAM", "8"),
                ("Path ORAM", "16"),
                ("Ring ORAM", "16"),
            ],
        )


if __name__ == "__main__":
    unittest.main()
