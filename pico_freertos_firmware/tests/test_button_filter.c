/**
 * @file test_button_filter.c
 * @brief 验证 2/2 采样消抖：必须连续两拍同电平才改变状态。
 */

#include <stdio.h>
#include <stdlib.h>

#include "button_filter.h"

/** @brief 连续喂同一电平 n 拍。 */
static void feed(ButtonFilter* filter, bool low, unsigned times,
                 unsigned* clicks) {
  for (unsigned i = 0; i < times; ++i) {
    if (button_filter_update(filter, low)) {
      ++(*clicks);
    }
  }
}

int main(void) {
  ButtonFilter filter;
  unsigned clicks = 0;

  /* 抖动：低-高-低-高 不应产生任何单击。 */
  button_filter_reset(&filter);
  feed(&filter, true, 1, &clicks);
  feed(&filter, false, 1, &clicks);
  feed(&filter, true, 1, &clicks);
  feed(&filter, false, 1, &clicks);
  if (clicks != 0) {
    printf("FAIL: 抖动产生了 %u 次单击\n", clicks);
    return EXIT_FAILURE;
  }

  /* 稳定按下 2 拍 = 一次单击（在第 2 拍产生）。 */
  feed(&filter, true, 2, &clicks);
  if (clicks != 1) {
    printf("FAIL: 按下未产生单击（%u）\n", clicks);
    return EXIT_FAILURE;
  }

  /* 持续按住不再重复触发。 */
  feed(&filter, true, 10, &clicks);
  if (clicks != 1) {
    printf("FAIL: 按住重复触发（%u）\n", clicks);
    return EXIT_FAILURE;
  }

  /* 释放 2 拍后才能再次触发。 */
  feed(&filter, false, 1, &clicks);
  feed(&filter, true, 1, &clicks);
  if (clicks != 1) {
    printf("FAIL: 释放未满 2 拍就再次触发（%u）\n", clicks);
    return EXIT_FAILURE;
  }
  feed(&filter, false, 2, &clicks);
  feed(&filter, true, 2, &clicks);
  if (clicks != 2) {
    printf("FAIL: 第二次单击未触发（%u）\n", clicks);
    return EXIT_FAILURE;
  }

  /* 上电即按住：按规格 §4.1 有意接受为一次单击。 */
  clicks = 0;
  button_filter_reset(&filter);
  feed(&filter, true, 2, &clicks);
  if (clicks != 1) {
    printf("FAIL: 上电按住应产生 1 次单击（%u）\n", clicks);
    return EXIT_FAILURE;
  }

  printf("PASS: 消抖行为符合规格 §4.1\n");
  return EXIT_SUCCESS;
}
