/**
 * @file theme_switch.c
 * @brief 单击推进主题并绕回：越界入参夹紧回默认深色。
 */

#include "theme_switch.h"

UiThemeId theme_switch_advance(UiThemeId current, bool clicked) {
  if (!clicked) {
    return current;
  }
  const unsigned id = (unsigned)current;
  if (id >= (unsigned)kThemeCount) {
    return kThemeM3Dark;
  }
  return (UiThemeId)((id + 1u) % (unsigned)kThemeCount);
}
