#include "chess_rules.h"
#include <string.h>

static int isign(int x) { return x > 0 ? 1 : (x < 0 ? -1 : 0); }
static int iabs(int x) { return x < 0 ? -x : x; }

static bool on_board(int r, int f) { return r >= 0 && r <= 9 && f >= 0 && f <= 8; }

static bool in_palace(int r, int f, int8_t color) {
    if (f < 3 || f > 5) return false;
    return color > 0 ? (r >= 0 && r <= 2) : (r >= 7 && r <= 9);
}

static bool on_own_side(int r, int8_t color) {
    return color > 0 ? r <= 4 : r >= 5;
}

static bool pawn_crossed(int r, int8_t color) {
    return color > 0 ? r >= 5 : r <= 4;
}

/* on board and empty or enemy of color */
static bool can_land(const chess_sq_t *b, int r, int f, int8_t color) {
    if (!on_board(r, f)) return false;
    chess_sq_t sq = b[r * 9 + f];
    return sq == 0 || ((sq > 0) != (color > 0));
}

static void push(chess_move_t *buf, int *n, int cap,
                 int fr, int ff, int tr, int tf) {
    if (*n < cap) {
        buf[*n].fr = (int8_t)fr; buf[*n].ff = (int8_t)ff;
        buf[*n].tr = (int8_t)tr; buf[*n].tf = (int8_t)tf;
        (*n)++;
    }
}

void chess_init(chess_sq_t board[90]) {
    memset(board, 0, 90);
    static const int back[9] = {5, 4, 3, 2, 1, 2, 3, 4, 5};
    for (int f = 0; f < 9; f++) {
        board[0 * 9 + f] = (int8_t)back[f];
        board[9 * 9 + f] = (int8_t)(-back[f]);
    }
    board[2 * 9 + 1] = 6;  board[2 * 9 + 7] = 6;
    board[7 * 9 + 1] = -6; board[7 * 9 + 7] = -6;
    for (int f = 0; f < 9; f += 2) {
        board[3 * 9 + f] = 7;
        board[6 * 9 + f] = -7;
    }
}

chess_sq_t chess_at(const chess_sq_t board[90], int r, int f) {
    return board[r * 9 + f];
}

