"""从 RTSP 摄像头截取 ST7735S 屏幕的正面画面。

为什么需要这个脚本：屏幕只有 128x160，用摄像头斜着拍摄时四周会有大量
无用画面，而且小字号在 4K 原图里会被镜头色差与摩尔纹污染。脚本会：

1. 从 RTSP 流里取若干帧并做逐像素中值合成，压掉传感器随机噪声；
2. 自动找到屏幕面板的四个角（也可用 ``--quad`` 手工指定）；
3. 做透视校正，直接得到 128x160 的正面图，避免大比例降采样混叠；
4. 保存 PNG（可用 ``--scale`` 放大，方便肉眼检查）。

用法示例（URL 里的账号密码请通过环境变量传入，不要写进仓库）：

    PICO_SCREEN_RTSP_URL='rtsp://user:pass@192.168.10.26/stream0' \
        python3 tools/capture_screen.py --out /tmp/screen.png --scale 3
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

import cv2
import numpy as np

# ST7735S 的可见区域尺寸（像素）。
kScreenWidth = 128
kScreenHeight = 160

# 面板自动定位失败时的兜底四角（本机摄像头固定机位下实测值）。
kFallbackQuad = np.float32([[1346, 244], [1294, 1827], [2651, 1872], [2581, 322]])


def grab_median_frame(url: str, frame_count: int) -> np.ndarray:
    """Read frames from ``url`` and return their per-pixel median image."""
    capture = cv2.VideoCapture(url, cv2.CAP_FFMPEG)
    if not capture.isOpened():
        raise RuntimeError(f"无法打开视频流：{url}")
    frames = []
    try:
        for _ in range(frame_count * 3):
            ok, frame = capture.read()
            if not ok:
                continue
            frames.append(frame.astype(np.float32))
            if len(frames) >= frame_count:
                break
    finally:
        capture.release()
    if not frames:
        raise RuntimeError("视频流没有返回任何可用帧")
    return np.median(np.stack(frames), axis=0)


def detect_panel_quad(frame: np.ndarray) -> np.ndarray | None:
    """Locate the LCD panel's four corners in a camera frame."""
    blue, green, red = cv2.split(frame)
    # 面板是偏蓝的液晶背光，桌面与机身偏暖色、偏暗，据此做粗筛。
    mask = ((blue - red) > 18) & (blue > 25) & (blue < 190)
    mask = mask.astype(np.uint8) * 255
    kernel = np.ones((15, 15), np.uint8)
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel)
    mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, kernel)

    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    if not contours:
        return None
    largest = max(contours, key=cv2.contourArea)
    # 面板在画面里常被排线或手挡住一个角，approxPolyDP 会得到 5~6 个顶点。
    # 用最小外接旋转矩形更稳：它总是恰好 4 个角，且对轻微遮挡不敏感。
    rect = cv2.minAreaRect(largest)
    if rect[1][0] < 32 or rect[1][1] < 32:
        return None
    return np.float32(cv2.boxPoints(rect))


def order_quad(quad: np.ndarray) -> np.ndarray:
    """Order four corners as top-left, top-right, bottom-right, bottom-left.

    直接把点按“到质心的角度”排序得到环绕顺序，再把最靠左上的点转到首位，
    最后按叉积判断环绕方向。这样即使面板在画面里是竖放、略有旋转，甚至
    是 minAreaRect 返回的任意起始角，也能得到稳定的起点与方向；用
    “x+y 最小/最大”那种启发式在竖放矩形上会把角点错配成旋转 90 度。
    """
    points = quad.reshape(4, 2).astype(np.float32)
    center = points.mean(axis=0)
    angles = np.arctan2(points[:, 1] - center[1], points[:, 0] - center[0])
    ordered = points[np.argsort(angles)]
    start = int(np.argmin(ordered.sum(axis=1)))
    ordered = np.roll(ordered, -start, axis=0)
    # 图像坐标系 y 轴向下，“左上→右上→右下”的叉积为正；为负说明是反向环绕。
    first = ordered[1] - ordered[0]
    second = ordered[2] - ordered[1]
    if float(first[0] * second[1] - first[1] * second[0]) < 0:
        ordered = ordered[[0, 3, 2, 1]]
    return ordered


def warp_to_screen(frame: np.ndarray, quad: np.ndarray) -> np.ndarray:
    """Perspective-correct the panel region into a 128x160 image."""
    source = order_quad(quad)
    target = np.float32(
        [
            [0, 0],
            [kScreenWidth - 1, 0],
            [kScreenWidth - 1, kScreenHeight - 1],
            [0, kScreenHeight - 1],
        ]
    )
    transform = cv2.getPerspectiveTransform(source, target)
    return cv2.warpPerspective(
        frame, transform, (kScreenWidth, kScreenHeight), flags=cv2.INTER_AREA
    )


def parse_arguments(argv: list[str]) -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(description="截取 ST7735S 屏幕正面画面")
    parser.add_argument(
        "--url",
        default=os.environ.get("PICO_SCREEN_RTSP_URL", ""),
        help="RTSP 地址，默认读取环境变量 PICO_SCREEN_RTSP_URL",
    )
    parser.add_argument("--frames", type=int, default=20, help="参与中值合成的帧数")
    parser.add_argument("--scale", type=int, default=3, help="输出放大倍数")
    parser.add_argument("--out", default="/tmp/screen.png", help="输出 PNG 路径")
    parser.add_argument(
        "--quad",
        default="",
        help="手工指定四角，格式 x1,y1;x2,y2;x3,y3;x4,y4（按左上起顺时针）",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    """Capture one screenshot and report where it was written."""
    arguments = parse_arguments(argv)
    if not arguments.url:
        print("缺少 RTSP 地址：请用 --url 或环境变量 PICO_SCREEN_RTSP_URL 提供")
        return 2

    frame = grab_median_frame(arguments.url, arguments.frames)

    if arguments.quad:
        quad = np.float32(
            [
                [float(value) for value in point.split(",")]
                for point in arguments.quad.split(";")
            ]
        )
    else:
        quad = detect_panel_quad(frame)
        if quad is None:
            print("未能自动定位面板，改用内置兜底四角")
            quad = kFallbackQuad

    screen = warp_to_screen(frame, quad)
    if arguments.scale > 1:
        screen = cv2.resize(
            screen,
            (kScreenWidth * arguments.scale, kScreenHeight * arguments.scale),
            interpolation=cv2.INTER_NEAREST,
        )

    output_path = Path(arguments.out)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    if not cv2.imwrite(str(output_path), screen):
        print(f"写入失败：{output_path}")
        return 1
    print(f"已保存 {output_path}（{screen.shape[1]}x{screen.shape[0]}）")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
