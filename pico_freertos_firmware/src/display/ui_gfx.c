/**
 * @file ui_gfx.c
 * @brief v2 渲染层的矩形图元实现：全部按屏幕坐标调用，内部平移到条带并裁剪。
 */

#include "ui_gfx.h"

#include <stddef.h>

/** @brief 把屏幕 y 转成条带行号；越界返回 -1。 */
static int strip_row(const UiSurface* surface, int screen_y) {
  int row = screen_y - surface->origin_y;
  if (row < 0 || row >= surface->height) {
    return -1;
  }
  return row;
}

/** @brief 整数平方根（逐位法），避免链接浮点库。 */
static int isqrt(int value) {
  int result = 0;
  int bit = 1 << 14;
  while (bit > value) {
    bit >>= 2;
  }
  while (bit != 0) {
    if (value >= result + bit) {
      value -= result + bit;
      result = (result >> 1) + bit;
    } else {
      result >>= 1;
    }
    bit >>= 2;
  }
  return result;
}

void gfx_clear(const UiSurface* surface, uint16_t color) {
  if (surface == NULL || surface->pixels == NULL) {
    return;
  }
  size_t total = (size_t)surface->width * (size_t)surface->height;
  for (size_t i = 0; i < total; ++i) {
    surface->pixels[i] = color;
  }
}

void gfx_fill(const UiSurface* surface, int x, int y, int w, int h,
              uint16_t color) {
  if (surface == NULL || surface->pixels == NULL || w <= 0 || h <= 0) {
    return;
  }
  int x0 = (x < 0) ? 0 : x;
  int x1 = (x + w > surface->width) ? surface->width : (x + w);
  if (x0 >= x1) {
    return;
  }
  for (int screen_y = y; screen_y < y + h; ++screen_y) {
    int row = strip_row(surface, screen_y);
    if (row < 0) {
      continue;
    }
    uint16_t* line = &surface->pixels[(size_t)row * (size_t)surface->width];
    for (int col = x0; col < x1; ++col) {
      line[col] = color;
    }
  }
}

void gfx_stroke(const UiSurface* surface, int x, int y, int w, int h,
                uint16_t color) {
  if (w <= 0 || h <= 0) {
    return;
  }
  gfx_fill(surface, x, y, w, 1, color);
  gfx_fill(surface, x, y + h - 1, w, 1, color);
  gfx_fill(surface, x, y, 1, h, color);
  gfx_fill(surface, x + w - 1, y, 1, h, color);
}

void gfx_dashed_hline(const UiSurface* surface, int x, int y, int w,
                      uint16_t color, int dash, int gap) {
  if (dash <= 0 || gap < 0) {
    return;
  }
  int pen = x;
  int end = x + w;
  while (pen < end) {
    int run = (pen + dash > end) ? (end - pen) : dash;
    gfx_fill(surface, pen, y, run, 1, color);
    pen += dash + gap;
  }
}

void gfx_round_fill(const UiSurface* surface, int x, int y, int w, int h, int r,
                    uint16_t color) {
  if (r <= 0 || w <= 0 || h <= 0) {
    gfx_fill(surface, x, y, w, h, color);
    return;
  }
  int radius = r;
  if (radius * 2 > w) {
    radius = w / 2;
  }
  if (radius * 2 > h) {
    radius = h / 2;
  }
  for (int row = 0; row < h; ++row) {
    int inset = 0;
    int dy = -1;
    if (row < radius) {
      dy = radius - 1 - row;
    } else if (row >= h - radius) {
      dy = row - (h - radius);
    }
    if (dy >= 0) {
      inset = radius - isqrt(radius * radius - dy * dy);
      if (inset < 0) {
        inset = 0;
      }
    }
    gfx_fill(surface, x + inset, y + row, w - inset * 2, 1, color);
  }
}

void gfx_grid(const UiSurface* surface, int x, int y, int w, int h,
              uint16_t color, int step) {
  if (step <= 0 || w <= 0 || h <= 0) {
    return;
  }
  for (int col = 0; col < w; col += step) {
    gfx_fill(surface, x + col, y, 1, h, color);
  }
  for (int row = 0; row < h; row += step) {
    gfx_fill(surface, x, y + row, w, 1, color);
  }
}

/** @brief 画一条 Bresenham 线段。 */
static void line(const UiSurface* surface, int x0, int y0, int x1, int y1,
                 uint16_t color) {
  int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
  int sx = (x0 < x1) ? 1 : -1;
  int dy = (y1 > y0) ? -(y1 - y0) : -(y0 - y1);
  int sy = (y0 < y1) ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    if (surface != NULL && x0 >= 0 && x0 < surface->width) {
      int row = strip_row(surface, y0);
      if (row >= 0) {
        surface->pixels[(size_t)row * (size_t)surface->width + (size_t)x0] =
            color;
      }
    }
    if (x0 == x1 && y0 == y1) {
      break;
    }
    int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void gfx_polyline(const UiSurface* surface, const int16_t* points, int count,
                  uint16_t color) {
  if (surface == NULL || points == NULL || count < 2) {
    return;
  }
  for (int i = 1; i < count; ++i) {
    line(surface, points[(i - 1) * 2], points[(i - 1) * 2 + 1], points[i * 2],
         points[i * 2 + 1], color);
  }
}

void gfx_bevel_top(const UiSurface* surface, int x, int y, int w, int h,
                   int cut, uint16_t color) {
  if (w <= 0 || h <= 0) {
    return;
  }
  if (cut <= 0) {
    gfx_fill(surface, x, y, w, h, color);
    return;
  }
  for (int row = 0; row < h; ++row) {
    int trim = (row < cut) ? (cut - row) : 0;
    gfx_fill(surface, x, y + row, w - trim, 1, color);
  }
}
