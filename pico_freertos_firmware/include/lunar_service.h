#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_LUNAR_SERVICE_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_LUNAR_SERVICE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  int ganzhi_index;         /* 0..59 */
  char ganzhi_year[16];     /* 例如 "丙午年" */
  int lunar_month;          /* 1..12 */
  bool is_leap;             /* 是否闰月 */
  int lunar_day;            /* 1..30 */
  char lunar_month_day[32]; /* 例如 "九月廿八" 或 "闰二月初二" */
} LunarDate;

/* 从文件加载表（Host 测试用）。返回 0 成功，负数为错误。
 * 成功后表内容归本模块所有；再次初始化（文件或内存）会释放该缓冲区。
 * 失败时不改变已加载的表。 */
int lunar_service_init_from_file(const char* filepath);
/* 从内存加载表（设备端直接用 Flash 内嵌指针）。
 * 缓冲区的生命周期必须覆盖后续所有查询；无效表会让查询返回 false。
 * 若此前通过 lunar_service_init_from_file 加载过表，其缓冲区会被释放。 */
void lunar_service_init_from_memory(const uint8_t* buffer, size_t size);
/* 查询公历日期对应的农历。越界/数据无效返回 false。 */
bool lunar_service_lookup(int year, int month, int day, LunarDate* out_lunar);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_LUNAR_SERVICE_H_ */
