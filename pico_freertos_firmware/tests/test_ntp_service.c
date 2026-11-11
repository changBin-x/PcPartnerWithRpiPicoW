#include "ntp_service.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void expect_unix(uint32_t ntp_seconds, uint32_t expected_unix) {
  uint32_t actual = ntp_service_unix_from_ntp(ntp_seconds);
  printf("  NTP %10u -> Unix %10u\n", (unsigned)ntp_seconds, (unsigned)actual);
  assert(actual == expected_unix);
}

static void expect_clock(uint32_t unix_seconds, int year, int month, int day,
                         int hour, int minute, int second, int weekday) {
  ClockData clock;
  memset(&clock, 0, sizeof(clock));
  assert(ntp_service_civil_from_unix(unix_seconds, &clock));
  assert(clock.year == year);
  assert(clock.month == month);
  assert(clock.day == day);
  assert(clock.hour == hour);
  assert(clock.minute == minute);
  assert(clock.second == second);
  assert(clock.weekday == weekday);
  assert(clock.time_synced);
  printf("  Unix %10u -> %04d-%02d-%02d %02d:%02d:%02d weekday=%d\n",
         (unsigned)unix_seconds, clock.year, clock.month, clock.day, clock.hour,
         clock.minute, clock.second, clock.weekday);
}

static void test_parse_packet(void) {
  uint8_t packet[48];
  uint32_t seconds = 0;

  memset(packet, 0, sizeof(packet));
  packet[0] = 0x24; /* LI = 0，版本 4，模式 4（服务器）。 */
  packet[1] = 2;
  packet[40] = 0x12;
  packet[41] = 0x34;
  packet[42] = 0x56;
  packet[43] = 0x78;
  assert(ntp_service_parse_packet(packet, sizeof(packet), &seconds));
  assert(seconds == 0x12345678u);

  /* 闰秒标志为 3 的包不可信。 */
  packet[0] = 0xE4;
  assert(!ntp_service_parse_packet(packet, sizeof(packet), &seconds));

  /* 模式不是服务器响应。 */
  packet[0] = 0x23;
  assert(!ntp_service_parse_packet(packet, sizeof(packet), &seconds));

  /* stratum 越界。 */
  packet[0] = 0x24;
  packet[1] = 0;
  assert(!ntp_service_parse_packet(packet, sizeof(packet), &seconds));
  packet[1] = 16;
  assert(!ntp_service_parse_packet(packet, sizeof(packet), &seconds));

  /* 传输时间戳为 0 的包必须拒绝。 */
  packet[1] = 2;
  packet[40] = 0;
  packet[41] = 0;
  packet[42] = 0;
  packet[43] = 0;
  assert(!ntp_service_parse_packet(packet, sizeof(packet), &seconds));

  /* 长度不足、空指针。 */
  packet[40] = 0x12;
  packet[41] = 0x34;
  packet[42] = 0x56;
  packet[43] = 0x78;
  assert(!ntp_service_parse_packet(packet, 47, &seconds));
  assert(!ntp_service_parse_packet(NULL, sizeof(packet), &seconds));
  assert(!ntp_service_parse_packet(packet, sizeof(packet), NULL));

  printf("  NTP 响应校验/解析通过\n");
}

int main(void) {
  printf("== test_ntp_service ==\n");

  expect_unix((uint32_t)3913056000ULL, 1704067200u); /* 2024-01-01 00:00:00 */
  expect_unix((uint32_t)3918198896ULL, 1709210096u); /* 2024-02-29 12:34:56 */
  expect_unix((uint32_t)3160771200ULL, 951782400u);  /* 2000-02-29 00:00:00 */
  expect_unix((uint32_t)4356472447ULL, 2147483647u); /* 2038-01-19 03:14:07 */
  expect_unix((uint32_t)4356472448ULL, 2147483648u); /* 2038-01-19 03:14:08 */
  expect_unix((uint32_t)4294955648ULL, 2085966848u); /* 2036-02-07 03:14:08 */
  expect_unix(0u, 2085978496u);          /* 2036-02-07 06:28:16，回绕点 */
  expect_unix(0xFFFFFFFFu, 2085978495u); /* 回绕前一秒 */

  expect_clock(0u, 1970, 1, 1, 0, 0, 0, 4);
  expect_clock(951782400u, 2000, 2, 29, 0, 0, 0, 2);
  expect_clock(1704067200u, 2024, 1, 1, 0, 0, 0, 1);
  expect_clock(1709210096u, 2024, 2, 29, 12, 34, 56, 4);
  expect_clock(2147483647u, 2038, 1, 19, 3, 14, 7, 2);
  expect_clock(2147483648u, 2038, 1, 19, 3, 14, 8, 2);
  expect_clock(2085966848u, 2036, 2, 7, 3, 14, 8, 4);
  expect_clock(2085978496u, 2036, 2, 7, 6, 28, 16, 4);
  expect_clock((uint32_t)4107456000ULL, 2100, 2, 28, 0, 0, 0, 0);
  expect_clock((uint32_t)4107542400ULL, 2100, 3, 1, 0, 0, 0, 1);

  test_parse_packet();

  /* 显示时间固定为东八区：UTC 00:00 对应本地 08:00。 */
  assert(ntp_service_local_unix(1704067200u) == 1704096000u);
  ClockData local_clock;
  memset(&local_clock, 0, sizeof(local_clock));
  assert(ntp_service_civil_from_unix(ntp_service_local_unix(1709210096u),
                                     &local_clock));
  assert(local_clock.year == 2024 && local_clock.month == 2 &&
         local_clock.day == 29 && local_clock.hour == 20 &&
         local_clock.minute == 34 && local_clock.second == 56);
  printf("  UTC -> 东八区显示换算通过\n");

  /* 空输出指针必须安全返回 false。 */
  assert(!ntp_service_civil_from_unix(0u, NULL));
  assert(ntp_service_weekday(0u) == 4);
  assert(ntp_service_weekday(1704067200u) == 1);

  printf("test_ntp_service PASS\n");
  return 0;
}
