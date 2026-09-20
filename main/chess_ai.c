// main/chess_ai.c —— 中国象棋 AI:negamax + alpha-beta + quiescence + 迭代加深 + 硬时限。
#include "chess_ai.h"
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_timer.h"
static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
#else
#include <time.h>
static uint32_t now_ms(void) {
    clock_t c = clock();
    if (c < 0) c = 0;
    return (uint32_t)((uint64_t)c * 1000 / CLOCKS_PER_SEC);
}
#endif

#define MATE 100000
#define MATE_THRESH (MATE - 1000)

/* 子力价值(分),下标 = abs(类型)。王值极大,搜索中王不会被合法吃(被将即不合法)。 */
static const int PIECE_VAL[8] = {0, MATE, 200, 200, 400, 900, 450, 100};

/* 位置价值表(红方视角,下标 [type][r][f];r=0 红本方底线,r=9 黑对方底线)。
   黑方查表镜像:r→9-r, f→8-f。位置分 < material 1/3,避免 AI 为位置弃子。
   下标 0/1(空/将)无位置价值,全 0。 */
static const int8_t POS_VAL[8][10][9] = {
    [0] = {{0}},                                   /* 空 */
    [1] = {{0}},                                   /* 将:不动,无位置价值 */
    [2] = {{0,0,0,20,10,20,0,0,0},                 /* 仕:九宫角 */
           {0,0,0,10,20,10,0,0,0},
           {0,0,0,20,10,20,0,0,0},
           {0},{0},{0},{0},{0},{0},{0}},
    [3] = {{0,0,30,0,0,0,30,0,0},                  /* 相:本方相眼 */
           {0},
           {0,0,20,0,0,0,20,0,0},
           {0},
           {0,0,10,0,0,0,10,0,0},
           {0},{0},{0},{0},{0}},
    [4] = {{ 0,10,20,20,20,20,20,10, 0},           /* 马:中心强边角弱 */
           { 0,10,20,20,20,20,20,10, 0},
           {10,20,30,30,30,30,30,20,10},
           {20,30,40,50,50,50,40,30,20},
           {20,30,40,50,50,50,40,30,20},
           {20,30,40,50,50,50,40,30,20},
           {20,30,40,50,50,50,40,30,20},
           {10,20,30,30,30,30,30,20,10},
           { 0,10,20,20,20,20,20,10, 0},
           { 0,10,20,20,20,20,20,10, 0}},
    [5] = {{15,15,15,15,25,15,15,15,15},           /* 车:纵深 + 中线 */
           {15,15,15,15,25,15,15,15,15},
           {15,15,15,15,25,15,15,15,15},
           {15,15,15,15,25,15,15,15,15},
           {30,30,30,30,40,30,30,30,30},
           {45,45,45,45,55,45,45,45,45},
           {60,60,60,60,70,60,60,60,60},
           {75,75,75,75,85,75,75,75,75},
           {90,90,90,90,100,90,90,90,90},
           {90,90,90,90,100,90,90,90,90}},
    [6] = {{10,10,20,20,20,20,20,10,10},           /* 炮:中线/镇 */
           {10,10,20,20,20,20,20,10,10},
           {20,20,30,40,50,40,30,20,20},
           {20,20,30,40,40,40,30,20,20},
           {20,30,40,40,40,40,40,30,20},
           {20,30,30,30,30,30,30,30,20},
           {20,20,20,20,20,20,20,20,20},
           {20,20,30,40,50,40,30,20,20},
           {10,10,20,20,20,20,20,10,10},
           {10,10,20,20,20,20,20,10,10}},
    [7] = {{0},{0},{0},{0},{0},                     /* 兵:过河中线小加(r 推进已有) */
           {0,5,5,10,10,10,5,5,0},
           {0,5,5,10,10,10,5,5,0},
           {0,5,5,10,10,10,5,5,0},
           {0,5,5,10,10,10,5,5,0},
           {0,5,5,10,10,10,5,5,0}},
};

