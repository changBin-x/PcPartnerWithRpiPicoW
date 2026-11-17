"""Unit tests for host identity label detection and sanitizing."""

import unittest

from pc_metrics.labels import (
    DEFAULT_CPU_LABEL,
    DEFAULT_GPU_LABEL,
    DEFAULT_RAM_LABEL,
    detect_cpu_label,
    detect_gpu_label,
    detect_ram_label,
    format_ram_label,
    resolve_identity_labels,
    shorten_cpu_name,
    shorten_gpu_name,
)
from pc_metrics.protocol import LABEL_MAX_BYTES


class LabelHelperTests(unittest.TestCase):
    def test_format_ram_label_rounds_up_to_common_tiers(self):
        self.assertEqual(format_ram_label(7 * 1024**3), "8G")
        self.assertEqual(format_ram_label(8 * 1024**3), "8G")
        self.assertEqual(format_ram_label(15 * 1024**3), "16G")
        self.assertEqual(format_ram_label(16 * 1024**3), "16G")
        self.assertEqual(format_ram_label(16 * 1024**3 + 1), "32G")
        self.assertEqual(format_ram_label(63 * 1024**3), "64G")
        self.assertEqual(format_ram_label(0), DEFAULT_RAM_LABEL)
        self.assertEqual(format_ram_label(-1), DEFAULT_RAM_LABEL)

    def test_shorten_cpu_name_recognizes_orin_and_desktops(self):
        self.assertEqual(
            shorten_cpu_name("NVIDIA Orin NX Developer Kit"), "Orin NX"
        )
        self.assertEqual(shorten_cpu_name("Jetson AGX Orin"), "Orin AGX")
        self.assertEqual(
            shorten_cpu_name("AMD Ryzen 5 5600X 6-Core Processor"), "R5 5600X"
        )
        self.assertEqual(
            shorten_cpu_name("Intel(R) Core(TM) i7-13700K"), "i7-13700K"
        )
        self.assertEqual(shorten_cpu_name(""), DEFAULT_CPU_LABEL)

    def test_shorten_gpu_name(self):
        self.assertEqual(shorten_gpu_name("Orin (nvgpu)"), "Orin nvgpu")
        self.assertEqual(
            shorten_gpu_name("NVIDIA GeForce RTX 3060"), "RTX 3060"
        )
        self.assertEqual(
            shorten_gpu_name("A" * 40),
            "A" * LABEL_MAX_BYTES,
        )

    def test_detect_uses_injectable_sources(self):
        self.assertEqual(
            detect_cpu_label(reader=lambda: "NVIDIA Jetson Orin Nano"),
            "Orin Nano",
        )
        self.assertEqual(
            detect_gpu_label(query=lambda: "NVIDIA GeForce RTX 4090"),
            "RTX 4090",
        )
        self.assertEqual(detect_ram_label(total_bytes=24 * 1024**3), "32G")

    def test_detect_falls_back_on_failure(self):
        def boom() -> str:
            raise OSError("nope")

        self.assertEqual(detect_cpu_label(reader=boom), DEFAULT_CPU_LABEL)
        self.assertEqual(
            detect_gpu_label(query=boom, board_reader=boom),
            DEFAULT_GPU_LABEL,
        )

    def test_gpu_falls_back_to_orin_board_clue(self):
        def boom() -> str:
            raise OSError("nope")

        self.assertEqual(
            detect_gpu_label(
                query=boom,
                board_reader=lambda: "NVIDIA Jetson Orin NX Engineering Kit",
            ),
            "Orin nvgpu",
        )
        self.assertEqual(
            detect_gpu_label(
                query=lambda: "NVIDIA-SMI has failed because it couldn't "
                "communicate with the NVIDIA driver.",
                board_reader=lambda: "Jetson Orin Nano",
            ),
            "Orin nvgpu",
        )

    def test_resolve_prefers_env_overrides(self):
        labels = resolve_identity_labels(
            {
                "PICO_CPU_LABEL": "CustomCPU",
                "PICO_GPU_LABEL": "CustomGPU",
                "PICO_RAM_LABEL": "64G",
            },
            cpu_detector=lambda: "ignored",
            gpu_detector=lambda: "ignored",
            ram_detector=lambda: "ignored",
        )
        self.assertEqual(
            labels,
            {
                "cpu_label": "CustomCPU",
                "gpu_label": "CustomGPU",
                "ram_label": "64G",
            },
        )

    def test_resolve_sanitizes_env_and_falls_back(self):
        labels = resolve_identity_labels(
            {"PICO_CPU_LABEL": "  中文  "},
            cpu_detector=lambda: "Orin NX",
            gpu_detector=lambda: "GPU",
            ram_detector=lambda: "16G",
        )
        self.assertEqual(labels["cpu_label"], "Orin NX")


if __name__ == "__main__":
    unittest.main()
