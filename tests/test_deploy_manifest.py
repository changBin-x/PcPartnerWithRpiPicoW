"""部署/启动 PowerShell 脚本的静态契约测试。

这些测试只读取脚本与 ``pico/`` 目录，不执行 PowerShell，
因此可以在 Linux 上、在没有安装 mpremote / Pico 的环境里运行。
"""

import re
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEPLOY_SCRIPT = REPO_ROOT / "tools" / "deploy_pico.ps1"
START_SCRIPT = REPO_ROOT / "tools" / "start_metrics.ps1"
PICO_ROOT = REPO_ROOT / "pico"

SCRIPT_ENCODING = "utf-8-sig"

# 两个本地配置文件算在 17 项清单里，但不进仓库（.gitignore），也不参与“磁盘文件全覆盖”断言。
LOCAL_SECRET_FILES = frozenset({"wifi_secrets.py", "weather_secrets.py"})

EXPECTED_FIRMWARE_FILES = (
    "__init__.py",
    "amap_root_ca.pem",
    "app.py",
    "display.py",
    "font_cn.bin",
    "font_cn.py",
    "glyph_font.py",
    "hko_root_ca.pem",
    "lunar.py",
    "lunar_table.bin",
    "st7735.py",
    "time_sync.py",
    "udp_receiver.py",
    "weather.py",
    "wifi.py",
)

EXPECTED_MANIFEST = frozenset(EXPECTED_FIRMWARE_FILES) | LOCAL_SECRET_FILES


def parse_package_manifest(script):
    """从 ``$packageFiles = @( ... )`` 中取出字符串清单。"""
    match = re.search(r"\$packageFiles\s*=\s*@\((?P<body>.*?)\)", script, re.DOTALL)
    if match is None:
        raise AssertionError("tools/deploy_pico.ps1 里找不到 $packageFiles 清单")
    return re.findall(r'"([^"]+)"', match.group("body"))


class DeployManifestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.script = DEPLOY_SCRIPT.read_text(encoding=SCRIPT_ENCODING)
        cls.manifest = parse_package_manifest(cls.script)

    def test_manifest_contains_exactly_the_expected_17_files(self):
        self.assertEqual(17, len(self.manifest))
        self.assertEqual(17, len(set(self.manifest)), "部署清单存在重复条目")
        self.assertEqual(EXPECTED_MANIFEST, set(self.manifest))

    def test_manifest_splits_into_fifteen_firmware_files_and_two_secrets(self):
        self.assertEqual(15, len(EXPECTED_FIRMWARE_FILES))
        self.assertEqual(
            set(EXPECTED_FIRMWARE_FILES),
            set(self.manifest) - LOCAL_SECRET_FILES,
        )
        for name in LOCAL_SECRET_FILES:
            self.assertIn(name, self.manifest)

    def test_every_packaged_file_exists_under_pico(self):
        missing = [
            name
            for name in self.manifest
            if name not in LOCAL_SECRET_FILES and not (PICO_ROOT / name).is_file()
        ]
        self.assertEqual([], missing, "部署清单引用了 pico/ 下不存在的文件")

        for example in ("wifi_secrets.example.py", "weather_secrets.example.py"):
            self.assertTrue((PICO_ROOT / example).is_file(), "缺少 %s" % example)

    def test_every_pico_file_is_covered_by_the_manifest(self):
        # 新增固件文件却忘记改清单时，这里会失败；避免漏传整个模块。
        on_disk = {
            path.name
            for path in PICO_ROOT.iterdir()
            if path.is_file()
            and path.name != "main.py"
            and path.name not in LOCAL_SECRET_FILES
            and not path.name.endswith("_secrets.example.py")
        }
        self.assertEqual([], sorted(on_disk - set(self.manifest)))
        self.assertEqual(sorted(on_disk), sorted(set(self.manifest) - LOCAL_SECRET_FILES))

    def test_main_py_is_copied_to_root_and_device_is_reset(self):
        self.assertNotIn("main.py", self.manifest, "main.py 必须单独复制到 :/main.py")
        self.assertIn('":/main.py"', self.script)
        copy_lines = [
            line
            for line in self.script.splitlines()
            if "main.py" in line and ":/main.py" in line
        ]
        self.assertTrue(copy_lines, "缺少 main.py → :/main.py 的复制步骤")
        self.assertIn('"reset"', self.script)

    def test_missing_config_reports_red_text_and_exit_code_2(self):
        self.assertNotIn("Write-Error", self.script)
        self.assertEqual(2, self.script.count("exit 2"))
        self.assertRegex(
            self.script,
            r'Write-Host "[^"]*wifi_secrets\.py" -ForegroundColor Red',
        )
        self.assertRegex(
            self.script,
            r'Write-Host "[^"]*weather_secrets\.py" -ForegroundColor Red',
        )
        self.assertIn("退出码", self.script)

    def test_header_documents_the_exit_code_contract(self):
        self.assertRegex(self.script, r"#\s*0\s+部署成功")
        self.assertRegex(self.script, r"#\s*1\s+一般失败")
        self.assertRegex(self.script, r"#\s*2\s+缺少本地配置")

    def test_port_is_validated_before_connecting(self):
        self.assertIn("GetPortNames()", self.script)
        self.assertIn("Thonny", self.script)
        self.assertIn("拼写有误", self.script)

    def test_port_is_interpolated_and_com4_only_appears_as_the_default(self):
        self.assertIn('"connect", $Port', self.script)
        self.assertGreaterEqual(self.script.count("$Port"), 8)
        com4_lines = [line.strip() for line in self.script.splitlines() if "COM4" in line]
        self.assertEqual(1, len(com4_lines), "COM4 只允许作为 -Port 的默认值出现")
        self.assertRegex(com4_lines[0], r'^\[string\]\$Port\s*=\s*"COM4"$')
        self.assertNotIn("COM4 设备", self.script)

    def test_mpremote_calls_are_wrapped_with_timeouts(self):
        self.assertIn("function Invoke-Mpremote", self.script)
        self.assertRegex(self.script, r"-Timeout\s+30\b")
        self.assertRegex(self.script, r"-Timeout\s+180\b")
        self.assertIn("$probeResult.TimedOut", self.script)
        self.assertIn("$uploadResult.TimedOut", self.script)
        self.assertIn("WaitForExit", self.script)
        self.assertIn(".Kill()", self.script)
        self.assertNotIn("2>&1", self.script, "5.1 下合并原生 stderr 会抛 NativeCommandError")

    def test_package_sentinel_is_verified_before_upload(self):
        self.assertIn("PICO_PACKAGE_READY", self.script)
        self.assertRegex(
            self.script,
            r'\$prepareResult\.Output\s+-notmatch\s+"PICO_PACKAGE_READY"',
        )
        self.assertLess(
            self.script.index("PICO_PACKAGE_READY"),
            self.script.index("$uploadResult = Invoke-Mpremote"),
            "哨兵校验必须发生在上传之前",
        )

    def test_arguments_are_not_routed_through_a_shell(self):
        for forbidden in ("cmd.exe", "cmd /c", "Invoke-Expression"):
            self.assertNotIn(forbidden, self.script)
        self.assertIn("-Arguments", self.script)
        self.assertEqual(1, self.script.count("-Timeout 180"))


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
