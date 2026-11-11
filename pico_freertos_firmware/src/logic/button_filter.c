/**
 * @file button_filter.c
 * @brief 2/2 采样消抖：连续两拍同电平才改变按键状态。
 */

#include "button_filter.h"

/** @brief 判定一次有效动作所需的连续采样拍数。 */
enum { kSamplesRequired = 2 };

void button_filter_reset(ButtonFilter* filter) {
  filter->low_samples = 0;
  filter->high_samples = 0;
  filter->pressed = false;
}

bool button_filter_update(ButtonFilter* filter, bool level_low) {
  if (level_low) {
    filter->high_samples = 0;
    if (filter->low_samples < kSamplesRequired) {
      ++filter->low_samples;
    }
    if (!filter->pressed && filter->low_samples >= kSamplesRequired) {
      filter->pressed = true;
      return true;
    }
    return false;
  }
  filter->low_samples = 0;
  if (filter->high_samples < kSamplesRequired) {
    ++filter->high_samples;
  }
  if (filter->pressed && filter->high_samples >= kSamplesRequired) {
    filter->pressed = false;
  }
  return false;
}
