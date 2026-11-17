"""Linux 指标启动脚本的静态契约测试。

这些测试只读取 ``tools/start_metrics.sh``，不执行脚本本体，因此不依赖真实
网卡或 Pico。
"""

import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
START_SCRIPT = REPO_ROOT / "tools" / "start_metrics.sh"


class StartMetricsShellScriptTests(unittest.TestCase):
    """Assert Linux start script contracts without executing it."""

    @classmethod
    def setUpClass(cls):
        cls.script = START_SCRIPT.read_text(encoding="utf-8")

    def test_script_file_exists(self):
        self.assertTrue(START_SCRIPT.is_file(), f"missing {START_SCRIPT}")

    def test_uses_bash_with_strict_mode(self):
        self.assertTrue(self.script.startswith("#!/bin/bash\n"))
        self.assertIn("set -euo pipefail", self.script)

    def test_psutil_version_range_is_checked_without_new_dependencies(self):
        self.assertIn("import psutil", self.script)
        self.assertIn("psutil.__version__", self.script)
        self.assertIn("(5, 9) <= v < (8, 0)", self.script)
        self.assertIn("-m pip install -r requirements-host.txt", self.script)

    def test_defaults_orin_wired_interface_when_env_unset(self):
        self.assertIn('PICO_ETHERNET_INTERFACE', self.script)
        self.assertIn("enP8p1s0", self.script)
        self.assertRegex(
            self.script,
            r'\$\{PICO_ETHERNET_INTERFACE:-enP8p1s0\}|'
            r'PICO_ETHERNET_INTERFACE=\$\{PICO_ETHERNET_INTERFACE:-enP8p1s0\}|'
            r'\[\[ -z "\$\{PICO_ETHERNET_INTERFACE',
        )

    def test_invokes_pc_metrics_host_module(self):
        self.assertIn("-u -m pc_metrics.host", self.script)

    def test_documents_exit_codes_in_header(self):
        self.assertIn("退出码", self.script)
        self.assertIn("环境不满足", self.script)
        self.assertIn("主机采集器启动失败", self.script)


if __name__ == "__main__":
    unittest.main()