int chess_ai_eval(const chess_sq_t board[90], int8_t color) {
    int score = 0;
    for (int i = 0; i < 90; i++) {
        chess_sq_t s = board[i];
        if (s == 0) continue;
        int t = s > 0 ? s : -s;
        int r = i / 9, f = i % 9;
        int v = PIECE_VAL[t];
        if (t == 7) {                       /* 兵卒推进:过河加分 */
            if (s > 0) { if (r >= 5) v += 20 + (r - 4) * 5; }
            else      { if (r <= 4) v += 20 + (4 - r) * 5; }
        }
        int pr = (s > 0) ? r : (9 - r);     /* 位置:红方直查,黑方镜像 */
        int pf = (s > 0) ? f : (8 - f);
        v += POS_VAL[t][pr][pf];
        score += (s > 0) ? v : -v;
    }
    return (color > 0) ? score : -score;
}

/* ---- 棋盘小工具 ---- */
static int sgn(int x) { return x > 0 ? 1 : (x < 0 ? -1 : 0); }
static int absv(int x) { return x < 0 ? -x : x; }
static bool onb(int r, int f) { return r >= 0 && r <= 9 && f >= 0 && f <= 8; }

/* make/unmake:不拷盘,记录被吃子原地改/还原(搜索每节点省 90 字节拷贝)。 */
static inline chess_sq_t ai_make(chess_sq_t b[90], chess_move_t m) {
    int to = m.tr * 9 + m.tf, from = m.fr * 9 + m.ff;
    chess_sq_t cap = b[to];
    b[to] = b[from]; b[from] = 0;
    return cap;
}
static inline void ai_unmake(chess_sq_t b[90], chess_move_t m, chess_sq_t cap) {
    int to = m.tr * 9 + m.tf, from = m.fr * 9 + m.ff;
    b[from] = b[to]; b[to] = cap;
}

/* (r,f) 是否被 by 色攻击。不含将帅照面(照面由 chess_kings_face 另查,
   因照面只对"王被照"有意义)。覆盖:车直线、炮隔一子、马(查蹩马腿)、兵。 */
static bool is_attacked(const chess_sq_t b[90], int r, int f, int8_t by) {
    int8_t erook = (int8_t)(5 * by), ecannon = (int8_t)(6 * by);
    int8_t ehorse = (int8_t)(4 * by), epawn = (int8_t)(7 * by);
    static const int d4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

    for (int i = 0; i < 4; i++) {                  /* 车:正交第一个子是敌车 */
        int nr = r + d4[i][0], nf = f + d4[i][1];
        while (onb(nr, nf)) {
            chess_sq_t s = b[nr * 9 + nf];
            if (s != 0) { if (s == erook) return true; break; }
            nr += d4[i][0]; nf += d4[i][1];
        }
    }
    for (int i = 0; i < 4; i++) {                  /* 炮:正交隔一子后第二个子是敌炮 */
        int nr = r + d4[i][0], nf = f + d4[i][1];
        bool screen = false;
        while (onb(nr, nf)) {
            chess_sq_t s = b[nr * 9 + nf];
            if (!screen) { if (s != 0) screen = true; }
            else { if (s != 0) { if (s == ecannon) return true; break; } }
            nr += d4[i][0]; nf += d4[i][1];
        }
    }
    static const int hd[8][2] = {{2, 1}, {2, -1}, {-2, 1}, {-2, -1},
                                 {1, 2}, {1, -2}, {-1, 2}, {-1, -2}};
    for (int i = 0; i < 8; i++) {                  /* 马:马位有敌马且蹩马腿空 */
        int hr = r + hd[i][0], hf = f + hd[i][1];
        if (!onb(hr, hf) || b[hr * 9 + hf] != ehorse) continue;
        int lr = (absv(hd[i][0]) == 2) ? sgn(hd[i][0]) : 0;
        int lf = (absv(hd[i][1]) == 2) ? sgn(hd[i][1]) : 0;
        if (onb(r + lr, f + lf) && b[(r + lr) * 9 + (f + lf)] == 0)
            return true;
    }
    if (by > 0) {                                  /* 红兵向 +r,过河(r>=5)横移 */
        if (onb(r - 1, f) && b[(r - 1) * 9 + f] == epawn) return true;
        if (r >= 5) {
            if (onb(r, f - 1) && b[r * 9 + (f - 1)] == epawn) return true;
            if (onb(r, f + 1) && b[r * 9 + (f + 1)] == epawn) return true;
        }
    } else {                                       /* 黑卒向 -r,过河(r<=4)横移 */
        if (onb(r + 1, f) && b[(r + 1) * 9 + f] == epawn) return true;
        if (r <= 4) {
            if (onb(r, f - 1) && b[r * 9 + (f - 1)] == epawn) return true;
            if (onb(r, f + 1) && b[r * 9 + (f + 1)] == epawn) return true;
        }
    }
    return false;
}

