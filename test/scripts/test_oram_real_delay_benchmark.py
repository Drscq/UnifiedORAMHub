import importlib.util
import subprocess
import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT_PATH = REPO_ROOT / "scripts" / "oram_real_delay_benchmark.py"


def load_module():
    spec = importlib.util.spec_from_file_location("oram_real_delay_benchmark", SCRIPT_PATH)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class ORAMRealDelayBenchmarkScriptTest(unittest.TestCase):
    def test_build_commands_target_real_benchmark_executable(self):
        module = load_module()

        commands = module.build_commands(REPO_ROOT / "build")

        self.assertEqual(commands[-1][0:4], ["cmake", "--build", str(REPO_ROOT / "build"), "--target"])
        self.assertIn("oram_real_delay_benchmark", commands[-1])

    def test_dry_run_prints_build_and_benchmark_commands(self):
        completed = subprocess.run(
            [
                sys.executable,
                str(SCRIPT_PATH),
                "--dry-run",
                "--",
                "--accesses",
                "2",
                "--block-sizes-kib",
                "1",
            ],
            cwd=REPO_ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=True,
        )

        self.assertIn("cmake --build", completed.stdout)
        self.assertIn("oram_real_delay_benchmark", completed.stdout)
        self.assertIn("--accesses 2", completed.stdout)


if __name__ == "__main__":
    unittest.main()
