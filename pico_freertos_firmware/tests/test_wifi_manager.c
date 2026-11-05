#include "wifi_manager.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
  printf("== test_wifi_manager ==\n");

  assert(wifi_manager_backoff_delay_ms(0) == 1000u);
  assert(wifi_manager_backoff_delay_ms(1) == 3000u);
  assert(wifi_manager_backoff_delay_ms(2) == 10000u);
  assert(wifi_manager_backoff_delay_ms(3) == 30000u);
  assert(wifi_manager_backoff_delay_ms(4) == 30000u);
  assert(wifi_manager_backoff_delay_ms(100) == 30000u);

  /* 非法 attempt 也必须有确定值，不能读越界。 */
  assert(wifi_manager_backoff_delay_ms(-1) == 1000u);

  /* 宿主构建没有 CYW43，连接状态始终为 false。 */
  assert(!wifi_manager_is_connected());
  assert(!wifi_manager_is_network_ready());

  printf("  退避序列：1000/3000/10000/30000/30000 ms\n");
  printf("test_wifi_manager PASS\n");
  return 0;
}
