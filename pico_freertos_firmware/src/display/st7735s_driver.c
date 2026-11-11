#include "st7735s_driver.h"

#include <stddef.h>
#include <stdint.h>

#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/regs/dreq.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

/* 硬件连接：SPI0 只使用 SCK/MOSI，CS/DC/RST 由软件控制。 */
enum {
  kPinSck = 18,
  kPinMosi = 19,
  kPinCs = 17,
  kPinDc = 21,
  kPinRst = 20,
};

/* ST7735S 命令字。 */
enum {
  kCmdSoftwareReset = 0x01,
  kCmdSleepOut = 0x11,
  kCmdInversionOff = 0x20,
  kCmdDisplayOn = 0x29,
  kCmdColumnAddressSet = 0x2a,
  kCmdRowAddressSet = 0x2b,
  kCmdMemoryWrite = 0x2c,
  kCmdMemoryDataAccessControl = 0x36,
  kCmdInterfacePixelFormat = 0x3a,
};

static spi_inst_t* const kSpi = spi0;

static bool s_initialized = false;

/* 发往面板的字节暂存区。
 *
 * 为什么需要它：渲染层交来的是主机序（小端）的 RGB565 半字数组，而
 * ST7735S 在 COLMOD=0x05（16 位/像素）下要求“高字节在前”。两块内存
 * 的字节序不同，必须转换一次。
 *
 * 只有一块暂存区是安全的：write_slice() 在写入前一定会先等上一片
 * DMA 结束（现已改用轮询），任何时刻最多只有一片数据在总线上。 */
static uint8_t s_slice_bytes[kDisplaySlicePixels * 2];

/* 等 SPI 移位寄存器与 FIFO 都排空，之后才能安全地翻转 DC 电平。 */
static void st7735s_wait_idle(void) {
  while (spi_is_busy(kSpi)) {
    tight_loop_contents();
  }
}

/* 小批量字节（命令与参数）走轮询；像素数据由 DMA 搬运。
 * spi_write_blocking 负责排空接收 FIFO 并清掉 PL022 的溢出标志。 */
static void st7735s_write_bytes(const uint8_t* data, size_t length) {
  spi_write_blocking(kSpi, data, length);
  st7735s_wait_idle();
}

/* 发命令：拉低 CS，DC 低发命令；带参数时拉高 DC 发数据；发完拉高 CS。
 * 严格按照 ST7735S 协议在每个命令周期翻转 CS，确保芯片串行解包状态机复位对齐。
 */
static void st7735s_write_command(uint8_t command, const uint8_t* data,
                                  size_t length) {
  st7735s_wait_idle();
  gpio_put(kPinCs, 0);
  gpio_put(kPinDc, 0);
  st7735s_write_bytes(&command, 1);
  if (length > 0) {
    gpio_put(kPinDc, 1);
    st7735s_write_bytes(data, length);
  }
  st7735s_wait_idle();
  gpio_put(kPinCs, 1);
}

static void st7735s_set_window(int x, int y, int width, int height) {
  const uint8_t caset[4] = {0, (uint8_t)x, 0, (uint8_t)(x + width - 1)};
  const uint8_t raset[4] = {0, (uint8_t)y, 0, (uint8_t)(y + height - 1)};
  st7735s_write_command(kCmdColumnAddressSet, caset, sizeof(caset));
  st7735s_write_command(kCmdRowAddressSet, raset, sizeof(raset));
  st7735s_write_command(kCmdMemoryWrite, NULL, 0);
}

/* 上电复位时序与厂商标定的初始化寄存器序列。 */
static void st7735s_hardware_reset(void) {
  gpio_put(kPinCs, 1);
  gpio_put(kPinDc, 0);
  gpio_put(kPinRst, 1);
  sleep_ms(5);
  gpio_put(kPinRst, 0);
  sleep_ms(20);
  gpio_put(kPinRst, 1);
  sleep_ms(120);
}

