#ifndef CHESS_MASTER_KEY_H
#define CHESS_MASTER_KEY_H

/* 棋评 TypeSafe API key 管理:
 * - 占位空串进 git,社区版编译用(棋评显示"棋评不可用")
 * - 真 key 放 chess_master_key_secret.h(gitignore),自编译版本地 include
 * - __has_include 自动检测 secret.h 存在则 include,否则占位空 */
#ifndef MASTER_AI_KEY
#  if __has_include("chess_master_key_secret.h")
#    include "chess_master_key_secret.h"
#  else
#    define MASTER_AI_KEY ""
#  endif
#endif

#include <stdbool.h>

/* 真 key 非空且长度 >10 视为合法(sk-... 通常 >10) */
static inline bool master_key_is_valid(void)
{
    const char *k = MASTER_AI_KEY;
    if (!k || !*k) return false;
    int n = 0;
    while (k[n]) n++;
    return n > 10;
}

#endif
