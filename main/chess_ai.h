#ifndef CHESS_AI_H
#define CHESS_AI_H

/*
 * Xiangqi AI search — pure logic, no LVGL/ESP-IDF deps (millis 来自 clock/esp_timer,
 * 用 ESP_PLATFORM 宏分流)。复用 chess_rules 的走法生成/落子;自带快速 is_attacked
 * 做"走后己方王是否被将"判定(比 chess_in_check 的全量敌方走法生成快一个量级)。
 *
 * 算法:negamax + alpha-beta + 静态搜索(quiescence,避免停在一串吃子中间)
 *      + 迭代加深 + 硬时限 + 走法排序(吃子在前 MVV-LVA,根层 PV-first)。
 *      估值 = 子力 + 兵卒推进。
 *
 * 时限语义:每完整搜完一层才更新最佳;时间到点丢弃当前未搜完层、返回上一
 * 已搜完层最佳。搜索前预置一个合法走法作 fallback,故任何超时都返回合法走法。
 */
#include "chess_rules.h"
#include <stdint.h>

/* 返回 color 方在 board 上的最佳走法(保证合法)。
   max_depth:迭代加深的最大层(1..N);time_limit_ms:硬时限,>0 到点即停,0=不限。
   调用方需先确保 color 有合法走法(将死/困毙局面不应调用本函数)。 */
chess_move_t chess_ai_best_move(const chess_sq_t board[90], int8_t color,
                                int max_depth, uint32_t time_limit_ms);

/* 估值(调试/测试用):board 对 color 方的分数(子力 + 兵卒推进,王值极大)。 */
int chess_ai_eval(const chess_sq_t board[90], int8_t color);

#endif /* CHESS_AI_H */