static void st7735s_send_init_sequence(void) {
  static const uint8_t kPowerControl1[] = {0x01, 0x2c, 0x2d};
  static const uint8_t kPowerControl3[] = {0x01, 0x2c, 0x2d, 0x01, 0x2c, 0x2d};
  static const uint8_t kPowerControl4[] = {0x07};
  static const uint8_t kPowerControl5[] = {0xa2, 0x02, 0x84};
  static const uint8_t kVcomh[] = {0xc5};
  static const uint8_t kVcoml[] = {0x0a, 0x00};
  static const uint8_t kVdvvrh[] = {0x8a, 0x2a};
  static const uint8_t kVdvvrl[] = {0x8a, 0xee};
  static const uint8_t kPowerControl6[] = {0x0e};
  static const uint8_t kMadtcl[] = {0xc8};
  static const uint8_t kColmod[] = {0x05};
  static const uint8_t kPositiveGamma[] = {0x02, 0x1c, 0x07, 0x12, 0x37, 0x32,
                                           0x29, 0x2d, 0x29, 0x25, 0x2b, 0x39,
                                           0x00, 0x01, 0x03, 0x10};
  static const uint8_t kNegativeGamma[] = {0x03, 0x1d, 0x07, 0x06, 0x2e, 0x2c,
                                           0x29, 0x2d, 0x2e, 0x2e, 0x37, 0x3f,
                                           0x00, 0x00, 0x02, 0x10};

  st7735s_write_command(kCmdSoftwareReset, NULL, 0);
  sleep_ms(150);
  st7735s_write_command(kCmdSleepOut, NULL, 0);
  sleep_ms(120);

  st7735s_write_command(0xb1, kPowerControl1, sizeof(kPowerControl1));
  st7735s_write_command(0xb2, kPowerControl1, sizeof(kPowerControl1));
  st7735s_write_command(0xb3, kPowerControl3, sizeof(kPowerControl3));
  st7735s_write_command(0xb4, kPowerControl4, sizeof(kPowerControl4));
  st7735s_write_command(0xc0, kPowerControl5, sizeof(kPowerControl5));
  st7735s_write_command(0xc1, kVcomh, sizeof(kVcomh));
  st7735s_write_command(0xc2, kVcoml, sizeof(kVcoml));
  st7735s_write_command(0xc3, kVdvvrh, sizeof(kVdvvrh));
  st7735s_write_command(0xc4, kVdvvrl, sizeof(kVdvvrl));
  st7735s_write_command(0xc5, kPowerControl6, sizeof(kPowerControl6));
  st7735s_write_command(kCmdInversionOff, NULL, 0);
  st7735s_write_command(kCmdMemoryDataAccessControl, kMadtcl, sizeof(kMadtcl));
  st7735s_write_command(kCmdInterfacePixelFormat, kColmod, sizeof(kColmod));
  st7735s_write_command(0xe0, kPositiveGamma, sizeof(kPositiveGamma));
  st7735s_write_command(0xe1, kNegativeGamma, sizeof(kNegativeGamma));
  st7735s_write_command(kCmdDisplayOn, NULL, 0);
  sleep_ms(100);
}

static bool st7735s_begin_frame(DisplayBackend* self) {
  (void)self;
  if (!s_initialized) {
    return false;
  }
  st7735s_wait_idle();
  gpio_put(kPinCs, 1);
  return true;
}

static bool st7735s_write_slice(DisplayBackend* self, int y, int height,
                                const uint16_t* pixels, size_t pixel_count) {
  (void)self;
  if (!s_initialized || pixels == NULL || y < 0 || height <= 0 ||
      height > kDisplaySliceRows || y + height > kDisplayHeight) {
    return false;
  }
  if (pixel_count != (size_t)kDisplayWidth * (size_t)height) {
    return false;
  }

  /* 转成面板要求的“高字节在前”字节流，见 s_slice_bytes 的说明。 */
  for (size_t i = 0; i < pixel_count; i++) {
    const uint16_t value = pixels[i];
    s_slice_bytes[i * 2u] = (uint8_t)(value >> 8);
    s_slice_bytes[i * 2u + 1u] = (uint8_t)(value & 0xffu);
  }

  /* 设置切片地址窗口（内部每个命令均有独立 CS 周期）。 */
  st7735s_set_window(0, y, kDisplayWidth, height);

  /* 搬运像素数据：独立拉低 CS，写完等待排空并拉高 CS。
   * 与 MicroPython 参考驱动完全对齐，彻底避免长周期 CS 低电平
   * 导致 ST7735S 内部字节计数器产生相位偏移引发的闪纹与青条。 */
  st7735s_wait_idle();
  gpio_put(kPinCs, 0);
  gpio_put(kPinDc, 1);
  spi_write_blocking(kSpi, s_slice_bytes, pixel_count * 2u);
  st7735s_wait_idle();
  gpio_put(kPinCs, 1);

  return true;
}

static void st7735s_end_frame(DisplayBackend* self) {
  (void)self;
  st7735s_wait_idle();
  gpio_put(kPinDc, 0);
  gpio_put(kPinCs, 1);
}

static DisplayBackend s_backend = {
    .name = "st7735s",
    .begin_frame = st7735s_begin_frame,
    .write_slice = st7735s_write_slice,
    .end_frame = st7735s_end_frame,
    .context = NULL,
};

bool st7735s_init(void) {
  if (s_initialized) {
    return true;
  }

  gpio_init(kPinCs);
  gpio_set_dir(kPinCs, GPIO_OUT);
  gpio_put(kPinCs, 1);
  gpio_init(kPinDc);
  gpio_set_dir(kPinDc, GPIO_OUT);
  gpio_put(kPinDc, 0);
  gpio_init(kPinRst);
  gpio_set_dir(kPinRst, GPIO_OUT);
  gpio_put(kPinRst, 1);

  gpio_set_function(kPinSck, GPIO_FUNC_SPI);
  gpio_set_function(kPinMosi, GPIO_FUNC_SPI);
  /* spi_init 内部按 clk_peri 计算分频，并设置 8 位/CPOL=0/CPHA=0/MSB。
   * 波特率取 10 MHz：降低时钟频率以抑制杜邦线跳线上的高频信号反射与抖动，
   * 进一步提升信号完整性。整屏 40960 字节在 10 MHz 下仅需约 32 毫秒，
   * 依然完全满足 30 FPS 左右的平滑刷新需求。 */
  spi_init(kSpi, 10000000);

  st7735s_hardware_reset();
  gpio_put(kPinCs, 0);
  st7735s_send_init_sequence();
  gpio_put(kPinCs, 1);

  s_initialized = true;
  return true;
}

DisplayBackend* st7735s_backend(void) { return &s_backend; }
