/**
 * @file board_button.c
 * @brief 板载按键（GP15）的实现：只做电平采样，消抖交给 button_filter。
 */

#include "board_button.h"

#include "button_filter.h"
#include "hardware/gpio.h"

/** @brief GP15：唯一的外置按键，接 GND，内部上拉，低电平有效。 */
enum { kBoardButtonPin = 15 };

/** @brief 2/2 采样消抖状态；只在渲染任务里被访问。 */
static ButtonFilter s_filter;

void board_button_init(void) {
  gpio_init(kBoardButtonPin);
  gpio_set_dir(kBoardButtonPin, GPIO_IN);
  gpio_pull_up(kBoardButtonPin);
  button_filter_reset(&s_filter);
}

bool board_button_poll_click(void) {
  const bool level_low = gpio_get(kBoardButtonPin) == 0;
  return button_filter_update(&s_filter, level_low);
}

const UiInputSource kBoardInput = {board_button_poll_click};