/* color 方王是否安全(走完一手后调用):不被攻击 + 不与敌王照面。 */
static bool king_safe(const chess_sq_t b[90], int8_t color) {
    int8_t own_k = color > 0 ? 1 : -1;
    int kr = -1, kf = -1;
    for (int i = 0; i < 90; i++)
        if (b[i] == own_k) { kr = i / 9; kf = i % 9; break; }
    if (kr < 0) return false;
    return !is_attacked(b, kr, kf, (int8_t)(-color)) && !chess_kings_face(b);
}

/* 生成 color 方合法走法(伪合法过 king_safe 滤)。返回条数。 */
static int legal_moves(chess_sq_t b[90], int8_t color, chess_move_t *out) {
    chess_move_t tmp[CHESS_MAX_MOVES];
    int n = chess_gen_moves(b, color, tmp, CHESS_MAX_MOVES);
    int m = 0;
    for (int i = 0; i < n; i++) {
        chess_sq_t cap = ai_make(b, tmp[i]);
        bool ok = king_safe(b, color);
        ai_unmake(b, tmp[i], cap);
        if (ok) out[m++] = tmp[i];
    }
    return m;
}

static int capture_val(const chess_sq_t b[90], chess_move_t m) {
    chess_sq_t s = b[m.tr * 9 + m.tf];
    return s != 0 ? PIECE_VAL[s > 0 ? s : -s] : 0;
}

typedef struct { chess_move_t m; int score; } scored_t;

/* 吃子在前(MVV-LVA:被吃子价值高优先)。根层由 best_move 另做 PV-first。 */
static void order_moves(const chess_sq_t b[90], chess_move_t *mv, int n) {
    scored_t s[CHESS_MAX_MOVES];
    for (int i = 0; i < n; i++) { s[i].m = mv[i]; s[i].score = capture_val(b, mv[i]); }
    for (int i = 0; i < n; i++)            /* 选择排序降序(n≤96,够小) */
        for (int j = i + 1; j < n; j++)
            if (s[j].score > s[i].score) { scored_t t = s[i]; s[i] = s[j]; s[j] = t; }
    for (int i = 0; i < n; i++) mv[i] = s[i].m;
}

typedef struct {
    uint32_t start_ms;
    uint32_t limit_ms;
    bool stop;
} ai_ctx_t;

static bool time_up(const ai_ctx_t *c) {
    return c->limit_ms > 0 && (now_ms() - c->start_ms) >= c->limit_ms;
}

