#include "lunar_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 二进制表格式：10 字节头 + record_count 条 3 字节记录。 */
#define kLunarHeaderSize 10u
#define kLunarRecordSize 3u

/* lunar_service_init_from_file 的返回码。 */
enum {
  LUNAR_OK = 0,
  LUNAR_ERR_ARGUMENT = -1,
  LUNAR_ERR_OPEN = -2,
  LUNAR_ERR_READ = -3,
  LUNAR_ERR_ALLOC = -4,
  LUNAR_ERR_TABLE = -5
};

static const char kLunarMagic[4] = {'H', 'K', 'L', '1'};

static const char* const kStems[10] = {
    "甲", "乙", "丙", "丁", "戊", "己", "庚", "辛", "壬", "癸",
};

static const char* const kBranches[12] = {
    "子", "丑", "寅", "卯", "辰", "巳", "午", "未", "申", "酉", "戌", "亥",
};

static const char* const kMonthNames[13] = {
    "",     "正月", "二月", "三月", "四月", "五月", "六月",
    "七月", "八月", "九月", "十月", "冬月", "腊月",
};

static const char* const kDigits[10] = {
    "零", "一", "二", "三", "四", "五", "六", "七", "八", "九",
};

/* 当前生效的表。设备端指向 Flash，Host 端指向 malloc 的缓冲区。 */
static const uint8_t* s_table = NULL;
static size_t s_table_size = 0;
static uint16_t s_start_year = 0;
static uint32_t s_record_count = 0;
/* 仅由 lunar_service_init_from_file 拥有的缓冲区。 */
static uint8_t* s_owned_table = NULL;

typedef struct {
  uint16_t start_year;
  uint32_t record_count;
} LunarTableMeta;

static bool lunar_is_leap_year(int year) {
  return (year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0));
}

static int lunar_days_in_month(int year, int month) {
  static const uint8_t days_per_month[12] = {
      31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
  };

  if (month == 2 && lunar_is_leap_year(year)) {
    return 29;
  }
  return (int)days_per_month[month - 1];
}

static void lunar_reset(void) {
  free(s_owned_table);
  s_owned_table = NULL;
  s_table = NULL;
  s_table_size = 0;
  s_start_year = 0;
  s_record_count = 0;
}

/* 校验表头与整体长度，不改变当前状态。 */
static bool lunar_table_parse(const uint8_t* buffer, size_t size,
                              LunarTableMeta* meta) {
  if (buffer == NULL || meta == NULL || size < kLunarHeaderSize) {
    return false;
  }
  if (memcmp(buffer, kLunarMagic, sizeof(kLunarMagic)) != 0) {
    return false;
  }

  uint16_t start_year =
      (uint16_t)((uint16_t)buffer[4] | ((uint16_t)buffer[5] << 8));
  uint32_t record_count = (uint32_t)buffer[6] | ((uint32_t)buffer[7] << 8) |
                          ((uint32_t)buffer[8] << 16) |
                          ((uint32_t)buffer[9] << 24);
  if (start_year == 0 || record_count == 0) {
    return false;
  }
  uint64_t required =
      (uint64_t)kLunarHeaderSize + (uint64_t)record_count * kLunarRecordSize;
  if (required > (uint64_t)size) {
    return false;
  }

  meta->start_year = start_year;
  meta->record_count = record_count;
  return true;
}

static void lunar_commit(const uint8_t* buffer, size_t size,
                         const LunarTableMeta* meta) {
  lunar_reset();
  s_table = buffer;
  s_table_size = size;
  s_start_year = meta->start_year;
  s_record_count = meta->record_count;
}

/* 与归档 MicroPython 版的 _day_index 等价；失败返回 false。 */
static bool lunar_day_index(int year, int month, int day, int start_year,
                            uint32_t* out_index) {
  if (year < start_year || month < 1 || month > 12) {
    return false;
  }
  if (day < 1 || day > lunar_days_in_month(year, month)) {
    return false;
  }

  int64_t previous_year = (int64_t)year - 1;
  int64_t start_previous_year = (int64_t)start_year - 1;
  int64_t count = (int64_t)365 * ((int64_t)year - (int64_t)start_year);
  count += previous_year / 4 - start_previous_year / 4;
  count -= previous_year / 100 - start_previous_year / 100;
  count += previous_year / 400 - start_previous_year / 400;
  for (int month_number = 1; month_number < month; month_number++) {
    count += lunar_days_in_month(year, month_number);
  }
  count += day - 1;
  if (count < 0 || count > (int64_t)UINT32_MAX) {
    return false;
  }

  *out_index = (uint32_t)count;
  return true;
}

