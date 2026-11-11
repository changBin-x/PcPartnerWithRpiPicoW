# pico/ 本地密钥目录

Pico 端当前使用 C + FreeRTOS 固件，源码位于 `pico_freertos_firmware/`。
本目录不再包含 MicroPython 固件；MicroPython 实现已归档在
`archive/micropython` 分支和 `micropython-final` 标签中。

这里只保留构建前由 `tools/sync_secrets.py` 读取的两个本地配置文件：

```python
# pico/wifi_secrets.py
SSID = "你的 Wi-Fi 名称"
PASSWORD = "你的 Wi-Fi 密码"
```

```python
# pico/weather_secrets.py
AMAP_WEB_KEY = "你的高德 Web 服务 Key"
CITY_OVERRIDE_ADCODE = ""  # 可选，六位 adcode；留空则自动定位
```

这两个文件已被 `.gitignore` 忽略，不会进入版本库，也不得粘贴到聊天、日志或
截图中。C 固件构建时，`tools/sync_secrets.py` 把它们转换成
`pico_freertos_firmware/include/firmware_secrets.h`（同样被忽略）；缺少文件时
仍可编译，但设备会因为没有 Wi-Fi 凭据而无法联网。
