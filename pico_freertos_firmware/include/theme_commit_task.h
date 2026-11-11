/**
 * @file theme_commit_task.h
 * @brief 主题提交任务：把用户选中的主题写进 Flash 配置扇区。
 *
 * 任务与渲染任务分离：擦除期间双核中断关闭约 400 ms，若在渲染任务里做，
 * 画面会停在同一帧上更久，而且渲染任务本来只该碰 SPI 与快照。
 */

#ifndef PICO_FREERTOS_FIRMWARE_INCLUDE_THEME_COMMIT_TASK_H_
#define PICO_FREERTOS_FIRMWARE_INCLUDE_THEME_COMMIT_TASK_H_

#include <stdbool.h>

/**
 * @brief 用静态栈创建提交任务并绑定到 Core 0。
 *
 * 必须在 supervisor_start() 之后调用：创建失败时 main.c 要 panic，而
 * 只有监督任务已经在跑，panic 才有看门狗兜底。
 *
 * @return 任务创建成功返回 true。
 */
bool theme_commit_task_start(void);

#endif /* PICO_FREERTOS_FIRMWARE_INCLUDE_THEME_COMMIT_TASK_H_ */
