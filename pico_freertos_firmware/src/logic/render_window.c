/**
 * @file render_window.c
 * @brief 渲染窗口的宿主实现：用 C11 原子量表达跨核可见性。
 */

#include "render_window.h"

#include <stdatomic.h>

/** @brief 渲染任务正在刷屏时置 1；静态初值为 0（空闲）。 */
static atomic_int s_busy;

void render_window_enter(void) {
  atomic_store_explicit(&s_busy, 1, memory_order_seq_cst);
}

void render_window_leave(void) {
  atomic_store_explicit(&s_busy, 0, memory_order_seq_cst);
}

bool render_window_is_idle(void) {
  return atomic_load_explicit(&s_busy, memory_order_seq_cst) == 0;
}
