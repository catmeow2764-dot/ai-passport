#ifndef CHESS_RULES_H
#define CHESS_RULES_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Xiangqi (Chinese chess) rules engine — pure logic, no LVGL/ESP-IDF deps.
 *
 * Board: 9 files (columns) x 10 ranks (rows), pieces sit on intersections.
 * Index = rank * 9 + file. rank 0 = Red back rank (Red attacks toward
 * higher rank). River between rank 4 and 5. Red palace file 3-5/rank 0-2,
 * Black palace file 3-5/rank 7-9.
 *
 * Square encoding (chess_sq_t): 0 = empty; +1..+7 = Red piece;
 * -1..-7 = Black piece. Sign = color, abs(value) = type.
 *
 * Type codes: 1 KING(帥/將) 2 ADVISOR(仕/士) 3 ELEPHANT(相/象)
 *             4 HORSE(傌/馬) 5 CHARIOT(俥/車) 6 CANNON(炮/砲) 7 PAWN(兵/卒)
 *
 * Pseudo-legal = follows piece movement (incl. captures), but ignores
 * "own king left in check" and the flying-general rule. chess_is_legal()
 * filters those. End: side to move with no legal move loses (covers
 * checkmate and stalemate; in Xiangqi stalemate is a loss).
 */

#define CHESS_RED   1
#define CHESS_BLACK (-1)
#define CHESS_MAX_MOVES 96

typedef int8_t chess_sq_t;
typedef struct {
    int8_t fr, ff;   /* from rank, file */
    int8_t tr, tf;   /* to rank, file */
} chess_move_t;

void       chess_init(chess_sq_t board[90]);
chess_sq_t chess_at(const chess_sq_t board[90], int r, int f);
int        chess_gen_moves(const chess_sq_t board[90], int8_t color,
                           chess_move_t *buf, int cap);
void       chess_make_move(chess_sq_t board[90], chess_move_t m);
bool       chess_in_check(const chess_sq_t board[90], int8_t color);
bool       chess_is_legal(const chess_sq_t board[90], chess_move_t m, int8_t color);
bool       chess_has_legal_move(const chess_sq_t board[90], int8_t to_move);

#endif /* CHESS_RULES_H */
