/**
 * @file ui_input.h
 * @brief 输入源抽象：把「有没有一次单击」从具体按键实现里摘出来。
 *
 * 渲染任务既要画屏，也要在同一个 20 ms 节拍里采样按键——消抖需要稳定的
 * 采样周期，另起一个按键任务只会让两边的节拍互相打架。但渲染层又不该认识
 * GPIO，于是用一个只有一个函数指针的结构体把「采样」抽象掉：
 *   - 固件侧由 board_button.c 提供真实实现（GP15）；
 *   - 宿主测试传 NULL 或者自己造一个假实现。
 *
 * 本头文件只依赖 <stdbool.h>，任何层都可以包含它。
 */

#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_UI_INPUT_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_UI_INPUT_H_

#include <stdbool.h>

/** @brief 输入源：每次调用 poll_click() 采样一拍。 */
typedef struct {
  /**
   * @brief 采样一次按键，返回是否发生了一次「按下」。
   *
   * 实现方负责消抖：连续两拍低电平才算一次单击，按住不放只算一次。
   * 返回 true 表示本次应当推进一个主题。
   */
  bool (*poll_click)(void);
} UiInputSource;

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_UI_INPUT_H_ */