static void lunar_format_day(int day, char* buffer, size_t buffer_size) {
  if (day < 10) {
    (void)snprintf(buffer, buffer_size, "初%s", kDigits[day]);
  } else if (day == 10) {
    (void)snprintf(buffer, buffer_size, "初十");
  } else if (day < 20) {
    (void)snprintf(buffer, buffer_size, "十%s", kDigits[day - 10]);
  } else if (day == 20) {
    (void)snprintf(buffer, buffer_size, "二十");
  } else if (day < 30) {
    (void)snprintf(buffer, buffer_size, "廿%s", kDigits[day - 20]);
  } else {
    (void)snprintf(buffer, buffer_size, "三十");
  }
}

int lunar_service_init_from_file(const char* filepath) {
  if (filepath == NULL) {
    return LUNAR_ERR_ARGUMENT;
  }

  FILE* file = fopen(filepath, "rb");
  if (file == NULL) {
    return LUNAR_ERR_OPEN;
  }

  if (fseek(file, 0, SEEK_END) != 0) {
    (void)fclose(file);
    return LUNAR_ERR_READ;
  }
  long length = ftell(file);
  if (length < 0) {
    (void)fclose(file);
    return LUNAR_ERR_READ;
  }
  if (fseek(file, 0, SEEK_SET) != 0) {
    (void)fclose(file);
    return LUNAR_ERR_READ;
  }
  if ((unsigned long)length < kLunarHeaderSize) {
    (void)fclose(file);
    return LUNAR_ERR_TABLE;
  }

  uint8_t* buffer = (uint8_t*)malloc((size_t)length);
  if (buffer == NULL) {
    (void)fclose(file);
    return LUNAR_ERR_ALLOC;
  }
  size_t read_size = fread(buffer, 1, (size_t)length, file);
  int close_result = fclose(file);
  if (read_size != (size_t)length || close_result != 0) {
    free(buffer);
    return LUNAR_ERR_READ;
  }

  LunarTableMeta meta;
  if (!lunar_table_parse(buffer, (size_t)length, &meta)) {
    free(buffer);
    return LUNAR_ERR_TABLE;
  }

  lunar_commit(buffer, (size_t)length, &meta);
  s_owned_table = buffer;
  return LUNAR_OK;
}

void lunar_service_init_from_memory(const uint8_t* buffer, size_t size) {
  LunarTableMeta meta;
  if (lunar_table_parse(buffer, size, &meta)) {
    lunar_commit(buffer, size, &meta);
  } else {
    lunar_reset();
  }
}

bool lunar_service_lookup(int year, int month, int day, LunarDate* out_lunar) {
  if (out_lunar == NULL) {
    return false;
  }
  if (s_table == NULL || s_start_year == 0 || s_record_count == 0) {
    return false;
  }

  uint32_t index = 0;
  if (!lunar_day_index(year, month, day, (int)s_start_year, &index)) {
    return false;
  }
  if (index >= s_record_count) {
    return false;
  }

  size_t offset = kLunarHeaderSize + (size_t)index * kLunarRecordSize;
  if (offset + kLunarRecordSize > s_table_size) {
    return false;
  }
  const uint8_t* record = s_table + offset;

  int ganzhi_index = record[0];
  uint8_t month_flags = record[1];
  int lunar_month = month_flags & 0x0F;
  bool is_leap = (month_flags & 0x80u) != 0;
  int lunar_day = record[2];

  if (ganzhi_index >= 60 || (month_flags & 0x70u) != 0 || lunar_month < 1 ||
      lunar_month > 12 || lunar_day < 1 || lunar_day > 30) {
    return false;
  }

  out_lunar->ganzhi_index = ganzhi_index;
  (void)snprintf(out_lunar->ganzhi_year, sizeof(out_lunar->ganzhi_year),
                 "%s%s年", kStems[ganzhi_index % 10],
                 kBranches[ganzhi_index % 12]);
  out_lunar->lunar_month = lunar_month;
  out_lunar->is_leap = is_leap;
  out_lunar->lunar_day = lunar_day;

  char day_name[16];
  lunar_format_day(lunar_day, day_name, sizeof(day_name));
  (void)snprintf(out_lunar->lunar_month_day, sizeof(out_lunar->lunar_month_day),
                 "%s%s%s", is_leap ? "闰" : "", kMonthNames[lunar_month],
                 day_name);
  return true;
}
