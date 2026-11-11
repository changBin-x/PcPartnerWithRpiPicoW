#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_BUTTON_FILTER_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_BUTTON_FILTER_H_

#include <stdbool.h>
#include <stdint.h>

/** @brief 2/2 采样消抖状态。 */
typedef struct {
  uint8_t low_samples;  /**< 连续低电平计数 */
  uint8_t high_samples; /**< 连续高电平计数 */
  bool pressed;         /**< 是否处于已按下状态 */
} ButtonFilter;

/** @brief 复位到空闲态（IDLE）。 */
void button_filter_reset(ButtonFilter* filter);

/** @brief 喂一拍采样，返回 true 表示本拍产生一次有效单击。 */
bool button_filter_update(ButtonFilter* filter, bool level_low);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_BUTTON_FILTER_H_ */
