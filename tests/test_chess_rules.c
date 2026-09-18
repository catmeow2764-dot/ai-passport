#include <assert.h>
#include "chess_rules.h"

static bool has(const chess_move_t *buf, int n,
                int fr, int ff, int tr, int tf) {
    for (int i = 0; i < n; i++)
        if (buf[i].fr == fr && buf[i].ff == ff &&
            buf[i].tr == tr && buf[i].tf == tf) return true;
    return false;
}

static int clear(chess_sq_t b[90], const chess_sq_t *src) {
    for (int i = 0; i < 90; i++) b[i] = src[i];
    return 0;
}

int main(void) {
    chess_sq_t b[90];
    chess_move_t buf[CHESS_MAX_MOVES];
    int n;

    /* --- init position --- */
    chess_init(b);
    assert(chess_at(b, 0, 4) == 1);    /* red king */
    assert(chess_at(b, 9, 4) == -1);   /* black king */
    assert(chess_at(b, 0, 0) == 5);    /* red chariot */
    assert(chess_at(b, 0, 8) == 5);
    assert(chess_at(b, 2, 1) == 6);     /* red cannon */
    assert(chess_at(b, 2, 7) == 6);
    assert(chess_at(b, 7, 1) == -6);
    assert(chess_at(b, 3, 0) == 7);    /* red pawn */
    assert(chess_at(b, 6, 0) == -7);
    assert(chess_at(b, 5, 0) == 0);     /* empty river */

    /* --- red start move count: 5 pawn + 24 cannon (incl. 2 long
           captures of the corner horses over the enemy cannon) +
           4 horse + 4 elephant + 2 advisor + 1 king + 4 chariot = 44 --- */
    n = chess_gen_moves(b, CHESS_RED, buf, CHESS_MAX_MOVES);
    assert(n == 44);

    /* pawn: forward only at start */
    assert(has(buf, n, 3, 0, 4, 0));
    assert(!has(buf, n, 3, 0, 3, 1));   /* not sideways (not crossed) */
    /* cannon: slide + cannot capture with no screen */
    assert(has(buf, n, 2, 1, 2, 6));    /* slide to empty */
    assert(!has(buf, n, 2, 1, 7, 1));   /* capture needs a screen */

    /* --- cannon capture with exactly one screen --- */
    static const chess_sq_t cannon_pos[90] = {
        [2 * 9 + 1] = 6,   /* red cannon */
        [4 * 9 + 1] = 7,   /* screen (red pawn) */
        [6 * 9 + 1] = -7,  /* black target */
    };
    clear(b, cannon_pos);
    n = chess_gen_moves(b, CHESS_RED, buf, CHESS_MAX_MOVES);
    assert(has(buf, n, 2, 1, 6, 1));    /* capture over one screen */
    assert(has(buf, n, 2, 1, 3, 1));    /* non-capture before screen */
    assert(!has(buf, n, 2, 1, 4, 1));   /* cannot land on screen (friend) */
    assert(!has(buf, n, 2, 1, 5, 1));    /* cannot stop beyond screen */
    /* two screens -> no capture */
    b[5 * 9 + 1] = 7;
    n = chess_gen_moves(b, CHESS_RED, buf, CHESS_MAX_MOVES);
    assert(!has(buf, n, 2, 1, 6, 1));
    assert(has(buf, n, 2, 1, 3, 1));

    /* --- horse leg block (蹩马腿) --- */
    static const chess_sq_t horse_pos[90] = { [0 * 9 + 1] = 4 };
    clear(b, horse_pos);
    n = chess_gen_moves(b, CHESS_RED, buf, CHESS_MAX_MOVES);
    assert(has(buf, n, 0, 1, 2, 0));
    assert(has(buf, n, 0, 1, 2, 2));
    b[1 * 9 + 1] = 7;   /* block the leg */
    n = chess_gen_moves(b, CHESS_RED, buf, CHESS_MAX_MOVES);
    assert(!has(buf, n, 0, 1, 2, 0));
    assert(!has(buf, n, 0, 1, 2, 2));

    /* --- elephant eye block (塞象眼) + river --- */
    static const chess_sq_t ele_pos[90] = { [0 * 9 + 2] = 3 };
    clear(b, ele_pos);
    n = chess_gen_moves(b, CHESS_RED, buf, CHESS_MAX_MOVES);
    assert(has(buf, n, 0, 2, 2, 0));
    assert(has(buf, n, 0, 2, 2, 4));
    b[1 * 9 + 3] = 7;   /* block eye for (2,4) */
    n = chess_gen_moves(b, CHESS_RED, buf, CHESS_MAX_MOVES);
    assert(!has(buf, n, 0, 2, 2, 4));
    assert(has(buf, n, 0, 2, 2, 0));

    /* --- pawn crossed river: sideways yes, backward no --- */
    static const chess_sq_t pawn_pos[90] = { [5 * 9 + 0] = 7 };
    clear(b, pawn_pos);
    n = chess_gen_moves(b, CHESS_RED, buf, CHESS_MAX_MOVES);
    assert(has(buf, n, 5, 0, 6, 0));    /* forward */
    assert(has(buf, n, 5, 0, 5, 1));    /* sideways (crossed) */
    assert(!has(buf, n, 5, 0, 4, 0));    /* no backward */

    /* --- king confined to palace --- */
    static const chess_sq_t king_pos[90] = { [0 * 9 + 4] = 1 };
    clear(b, king_pos);
    n = chess_gen_moves(b, CHESS_RED, buf, CHESS_MAX_MOVES);
    assert(has(buf, n, 0, 4, 1, 4));
    assert(!has(buf, n, 0, 4, 0, 6));    /* out of palace */
    assert(!has(buf, n, 0, 4, -1, 4));    /* off board */

    /* --- flying general (将帅照面) --- */
    static const chess_sq_t face_pos[90] = {
        [0 * 9 + 4] = 1, [9 * 9 + 4] = -1,
    };
    clear(b, face_pos);
    assert(chess_in_check(b, CHESS_RED));
    assert(chess_in_check(b, CHESS_BLACK));
    b[5 * 9 + 4] = 7;   /* interpose -> no longer facing */
    assert(!chess_in_check(b, CHESS_RED));
    assert(!chess_in_check(b, CHESS_BLACK));

    /* --- in_check by chariot, blocked by interposition --- */
    static const chess_sq_t chk_pos[90] = {
        [0 * 9 + 4] = 1, [3 * 9 + 4] = -5,
    };
    clear(b, chk_pos);
    assert(chess_in_check(b, CHESS_RED));
    b[2 * 9 + 4] = 7;   /* block */
    assert(!chess_in_check(b, CHESS_RED));

    /* --- is_legal: discovered self-check exposure --- */
    /* red king (0,4), red chariot blocker (2,4), black chariot (5,4) */
    static const chess_sq_t disc_pos[90] = {
        [0 * 9 + 4] = 1, [2 * 9 + 4] = 5, [5 * 9 + 4] = -5,
    };
    clear(b, disc_pos);
    chess_move_t expose = {2, 4, 2, 3};      /* move blocker off file 4 */
    chess_move_t keep   = {2, 4, 1, 4};      /* stay on file 4 */
    assert(!chess_is_legal(b, expose, CHESS_RED));
    assert(chess_is_legal(b, keep, CHESS_RED));

    /* --- make_move --- */
    chess_init(b);
    chess_move_t pm = {3, 0, 4, 0};
    chess_make_move(b, pm);
    assert(chess_at(b, 4, 0) == 7);
    assert(chess_at(b, 3, 0) == 0);

    /* --- has_legal_move: start has; a constructed mate has none --- */
    chess_init(b);
    assert(chess_has_legal_move(b, CHESS_RED));
    /* red king (0,4) mated by black chariots on (0,3),(0,5),(1,4),(3,4) */
    static const chess_sq_t mate_pos[90] = {
        [0 * 9 + 4] = 1,
        [0 * 9 + 3] = -5, [0 * 9 + 5] = -5,
        [1 * 9 + 4] = -5, [3 * 9 + 4] = -5,
    };
    clear(b, mate_pos);
    assert(!chess_has_legal_move(b, CHESS_RED));

    return 0;
}
