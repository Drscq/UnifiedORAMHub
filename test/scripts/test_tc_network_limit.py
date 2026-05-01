import subprocess
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT_PATH = REPO_ROOT / "scripts" / "tc_network_limit.sh"


class TCNetworkLimitScriptTest(unittest.TestCase):
    def run_script(self, *args):
        return subprocess.run(
            ["bash", str(SCRIPT_PATH), *args],
            check=True,
            capture_output=True,
            text=True,
        )

    def test_apply_dry_run_emits_default_limit_commands(self):
        completed = self.run_script("apply", "--dry-run")

        self.assertIn(
            "tc qdisc replace dev lo root handle 1: htb default 10",
            completed.stdout,
        )
        self.assertIn(
            "tc class add dev lo parent 1: classid 1:10 htb rate 50mbit ceil 50mbit",
            completed.stdout,
        )
        self.assertIn(
            "tc qdisc add dev lo parent 1:10 handle 10: netem delay 2.5ms",
            completed.stdout,
        )

    def test_apply_dry_run_can_derive_one_way_delay_from_rtt(self):
        completed = self.run_script("apply", "--dry-run", "--dev", "eth0", "--rtt-ms", "8")

        self.assertIn(
            "tc qdisc add dev eth0 parent 1:10 handle 10: netem delay 4.000ms",
            completed.stdout,
        )

    def test_clear_dry_run_emits_delete_command_for_interface(self):
        completed = self.run_script("clear", "--dry-run", "--dev", "eth0")

        self.assertIn("tc qdisc del dev eth0 root", completed.stdout)


if __name__ == "__main__":
    unittest.main()
