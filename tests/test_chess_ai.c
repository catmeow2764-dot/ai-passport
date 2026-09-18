#include <assert.h>
#include <string.h>
#include "chess_rules.h"
#include "chess_ai.h"

/* 把 90 格按 designated initializer 填好(src 必须是 90 项)。 */
static void load(chess_sq_t b[90], const chess_sq_t *src) {
    for (int i = 0; i < 90; i++) b[i] = src[i];
}

/* 返回的走法是否在 color 方的合法走法集合里。 */
static bool is_legal_choice(const chess_sq_t b[90], chess_move_t m, int8_t color) {
    chess_move_t buf[CHESS_MAX_MOVES];
    int n = chess_gen_moves(b, color, buf, CHESS_MAX_MOVES);
    for (int i = 0; i < n; i++)
        if (buf[i].fr == m.fr && buf[i].ff == m.ff &&
            buf[i].tr == m.tr && buf[i].tf == m.tf)
            return chess_is_legal(b, buf[i], color);
    return false;
}

int main(void) {
    chess_sq_t b[90];
    chess_move_t m;

    /* --- 1. 开局:返回的走法合法(红 depth 2 / 黑 depth 2)--- */
    chess_init(b);
    m = chess_ai_best_move(b, CHESS_RED, 2, 0);
    assert(is_legal_choice(b, m, CHESS_RED));
    /* 黑方:红先走一步炮,再让黑走 */
    chess_make_move(b, (chess_move_t){2, 1, 2, 4});
    m = chess_ai_best_move(b, CHESS_BLACK, 2, 0);
    assert(is_legal_choice(b, m, CHESS_BLACK));

    /* --- 2. 吃无保护车:红车(5,0) 可吃黑车(5,4),无防守 -> 必吃 --- */
    static const chess_sq_t hang[90] = {
        [0 * 9 + 3] = 1,    /* 红王(0,3) */
        [9 * 9 + 4] = -1,  /* 黑王(9,4) */
        [5 * 9 + 0] = 5,   /* 红车(5,0) */
        [5 * 9 + 4] = -5,  /* 黑车(5,4) 无防守 */
    };
    load(b, hang);
    m = chess_ai_best_move(b, CHESS_RED, 2, 0);
    assert(m.fr == 5 && m.ff == 0 && m.tr == 5 && m.tf == 4);  /* 吃黑车 */
    assert(is_legal_choice(b, m, CHESS_RED));

    /* --- 3. 杀棋一步:红车(8,3)->(9,3) 将死黑王(9,4) ---
       黑王逃路:(9,5)被(8,5)车封、(8,4)被(8,5)车封、(9,3)有(9,2)车守、(8,3)被(8,5)封。 */
    static const chess_sq_t mate1[90] = {
        [0 * 9 + 3] = 1,    /* 红王(0,3) */
        [9 * 9 + 4] = -1,   /* 黑王(9,4) */
        [8 * 9 + 3] = 5,    /* 红车(8,3) — 将杀走法:->(9,3) */
        [8 * 9 + 5] = 5,    /* 红车(8,5) — 封 (9,5)/(8,4)/(8,3) */
        [9 * 9 + 2] = 5,    /* 红车(9,2) — 守 (9,3) */
    };
    load(b, mate1);
    /* 先确认这个局面下确有杀棋走法(不是测试构造错了) */
    bool has_mate = false;
    {
        chess_move_t buf[CHESS_MAX_MOVES];
        int n = chess_gen_moves(b, CHESS_RED, buf, CHESS_MAX_MOVES);
        for (int i = 0; i < n; i++) {
            if (!chess_is_legal(b, buf[i], CHESS_RED)) continue;
            chess_sq_t c[90]; memcpy(c, b, 90);
            chess_make_move(c, buf[i]);
            if (!chess_has_legal_move(c, CHESS_BLACK)) { has_mate = true; break; }
        }
    }
    assert(has_mate);
    /* AI depth 2 应找到杀棋走法 */
    m = chess_ai_best_move(b, CHESS_RED, 2, 0);
    assert(is_legal_choice(b, m, CHESS_RED));
    chess_sq_t c[90]; memcpy(c, b, 90);
    chess_make_move(c, m);
    assert(!chess_has_legal_move(c, CHESS_BLACK));   /* 落子后黑无解 = 将杀 */

    /* --- 4. 超时:fallback 必返回合法走法(depth 8 + 1ms 限制)--- */
    chess_init(b);
    m = chess_ai_best_move(b, CHESS_RED, 8, 1);
    assert(is_legal_choice(b, m, CHESS_RED));

    /* --- 5. 确定性:同盘同 depth(time=0)两次结果一致 --- */
    chess_init(b);
    chess_move_t m1 = chess_ai_best_move(b, CHESS_RED, 2, 0);
    chess_move_t m2 = chess_ai_best_move(b, CHESS_RED, 2, 0);
    assert(m1.fr == m2.fr && m1.ff == m2.ff && m1.tr == m2.tr && m1.tf == m2.tf);

    /* --- 6. 估值:开局双方子力对称 -> 评估对红为 0(正负相抵)--- */
    chess_init(b);
    assert(chess_ai_eval(b, CHESS_RED) == 0);
    assert(chess_ai_eval(b, CHESS_BLACK) == 0);

    return 0;
}
