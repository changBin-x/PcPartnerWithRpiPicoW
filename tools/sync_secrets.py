"""把 pico/*_secrets.py 里的运行时密钥转换成固件可用的 C 头文件。

固件需要的 Wi-Fi 凭据与高德 Web 服务 Key 都只存在于被 .gitignore 忽略的
Python 文件里。与其在两处各维护一份，不如构建前从单一源头生成：

    python3 tools/sync_secrets.py

生成 pico_freertos_firmware/include/firmware_secrets.h（同样被忽略）。
某个文件缺失时对应字段写空字符串，固件据此判定「未配置」并跳过该功能，
从而在没有真实凭据的环境下依然可以编译与运行。
"""

import ast
import pathlib
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
WIFI_SOURCE = REPO_ROOT / "pico" / "wifi_secrets.py"
WEATHER_SOURCE = REPO_ROOT / "pico" / "weather_secrets.py"
OUTPUT = (
    REPO_ROOT / "pico_freertos_firmware" / "include" / "firmware_secrets.h"
)

TEMPLATE = """\
/* 本文件由 tools/sync_secrets.py 生成，请勿手工编辑。
 * 数据来源：pico/wifi_secrets.py、pico/weather_secrets.py
 * 该文件已被 .gitignore 忽略，不会进入版本库。 */
#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_FIRMWARE_SECRETS_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_FIRMWARE_SECRETS_H_

/* Wi-Fi SSID，空字符串表示未配置。 */
#define kFirmwareWifiSsid "{ssid}"
/* Wi-Fi 口令，空字符串表示开放网络或未配置。 */
#define kFirmwareWifiPassword "{password}"
/* 高德 Web 服务 Key，空字符串表示未配置天气功能。 */
#define kFirmwareAmapWebKey "{amap_key}"
/* 高德 adcode（行政区划代码），空字符串表示用固件内置默认城市。 */
#define kFirmwareWeatherAdcode "{adcode}"

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_FIRMWARE_SECRETS_H_ */
"""


def read_strings(path):
    """读取模块级字符串常量，忽略无法静态求值的表达式。"""
    values = {}
    if not path.exists():
        return values
    try:
        tree = ast.parse(path.read_text(encoding="utf-8"), str(path))
    except SyntaxError as error:
        print(f"警告: 无法解析 {path}: {error}", file=sys.stderr)
        return values
    for node in tree.body:
        if not isinstance(node, ast.Assign) or len(node.targets) != 1:
            continue
        target = node.targets[0]
        if not isinstance(target, ast.Name):
            continue
        try:
            value = ast.literal_eval(node.value)
        except ValueError:
            continue
        if isinstance(value, str):
            values[target.id] = value
    return values


def escape(value):
    """转义成 C 字符串字面量内容。"""
    return value.replace("\\", "\\\\").replace('"', '\\"')


def main():
    wifi = read_strings(WIFI_SOURCE)
    weather = read_strings(WEATHER_SOURCE)
    ssid = wifi.get("SSID", "")
    password = wifi.get("PASSWORD", "")
    amap_key = weather.get("AMAP_WEB_KEY", "")
    adcode = weather.get("CITY_OVERRIDE_ADCODE", "")
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(
        TEMPLATE.format(
            ssid=escape(ssid),
            password=escape(password),
            amap_key=escape(amap_key),
            adcode=escape(adcode),
        ),
        encoding="utf-8",
    )
    print(f"OK: 已生成 {OUTPUT.relative_to(REPO_ROOT)}")
    print(f"    Wi-Fi SSID {'已配置' if ssid else '未配置'}")
    print(f"    高德 Key  {'已配置' if amap_key else '未配置'}")
    print(f"    城市代码  {adcode if adcode else '未配置，使用固件默认值'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
