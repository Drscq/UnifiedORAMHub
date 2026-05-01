import importlib.util
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT_PATH = REPO_ROOT / "scripts" / "oram_tcp_benchmark.py"


def load_module():
    spec = importlib.util.spec_from_file_location("oram_tcp_benchmark", SCRIPT_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ORAMTCPBenchmarkTest(unittest.TestCase):
    def test_path_payload_plan_matches_requested_protocol(self):
        benchmark = load_module()

        plan = benchmark.path_payload_plan(block_size_bytes=4 * 1024, levels=16, z=4)

        self.assertEqual(plan.read_bytes, 16 * 4 * 4 * 1024)
        self.assertEqual(plan.write_bytes, 16 * 4 * 4 * 1024)

    def test_ring_payload_plan_matches_requested_protocol(self):
        benchmark = load_module()

        plan = benchmark.ring_payload_plan(block_size_bytes=4 * 1024, levels=16, z=33, s=48)

        self.assertEqual(plan.online_read_bytes, 4 * 1024)
        self.assertEqual(plan.eviction_read_bytes, 16 * 33 * 4 * 1024)
        self.assertEqual(plan.eviction_write_bytes, 16 * (33 + 48) * 4 * 1024)

    def test_tiny_tcp_smoke_run_completes(self):
        benchmark = load_module()
        server = benchmark.PayloadServer("127.0.0.1", 0)
        server.start()
        try:
            result = benchmark.measure_path_oram(
                host="127.0.0.1",
                port=server.port,
                block_size_bytes=64,
                accesses=1,
                levels=1,
                z=1,
            )
        finally:
            server.stop()

        self.assertEqual(result.algorithm, "Path ORAM")
        self.assertEqual(result.transfer_bytes, 128)
        self.assertGreaterEqual(result.elapsed_seconds, 0)


if __name__ == "__main__":
    unittest.main()
