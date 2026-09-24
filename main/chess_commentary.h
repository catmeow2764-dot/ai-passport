#ifndef CHESS_COMMENTARY_H
#define CHESS_COMMENTARY_H

#include "chess_rules.h"

/* JEV 棋评:每回合 AI 走完/黑走完后,联网调 TypeSafe systemone API 做局面语义
 * 评估(谁占优/攻势/阶段),结果通过 callback 回传给 UI 显示。
 * 社区版(无 key)/断网/超时 → callback 回传"棋评不可用"。 */

typedef void (*commentary_cb_t)(const char *text, void *user);

/* 启动棋评任务(后等 commentary_request notify)。cb 在 LVGL 锁内调用 */
void commentary_init(commentary_cb_t cb, void *user);

/* 触发棋评:拷贝 board/turn/moves 到内部,notify task 异步处理 */
void commentary_request(const chess_sq_t *board, int8_t turn,
                        const chess_move_t *moves, int n);

/* 停止任务(demo exit 时调) */
void commentary_stop(void);

#endif
