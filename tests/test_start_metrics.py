"""Windows 指标启动脚本的静态契约测试。

这些测试只读取 ``tools/start_metrics.ps1``，不执行 PowerShell，因此可以在
Linux 上、在没有 Windows 与 Pico 的环境里运行。
"""

import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
START_SCRIPT = REPO_ROOT / "tools" / "start_metrics.ps1"
SCRIPT_ENCODING = "utf-8-sig"


class StartMetricsScriptTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.script = START_SCRIPT.read_text(encoding=SCRIPT_ENCODING)

    def test_psutil_version_range_is_checked_without_new_dependencies(self):
        self.assertIn("import psutil", self.script)
        self.assertIn("psutil.__version__", self.script)
        self.assertIn("(5, 9) <= v < (8, 0)", self.script)
        self.assertIn("-m pip install -r requirements-host.txt", self.script)

    def test_microsoft_store_stub_python_is_reported_accurately(self):
        self.assertIn("WindowsApps", self.script)
        self.assertIn("9009", self.script)
        self.assertIn("py -3", self.script)

    def test_ctrl_c_exit_code_is_treated_as_success(self):
        self.assertIn("-1073741510", self.script)
        self.assertIn("3221225786", self.script)
        self.assertIn("exit 0", self.script)


if __name__ == "__main__":
    unittest.main()