/* 静态搜索:只沿吃子继续搜,直到无吃子再评估,避免停在一串吃子中间。 */
static int quiescence(chess_sq_t b[90], int8_t color, int alpha, int beta,
                      int qply, ai_ctx_t *c) {
    if (c->stop || time_up(c)) { c->stop = true; return 0; }
    if (qply > 10) return chess_ai_eval(b, color);          /* 防御性深度上限 */
    int stand = chess_ai_eval(b, color);
    if (stand >= beta) return beta;
    if (stand > alpha) alpha = stand;

    chess_move_t tmp[CHESS_MAX_MOVES];
    int n = chess_gen_moves(b, color, tmp, CHESS_MAX_MOVES);
    chess_move_t caps[48];                                  /* 单局面吃子罕见超 48 */
    int m = 0;
    for (int i = 0; i < n; i++) {
        if (b[tmp[i].tr * 9 + tmp[i].tf] == 0) continue;    /* 非吃子 */
        chess_sq_t cap = ai_make(b, tmp[i]);
        bool ok = king_safe(b, color);
        ai_unmake(b, tmp[i], cap);
        if (!ok) continue;
        caps[m] = tmp[i];
        /* 粗排:被吃子价值高的先搜(就地冒泡,m 小) */
        int v = capture_val(b, caps[m]);
        int j = m;
        while (j > 0 && capture_val(b, caps[j - 1]) < v) {
            caps[j] = caps[j - 1]; j--;
        }
        caps[j] = tmp[i];
        m++;
    }
    for (int i = 0; i < m; i++) {
        chess_sq_t cap = ai_make(b, caps[i]);
        int score = -quiescence(b, (int8_t)(-color), -beta, -alpha, qply + 1, c);
        ai_unmake(b, caps[i], cap);
        if (c->stop) return 0;
        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }
    return alpha;
}

/* negamax + alpha-beta。从 color 视角的分数(正=有利 color)。 */
static int negamax(chess_sq_t b[90], int8_t color, int depth, int ply,
                   int alpha, int beta, ai_ctx_t *c) {
    if (c->stop || time_up(c)) { c->stop = true; return 0; }
    if (depth <= 0) return quiescence(b, color, alpha, beta, 0, c);

    chess_move_t mv[CHESS_MAX_MOVES];
    int n = legal_moves(b, color, mv);
    if (n == 0) return -MATE + ply;              /* 被将杀/无走法:负大分 */

    order_moves(b, mv, n);
    int best = -MATE - 1;
    for (int i = 0; i < n; i++) {
        chess_sq_t cap = ai_make(b, mv[i]);
        int score = -negamax(b, (int8_t)(-color), depth - 1, ply + 1, -beta, -alpha, c);
        ai_unmake(b, mv[i], cap);
        if (c->stop) return 0;
        if (score > best) best = score;
        if (best > alpha) alpha = best;
        if (alpha >= beta) break;               /* β 剪枝 */
    }
    return best;
}

chess_move_t chess_ai_best_move(const chess_sq_t board[90], int8_t color,
                                int max_depth, uint32_t time_limit_ms) {
    chess_sq_t b[90];
    memcpy(b, board, 90);
    ai_ctx_t c = {0};
    c.start_ms = now_ms();
    c.limit_ms = time_limit_ms;
    c.stop = false;

    chess_move_t legal[CHESS_MAX_MOVES];
    int n = legal_moves(b, color, legal);
    if (n == 0) { chess_move_t none = {0, 0, 0, 0}; return none; }  /* 防御,调用方不该到这 */
    chess_move_t best = legal[0];               /* fallback:任一合法走法 */

    for (int d = 1; d <= max_depth; d++) {
        n = legal_moves(b, color, legal);
        order_moves(b, legal, n);
        /* PV-first:把上一轮 best 挪到最前,提升 alpha-beta 剪枝 */
        for (int i = 0; i < n; i++)
            if (legal[i].fr == best.fr && legal[i].ff == best.ff &&
                legal[i].tr == best.tr && legal[i].tf == best.tf) {
                chess_move_t t = legal[0]; legal[0] = legal[i]; legal[i] = t;
                break;
            }
        int best_score = -MATE - 1;
        chess_move_t cur = legal[0];
        for (int i = 0; i < n; i++) {
            chess_sq_t cap = ai_make(b, legal[i]);
            int score = -negamax(b, (int8_t)(-color), d - 1, 1, -MATE - 1, MATE + 1, &c);
            ai_unmake(b, legal[i], cap);
            if (c.stop) break;                 /* 本层未搜完 -> 丢弃 */
            if (score > best_score) { best_score = score; cur = legal[i]; }
        }
        if (c.stop) break;                     /* 时间到,保留上一轮 best */
        best = cur;
        if (best_score >= MATE_THRESH) break;  /* 找到将杀,提前结束 */
    }
    return best;
}
