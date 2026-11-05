#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_ST7735S_DRIVER_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_ST7735S_DRIVER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 逻辑分辨率：面板原生 128x160，固件按竖屏排版。 */
#define kDisplayWidth 128
#define kDisplayHeight 160

/* 16 行切片：128*16*2 = 4096 字节/片，双缓冲合计 8192 字节，正好落在
 * 显示子系统 8 KB 的内存预算内。全屏 160 行 = 10 个切片。 */
#define kDisplaySliceRows 16
#define kDisplaySlicePixels (kDisplayWidth * kDisplaySliceRows)
#define kDisplaySliceBytes (kDisplaySlicePixels * 2)

typedef struct DisplayBackend DisplayBackend;

/* 显示后端抽象：把“渲染一帧”和“把像素送上面板”解耦。
 * 设备端是 ST7735S + DMA，宿主端是内存帧缓冲，渲染逻辑两边共用。 */
struct DisplayBackend {
  const char* name;

  /* 开始一帧。返回 false 表示后端尚未就绪。 */
  bool (*begin_frame)(DisplayBackend* self);

  /* 提交从 y 行开始、共 height 行的整行切片（128*height 个 RGB565 像素）。
   * 实现可以异步启动传输，但下一次 write_slice 前必须保证上一片已经
   * 完成；调用方据此用两块缓冲交替光栅化与发送。 */
  bool (*write_slice)(DisplayBackend* self, int y, int height,
                      const uint16_t* pixels, size_t pixel_count);

  /* 结束一帧，等待所有异步传输完成并把 CS 拉高。 */
  void (*end_frame)(DisplayBackend* self);

  /* 后端私有数据，由实现解释。 */
  void* context;
};

/* 初始化 SPI0、GP16..GP20 与两块 DMA 通道，并下发 ST7735S 初始化序列。
 * 重复调用是幂等的；硬件不可用时返回 false。 */
bool st7735s_init(void);

/* 返回 ST7735S 的显示后端。未初始化时也能取到指针，但 begin_frame 会
 * 返回 false。 */
DisplayBackend* st7735s_backend(void);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_ST7735S_DRIVER_H_ */