int chess_gen_moves(const chess_sq_t board[90], int8_t color,
                    chess_move_t *buf, int cap) {
    int n = 0;
    for (int r = 0; r < 10; r++) {
        for (int f = 0; f < 9; f++) {
            chess_sq_t sq = board[r * 9 + f];
            if (sq == 0 || ((sq > 0) != (color > 0))) continue;
            switch (sq > 0 ? sq : -sq) {
            case 1: { /* KING */
                static const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                for (int i = 0; i < 4; i++) {
                    int nr = r + d[i][0], nf = f + d[i][1];
                    if (in_palace(nr, nf, color) && can_land(board, nr, nf, color))
                        push(buf, &n, cap, r, f, nr, nf);
                }
                break; }
            case 2: { /* ADVISOR */
                static const int d[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
                for (int i = 0; i < 4; i++) {
                    int nr = r + d[i][0], nf = f + d[i][1];
                    if (in_palace(nr, nf, color) && can_land(board, nr, nf, color))
                        push(buf, &n, cap, r, f, nr, nf);
                }
                break; }
            case 3: { /* ELEPHANT */
                static const int d[4][2] = {{2, 2}, {2, -2}, {-2, 2}, {-2, -2}};
                for (int i = 0; i < 4; i++) {
                    int er = r + isign(d[i][0]), ef = f + isign(d[i][1]);
                    int nr = r + d[i][0], nf = f + d[i][1];
                    if (!on_own_side(nr, color)) continue;
                    if (on_board(er, ef) && board[er * 9 + ef] == 0 &&
                        can_land(board, nr, nf, color))
                        push(buf, &n, cap, r, f, nr, nf);
                }
                break; }
            case 4: { /* HORSE */
                static const int d[8][2] = {{2, 1}, {2, -1}, {-2, 1}, {-2, -1},
                                            {1, 2}, {1, -2}, {-1, 2}, {-1, -2}};
                for (int i = 0; i < 8; i++) {
                    int lr = (iabs(d[i][0]) == 2) ? isign(d[i][0]) : 0;
                    int lf = (iabs(d[i][1]) == 2) ? isign(d[i][1]) : 0;
                    int leg_r = r + lr, leg_f = f + lf;
                    int nr = r + d[i][0], nf = f + d[i][1];
                    if (on_board(leg_r, leg_f) && board[leg_r * 9 + leg_f] == 0 &&
                        can_land(board, nr, nf, color))
                        push(buf, &n, cap, r, f, nr, nf);
                }
                break; }
            case 5: { /* CHARIOT */
                static const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                for (int i = 0; i < 4; i++) {
                    int nr = r + d[i][0], nf = f + d[i][1];
                    while (on_board(nr, nf)) {
                        chess_sq_t ts = board[nr * 9 + nf];
                        if (ts == 0) {
                            push(buf, &n, cap, r, f, nr, nf);
                        } else {
                            if ((ts > 0) != (color > 0))
                                push(buf, &n, cap, r, f, nr, nf);
                            break;
                        }
                        nr += d[i][0]; nf += d[i][1];
                    }
                }
                break; }
            case 6: { /* CANNON */
                static const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                for (int i = 0; i < 4; i++) {
                    int nr = r + d[i][0], nf = f + d[i][1];
                    int screen = 0;
                    while (on_board(nr, nf)) {
                        chess_sq_t ts = board[nr * 9 + nf];
                        if (!screen) {
                            if (ts == 0) push(buf, &n, cap, r, f, nr, nf);
                            else screen = 1;
                        } else if (ts != 0) {
                            if ((ts > 0) != (color > 0))
                                push(buf, &n, cap, r, f, nr, nf);
                            break;
                        }
                        nr += d[i][0]; nf += d[i][1];
                    }
                }
                break; }
            case 7: { /* PAWN */
                int fwd = color > 0 ? 1 : -1;
                if (can_land(board, r + fwd, f, color))
                    push(buf, &n, cap, r, f, r + fwd, f);
                if (pawn_crossed(r, color)) {
                    if (can_land(board, r, f + 1, color))
                        push(buf, &n, cap, r, f, r, f + 1);
                    if (can_land(board, r, f - 1, color))
                        push(buf, &n, cap, r, f, r, f - 1);
                }
                break; }
            default:
                break;
            }
        }
    }
    return n;
}

void chess_make_move(chess_sq_t board[90], chess_move_t m) {
    board[m.tr * 9 + m.tf] = board[m.fr * 9 + m.ff];
    board[m.fr * 9 + m.ff] = 0;
}

bool chess_kings_face(const chess_sq_t board[90]) {
    int rkr = -1, rkf = -1, bkr = -1, bkf = -1;
    for (int i = 0; i < 90; i++) {
        if (board[i] == 1) { rkr = i / 9; rkf = i % 9; }
        else if (board[i] == -1) { bkr = i / 9; bkf = i % 9; }
    }
    if (rkr < 0 || bkr < 0 || rkf != bkf) return false;
    int lo = rkr < bkr ? rkr : bkr, hi = rkr < bkr ? bkr : rkr;
    for (int r = lo + 1; r < hi; r++)
        if (board[r * 9 + rkf] != 0) return false;
    return true;
}

bool chess_in_check(const chess_sq_t board[90], int8_t color) {
    chess_sq_t own_k = color > 0 ? 1 : -1;
    int kr = -1, kf = -1;
    for (int i = 0; i < 90; i++)
        if (board[i] == own_k) { kr = i / 9; kf = i % 9; break; }
    if (kr < 0) return false;
    chess_move_t buf[CHESS_MAX_MOVES];
    int8_t enemy = color > 0 ? -1 : 1;
    int n = chess_gen_moves(board, enemy, buf, CHESS_MAX_MOVES);
    for (int i = 0; i < n; i++)
        if (buf[i].tr == kr && buf[i].tf == kf) return true;
    return chess_kings_face(board);
}

bool chess_is_legal(const chess_sq_t board[90], chess_move_t m, int8_t color) {
    chess_sq_t copy[90];
    memcpy(copy, board, 90);
    chess_make_move(copy, m);
    return !chess_in_check(copy, color);
}

bool chess_has_legal_move(const chess_sq_t board[90], int8_t to_move) {
    chess_move_t buf[CHESS_MAX_MOVES];
    int n = chess_gen_moves(board, to_move, buf, CHESS_MAX_MOVES);
    for (int i = 0; i < n; i++)
        if (chess_is_legal(board, buf[i], to_move)) return true;
    return false;
}
