/**
 * @file board_button.h
 * @brief 板载按键（GP15）的初始化与采样。
 *
 * 接线：GP15 与 GND 之间接一个轻触开关，芯片内部上拉；按下时读到低电平。
 * 采样由渲染任务的 20 ms 节拍驱动，消抖状态放在 src/logic/button_filter.c
 * （纯逻辑，宿主可测），本文件只负责「读电平」这一件硬件相关的事。
 */

#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_BOARD_BUTTON_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_BOARD_BUTTON_H_

#include <stdbool.h>

#include "ui_input.h"

/**
 * @brief 把 GP15 配成带上拉的输入。
 *
 * 必须在调度器启动前调用：上电瞬间的毛刺由消抖逻辑吸收，这里不做延时。
 */
void board_button_init(void);

/**
 * @brief 采样一次按键电平并喂给消抖器。
 *
 * @return 本次采样构成一次「按下」事件时返回 true。
 */
bool board_button_poll_click(void);

/** @brief 渲染任务使用的输入源，内部指向 board_button_poll_click()。 */
extern const UiInputSource kBoardInput;

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_BOARD_BUTTON_H_ */
