// main/demo_chess.c —— 中国象棋:双人/单人(三档 AI)+ 选先后手 + 走子/被吃动画
// + 胜负画面 + 将军提示。规则由 chess_rules 驱动;AI 由 chess_ai(negamax+alpha-beta+
// quiescence+迭代加深+硬时限)在独立 FreeRTOS 任务里搜,搜完取 LVGL 锁落子。
#include "demo.h"
#include "bsp_display.h"
#include "ui_pixel.h"
#include "chess_rules.h"
#include "chess_ai.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdlib.h>
#include <string.h>

extern const lv_font_t chess_cjk_14;
extern const lv_font_t chess_cjk_24;

#define CHESS_PITCH  24
#define CHESS_X0     24
#define CHESS_Y_TOP  55
#define CHESS_DISC   18
#define CHESS_RING   20
#define CHESS_RING_CURSOR 24
#define CHESS_ANIM_MS 150
#define CHESS_ANIM_T 1024
#define CHESS_HL_SELF   0x12A050
#define CHESS_HL_TARGET 0xE8B600
#define AI_TASK_STACK  20480
#define AI_STOP_TIMEOUT_MS 2000
#define CHESS_CLOCK_SECS  600          /* 10:00/方 */

typedef enum { CHESS_MODE_TWO, CHESS_MODE_AI_EASY, CHESS_MODE_AI_NORMAL, CHESS_MODE_AI_HARD } chess_mode_t;
typedef enum {
    CHESS_STATE_MODE_SELECT, CHESS_STATE_DIFF_SELECT, CHESS_STATE_SIDE_SELECT,
    CHESS_STATE_IDLE, CHESS_STATE_SELECTED, CHESS_STATE_OVER
} chess_state_t;

typedef struct { int8_t fr, ff, tr, tf, captured; } chess_hist_t;

static lv_obj_t *s_scr;
static chess_sq_t s_board[90];
static int8_t s_turn;
static lv_obj_t *s_turn_lbl;

static chess_state_t s_state;
static int8_t s_cur_r, s_cur_f;
static int8_t s_sel_r, s_sel_f;
static int s_sel_idx;
static chess_move_t s_targets[CHESS_MAX_MOVES];
static int s_target_count;
static lv_obj_t *s_pieces[90];
static lv_obj_t *s_cursor_ring;
static lv_obj_t *s_sel_ring;
static lv_obj_t *s_hints[CHESS_MAX_MOVES];

static chess_hist_t s_history[128];
static int s_hist_count;

static bool     s_animating;
static lv_obj_t *s_anim_piece, *s_anim_captured;
static int32_t  s_anim_from_x, s_anim_from_y, s_anim_to_x, s_anim_to_y;
static chess_move_t s_anim_move;
static lv_obj_t *s_win_overlay, *s_win_label, *s_win_hint;
static int s_clock[2];                 /* [0]=红 [1]=黑 剩余秒 */
static lv_obj_t *s_clk_lbl[2];          /* 左红 右黑 时钟标签 */
static lv_timer_t *s_clock_timer;

/* 模式/先后手/AI 任务 */
static chess_mode_t s_mode;
static int8_t s_human_color;
static TaskHandle_t s_ai_task;
static SemaphoreHandle_t s_ai_stopped;
static volatile bool s_ai_cancel;
static volatile bool s_ai_busy;

/* 模式选择屏 */
static lv_obj_t *s_menu_panel;
static lv_obj_t *s_menu_items[4];
static int s_menu_idx;

static const char *const MODE_OPTS[2]  = {"双人对战", "单人对战"};
static const char *const DIFF_OPTS[4]  = {"初级", "中级", "高级", "返回"};
static const char *const SIDE_OPTS[3]  = {"你执红", "你执黑", "返回"};

static int px_x(int f) { return CHESS_X0 + f * CHESS_PITCH; }
static int px_y(int r) { return CHESS_Y_TOP + (9 - r) * CHESS_PITCH; }

static bool is_own(chess_sq_t sq, int8_t color) {
    return sq != 0 && ((sq > 0) == (color > 0));
}
static int next_own(int from_idx, int8_t color) {
    for (int i = 1; i <= 90; i++) {
        int idx = (from_idx + i) % 90;
        if (is_own(s_board[idx], color)) return idx;
    }
    return -1;
}
static int prev_own(int from_idx, int8_t color) {
    for (int i = 1; i <= 90; i++) {
        int idx = (from_idx - i + 90) % 90;
        if (is_own(s_board[idx], color)) return idx;
    }
    return -1;
}
static void to_foreground(lv_obj_t *o) {
    lv_obj_t *p = lv_obj_get_parent(o);
    if (p) lv_obj_move_to_index(o, (int32_t)lv_obj_get_child_count(p) - 1);
}

static lv_obj_t *chess_rect(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}
static lv_obj_t *make_ring(lv_obj_t *parent, int8_t r, int8_t f,
                           uint32_t color, int border_w, int size)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, px_x(f) - size / 2, px_y(r) - size / 2);
    lv_obj_set_size(o, size, size);
    lv_obj_set_style_radius(o, size / 2, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, border_w, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    return o;
}
static lv_obj_t *make_dot(lv_obj_t *parent, int8_t r, int8_t f, uint32_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, px_x(f) - 3, px_y(r) - 3);
    lv_obj_set_size(o, 6, 6);
    lv_obj_set_style_radius(o, 3, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    return o;
}
static void move_ring(lv_obj_t *ring, int8_t r, int8_t f) {
    if (!ring) return;
    lv_obj_set_pos(ring, px_x(f) - CHESS_RING_CURSOR / 2, px_y(r) - CHESS_RING_CURSOR / 2);
    to_foreground(ring);
}
static const char *piece_char(int8_t sq)
{
    static const char *red[]   = {"", "帥", "仕", "相", "傌", "俥", "炮", "兵"};
    static const char *black[] = {"", "將", "士", "象", "馬", "車", "砲", "卒"};
    int t = sq > 0 ? sq : -sq;
    if (t < 1 || t > 7) return "";
    return sq > 0 ? red[t] : black[t];
}

static void draw_grid(lv_obj_t *parent)
{
    chess_rect(parent, CHESS_X0 - 8, CHESS_Y_TOP - 8,
               8 * CHESS_PITCH + 16, 9 * CHESS_PITCH + 16, UI_PAPER);
    for (int r = 0; r <= 9; r++)
        chess_rect(parent, CHESS_X0, px_y(r) - 1, 8 * CHESS_PITCH, 2, UI_INK);
    for (int f = 0; f <= 8; f++)
        chess_rect(parent, px_x(f) - 1, CHESS_Y_TOP, 2, 9 * CHESS_PITCH, UI_INK);
}
static lv_obj_t *make_piece(lv_obj_t *parent, int8_t sq, int8_t r, int8_t f)
{
    uint32_t bg = sq > 0 ? UI_RED : UI_INK;
    lv_obj_t *p = lv_label_create(parent);
    lv_label_set_text(p, piece_char(sq));
    lv_label_set_long_mode(p, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_pos(p, px_x(f) - CHESS_DISC / 2, px_y(r) - CHESS_DISC / 2);
    lv_obj_set_size(p, CHESS_DISC, CHESS_DISC);
    lv_obj_set_style_radius(p, CHESS_DISC / 2, 0);
    lv_obj_set_style_bg_color(p, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(p, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_border_width(p, 1, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_set_style_pad_top(p, (CHESS_DISC - chess_cjk_14.line_height) / 2, 0);
    lv_obj_set_style_pad_bottom(p, (CHESS_DISC - chess_cjk_14.line_height) / 2, 0);
    lv_obj_set_style_text_font(p, &chess_cjk_14, 0);
    lv_obj_set_style_text_color(p, lv_color_hex(UI_PAPER), 0);
    lv_obj_set_style_text_align(p, LV_TEXT_ALIGN_CENTER, 0);
    return p;
}

static void draw_pieces(lv_obj_t *parent)
{
    for (int r = 0; r <= 9; r++) {
        for (int f = 0; f <= 8; f++) {
            int idx = r * 9 + f;
            int8_t sq = s_board[idx];
            if (sq == 0) { s_pieces[idx] = NULL; continue; }
            s_pieces[idx] = make_piece(parent, sq, (int8_t)r, (int8_t)f);
        }
    }
}

static void update_turn(void)
{
    if (!s_turn_lbl) return;
    const char *t;
    if (s_ai_busy) {
        t = (s_turn == CHESS_RED) ? "红方思考中" : "黑方思考中";
    } else if (s_state == CHESS_STATE_OVER) {
        t = (s_turn == CHESS_RED) ? "黑方胜" : "红方胜";
    } else if (chess_in_check(s_board, s_turn)) {
        t = (s_turn == CHESS_RED) ? "红方被将" : "黑方被将";
    } else {
        t = (s_turn == CHESS_RED) ? "红方走棋" : "黑方走棋";
    }
    lv_label_set_text(s_turn_lbl, t);
    lv_obj_align(s_turn_lbl, LV_ALIGN_BOTTOM_MID, 0, -8);
}

static void update_clock(void)
{
    char buf[6];
    for (int i = 0; i < 2; i++) {
        int s = s_clock[i];
        buf[0] = (char)('0' + s / 600);
        buf[1] = (char)('0' + (s / 60) % 10);
        buf[2] = ':';
        buf[3] = (char)('0' + (s % 60) / 10);
        buf[4] = (char)('0' + s % 10);
        buf[5] = 0;
        if (s_clk_lbl[i]) lv_label_set_text(s_clk_lbl[i], buf);
    }
}

static void show_win(int8_t loser)
{
    s_state = CHESS_STATE_OVER;
    if (s_cursor_ring) lv_obj_add_flag(s_cursor_ring, LV_OBJ_FLAG_HIDDEN);
    s_turn = loser;              /* update_turn 用输方显对方胜 */
    update_turn();
    if (s_win_overlay) return;    /* 防重复 */
    s_win_overlay = lv_obj_create(s_scr);
    lv_obj_remove_flag(s_win_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_win_overlay, CHESS_X0 - 8, CHESS_Y_TOP - 8);
    lv_obj_set_size(s_win_overlay, 8 * CHESS_PITCH + 16, 9 * CHESS_PITCH + 16);
    lv_obj_set_style_radius(s_win_overlay, 0, 0);
    lv_obj_set_style_border_width(s_win_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_win_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_win_overlay, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_bg_opa(s_win_overlay, 153, 0);
    const char *win = (loser == CHESS_RED) ? "黑方胜" : "红方胜";
    s_win_label = ui_pixel_label(s_win_overlay, win, &chess_cjk_24, UI_PAPER);
    lv_obj_align(s_win_label, LV_ALIGN_CENTER, 0, -16);
    s_win_hint = ui_pixel_label(s_win_overlay, "重开", &chess_cjk_14, UI_PAPER);
    lv_obj_align(s_win_hint, LV_ALIGN_CENTER, 0, 16);
}

static void clock_tick(lv_timer_t *t)
{
    (void)t;
    if (s_state < CHESS_STATE_IDLE || s_state == CHESS_STATE_OVER) return;
    int idx = (s_turn == CHESS_RED) ? 0 : 1;
    if (s_clock[idx] > 0) {
        s_clock[idx]--;
        update_clock();
        if (s_clock[idx] == 0) show_win(s_turn);
    }
}

static void sel_cursor_sq(int8_t *r, int8_t *f)
{
    if (s_sel_idx == 0) { *r = s_sel_r; *f = s_sel_f; }
    else { *r = s_targets[s_sel_idx - 1].tr; *f = s_targets[s_sel_idx - 1].tf; }
}
static void clear_selected_visuals(void)
{
    if (s_sel_ring) { lv_obj_delete(s_sel_ring); s_sel_ring = NULL; }
    for (int i = 0; i < CHESS_MAX_MOVES; i++) {
        if (s_hints[i]) { lv_obj_delete(s_hints[i]); s_hints[i] = NULL; }
    }
    s_target_count = 0;
}
static void enter_selected(void)
{
    s_state = CHESS_STATE_SELECTED;
    s_sel_idx = 0;
    chess_move_t all[CHESS_MAX_MOVES];
    int n = chess_gen_moves(s_board, s_turn, all, CHESS_MAX_MOVES);
    s_target_count = 0;
    for (int i = 0; i < n; i++) {
        if (all[i].fr == s_sel_r && all[i].ff == s_sel_f &&
            chess_is_legal(s_board, all[i], s_turn)) {
            s_targets[s_target_count++] = all[i];
        }
    }
    s_sel_ring = make_ring(s_scr, s_sel_r, s_sel_f, CHESS_HL_SELF, 4, CHESS_RING);
    for (int i = 0; i < s_target_count; i++) {
        int8_t tr = s_targets[i].tr, tf = s_targets[i].tf;
        if (chess_at(s_board, tr, tf) != 0)
            s_hints[i] = make_ring(s_scr, tr, tf, CHESS_HL_TARGET, 2, CHESS_RING);
        else
            s_hints[i] = make_dot(s_scr, tr, tf, CHESS_HL_TARGET);
    }
    lv_obj_add_flag(s_cursor_ring, LV_OBJ_FLAG_HIDDEN);
}

static void slide_cb(void *var, int32_t t)
{
    int32_t x = s_anim_from_x + (s_anim_to_x - s_anim_from_x) * t / CHESS_ANIM_T;
    int32_t y = s_anim_from_y + (s_anim_to_y - s_anim_from_y) * t / CHESS_ANIM_T;
    lv_obj_set_pos((lv_obj_t *)var, x, y);
}
static void fade_cb(void *var, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0); }
static void fade_done_cb(lv_anim_t *a)
{
    (void)a;
    if (s_anim_captured) { lv_obj_delete(s_anim_captured); s_anim_captured = NULL; }
}

/* AI 任务前向声明 */
static void maybe_trigger_ai(void);

static void on_move_done(lv_anim_t *a)
{
    (void)a;
    int from = s_anim_move.fr * 9 + s_anim_move.ff;
    int to   = s_anim_move.tr * 9 + s_anim_move.tf;
    s_pieces[to] = s_anim_piece;
    s_pieces[from] = NULL;
    s_anim_piece = NULL;
    chess_make_move(s_board, s_anim_move);
    s_turn = (int8_t)(-s_turn);
    s_animating = false;

    if (!chess_has_legal_move(s_board, s_turn)) {
        show_win(s_turn);
    } else {
        s_state = CHESS_STATE_IDLE;
        update_turn();
        int first = next_own(-1, s_turn);
        if (first >= 0) {
            s_cur_r = (int8_t)(first / 9);
            s_cur_f = (int8_t)(first % 9);
            if (s_cursor_ring) {
                lv_obj_remove_flag(s_cursor_ring, LV_OBJ_FLAG_HIDDEN);
                move_ring(s_cursor_ring, s_cur_r, s_cur_f);
            }
        }
        maybe_trigger_ai();          /* 轮到 AI 则触发 */
    }
}

static void execute_move(chess_move_t m)
{
    int from = m.fr * 9 + m.ff;
    int to   = m.tr * 9 + m.tf;
    if (s_hist_count < 128) {
        s_history[s_hist_count].fr = m.fr; s_history[s_hist_count].ff = m.ff;
        s_history[s_hist_count].tr = m.tr; s_history[s_hist_count].tf = m.tf;
        s_history[s_hist_count].captured = s_board[to];
        s_hist_count++;
    }
    s_anim_move = m;
    s_anim_piece = s_pieces[from];
    s_anim_captured = s_pieces[to];
    s_anim_from_x = px_x(m.ff) - CHESS_DISC / 2;
    s_anim_from_y = px_y(m.fr) - CHESS_DISC / 2;
    s_anim_to_x = px_x(m.tf) - CHESS_DISC / 2;
    s_anim_to_y = px_y(m.tr) - CHESS_DISC / 2;
    clear_selected_visuals();
    if (s_cursor_ring) lv_obj_add_flag(s_cursor_ring, LV_OBJ_FLAG_HIDDEN);
    to_foreground(s_anim_piece);
    s_animating = true;
    if (s_anim_captured) {
        lv_anim_t fa;
        lv_anim_init(&fa);
        lv_anim_set_var(&fa, s_anim_captured);
        lv_anim_set_values(&fa, LV_OPA_COVER, LV_OPA_TRANSP);
        lv_anim_set_duration(&fa, CHESS_ANIM_MS);
        lv_anim_set_path_cb(&fa, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&fa, fade_cb);
        lv_anim_set_completed_cb(&fa, fade_done_cb);
        lv_anim_start(&fa);
    }
    lv_anim_t sa;
    lv_anim_init(&sa);
    lv_anim_set_var(&sa, s_anim_piece);
    lv_anim_set_values(&sa, 0, CHESS_ANIM_T);
    lv_anim_set_duration(&sa, CHESS_ANIM_MS);
    lv_anim_set_path_cb(&sa, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&sa, slide_cb);
    lv_anim_set_completed_cb(&sa, on_move_done);
    lv_anim_start(&sa);
}

static void flash_cb(void *var, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0); }
static void flash_cursor(void)
{
    if (!s_cursor_ring) return;
    lv_anim_delete(s_cursor_ring, flash_cb);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_cursor_ring);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_40);
    lv_anim_set_duration(&a, 120);
    lv_anim_set_playback_duration(&a, 120);
    lv_anim_set_exec_cb(&a, flash_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_start(&a);
}

static bool undo_one(void)
{
    if (s_hist_count == 0) return false;
    chess_hist_t h = s_history[--s_hist_count];
    int from = h.fr * 9 + h.ff;
    int to   = h.tr * 9 + h.tf;
    s_board[from] = s_board[to];
    s_board[to]   = h.captured;
    if (s_pieces[to]) {
        s_pieces[from] = s_pieces[to];
        s_pieces[to] = NULL;
        lv_obj_set_pos(s_pieces[from], px_x(h.ff) - CHESS_DISC / 2, px_y(h.fr) - CHESS_DISC / 2);
        to_foreground(s_pieces[from]);
    }
    if (h.captured != 0)
        s_pieces[to] = make_piece(s_scr, h.captured, h.tr, h.tf);
    s_turn = (int8_t)(-s_turn);
    return true;
}

static void do_undo(void)
{
    int steps = (s_mode == CHESS_MODE_TWO) ? 1 : 2;
    if (s_hist_count < steps) return;          /* 栈不足(含空):不响应 */
    if (s_state == CHESS_STATE_OVER && s_win_overlay) {
        lv_obj_delete(s_win_overlay);
        s_win_overlay = NULL; s_win_label = NULL; s_win_hint = NULL;
    }
    for (int i = 0; i < steps; i++) {
        if (!undo_one()) break;
    }
    clear_selected_visuals();
    s_state = CHESS_STATE_IDLE;
    int first = next_own(-1, s_turn);
    if (first >= 0) {
        s_cur_r = (int8_t)(first / 9);
        s_cur_f = (int8_t)(first % 9);
        if (s_cursor_ring) {
            lv_obj_remove_flag(s_cursor_ring, LV_OBJ_FLAG_HIDDEN);
            move_ring(s_cursor_ring, s_cur_r, s_cur_f);
        }
    }
    update_turn();
    flash_cursor();
}

static void reset_game(void)
{
    if (s_cursor_ring) lv_anim_delete(s_cursor_ring, flash_cb);
    if (s_win_overlay) {
        lv_obj_delete(s_win_overlay);
        s_win_overlay = NULL; s_win_label = NULL; s_win_hint = NULL;
    }
    clear_selected_visuals();
    if (s_cursor_ring) { lv_obj_delete(s_cursor_ring); s_cursor_ring = NULL; }
    for (int i = 0; i < 90; i++) {
        if (s_pieces[i]) { lv_obj_delete(s_pieces[i]); s_pieces[i] = NULL; }
    }
    chess_init(s_board);
    s_hist_count = 0;
    draw_pieces(s_scr);
    s_cursor_ring = make_ring(s_scr, 0, 0, CHESS_HL_SELF, 2, CHESS_RING_CURSOR);
    s_turn = CHESS_RED;
    s_state = CHESS_STATE_IDLE;
    s_animating = false;
    s_anim_piece = NULL; s_anim_captured = NULL;
    int first = next_own(-1, CHESS_RED);
    if (first >= 0) {
        s_cur_r = (int8_t)(first / 9);
        s_cur_f = (int8_t)(first % 9);
        move_ring(s_cursor_ring, s_cur_r, s_cur_f);
    }
    s_clock[0] = s_clock[1] = CHESS_CLOCK_SECS;
    update_clock();
    update_turn();
}

/* ---- AI 工作任务(复用 demo_audio 的 worker + xTaskNotify 模式)---- */
static void ai_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t v = 0;
        if (xTaskNotifyWait(0, UINT32_MAX, &v, portMAX_DELAY) != pdTRUE) continue;
        if (v == 1) break;                       /* STOP */
        if (s_ai_cancel) continue;
        int8_t ai_color = s_turn;
        int depth, tlim; bool easy_rand = false;
        switch (s_mode) {
            case CHESS_MODE_AI_EASY:   depth = 2; tlim = 0;   easy_rand = (rand() & 1); break;
            case CHESS_MODE_AI_NORMAL: depth = 3; tlim = 500;  break;
            case CHESS_MODE_AI_HARD:   depth = 6; tlim = 1500; break;
            default: depth = 2; tlim = 0; break;
        }
        chess_move_t mv;
        if (easy_rand) {
            /* 50% 随机合法走法(初级会犯错) */
            chess_move_t tmp[CHESS_MAX_MOVES];
            int n = chess_gen_moves(s_board, ai_color, tmp, CHESS_MAX_MOVES);
            chess_move_t legal[CHESS_MAX_MOVES]; int m = 0;
            for (int i = 0; i < n; i++)
                if (chess_is_legal(s_board, tmp[i], ai_color)) legal[m++] = tmp[i];
            mv = (m > 0) ? legal[rand() % m] : chess_ai_best_move(s_board, ai_color, 2, 0);
        } else {
            mv = chess_ai_best_move(s_board, ai_color, depth, tlim);
        }
        if (s_ai_cancel) continue;              /* 退出途中:不落子 */
        if (bsp_lvgl_lock(500)) {
            if (s_ai_cancel) { bsp_lvgl_unlock(); continue; }   /* 锁期间被取消 */
            s_ai_busy = false;
            execute_move(mv);                    /* 走同一条动画路径 */
            bsp_lvgl_unlock();
        } else {
            s_ai_busy = false;
        }
    }
    if (s_ai_stopped) xSemaphoreGive(s_ai_stopped);
    s_ai_task = NULL;
    vTaskDelete(NULL);
}

static void maybe_trigger_ai(void)
{
    if (s_mode == CHESS_MODE_TWO) return;
    if (s_turn == s_human_color) return;        /* 人的回合 */
    s_ai_busy = true;
    update_turn();                              /* 显"X方思考中" */
    if (s_cursor_ring) lv_obj_add_flag(s_cursor_ring, LV_OBJ_FLAG_HIDDEN);
    if (s_ai_task) xTaskNotify(s_ai_task, 0, eNoAction);
}

static void start_ai_task(void)
{
    if (s_ai_task) return;
    if (s_ai_stopped) { vSemaphoreDelete(s_ai_stopped); s_ai_stopped = NULL; }
    s_ai_stopped = xSemaphoreCreateBinary();
    if (!s_ai_stopped) return;
    s_ai_cancel = false;
    srand((unsigned)xTaskGetTickCount());
    xTaskCreate(ai_task, "chess_ai", AI_TASK_STACK / sizeof(StackType_t), NULL, 4, &s_ai_task);
}
static void stop_ai_task(void)
{
    TaskHandle_t task = s_ai_task;
    if (!task) {
        if (s_ai_stopped) { vSemaphoreDelete(s_ai_stopped); s_ai_stopped = NULL; }
        return;
    }
    s_ai_cancel = true;
    xTaskNotify(task, 1, eSetValueWithOverwrite);   /* STOP */
    if (!s_ai_stopped ||
        xSemaphoreTake(s_ai_stopped, pdMS_TO_TICKS(AI_STOP_TIMEOUT_MS)) != pdTRUE) {
        /* 超时(搜索未在 2s 内结束):强杀任务 */
        vTaskDelete(task);
        s_ai_task = NULL;
    }
    if (s_ai_stopped) { vSemaphoreDelete(s_ai_stopped); s_ai_stopped = NULL; }
    s_ai_busy = false; s_ai_cancel = false;
}

/* ---- 模式选择屏 ---- */
static lv_obj_t *make_menu_panel(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_border_width(o, 2, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    return o;
}

static void render_select(const char *const *opts, int n, int sel)
{
    if (s_menu_panel) { lv_obj_delete(s_menu_panel); s_menu_panel = NULL; }
    for (int i = 0; i < 4; i++) s_menu_items[i] = NULL;
    s_menu_panel = make_menu_panel(s_scr, 40, 90, 160, n * 28 + 24, UI_PAPER);
    for (int i = 0; i < n; i++) {
        s_menu_items[i] = ui_pixel_label(s_menu_panel, opts[i], &chess_cjk_14,
                                         (i == sel) ? UI_RED : UI_INK);
        lv_obj_align(s_menu_items[i], LV_ALIGN_TOP_MID, 0, 12 + i * 28);
    }
}
static void render_current_select(void)
{
    switch (s_state) {
        case CHESS_STATE_MODE_SELECT:  render_select(MODE_OPTS, 2, s_menu_idx); break;
        case CHESS_STATE_DIFF_SELECT:  render_select(DIFF_OPTS, 4, s_menu_idx); break;
        case CHESS_STATE_SIDE_SELECT: render_select(SIDE_OPTS, 3, s_menu_idx); break;
        default: break;
    }
}

static void start_game(void)
{
    if (s_menu_panel) { lv_obj_delete(s_menu_panel); s_menu_panel = NULL; }
    for (int i = 0; i < 4; i++) s_menu_items[i] = NULL;
    draw_grid(s_scr);
    s_turn_lbl = ui_pixel_label(s_scr, "", &chess_cjk_14, UI_PAPER);
    lv_obj_align(s_turn_lbl, LV_ALIGN_BOTTOM_MID, 0, -8);
    s_clk_lbl[0] = ui_pixel_label(s_scr, "", &lv_font_montserrat_14, UI_RED);
    s_clk_lbl[1] = ui_pixel_label(s_scr, "", &lv_font_montserrat_14, UI_INK);
    lv_obj_align(s_clk_lbl[0], LV_ALIGN_BOTTOM_MID, -32, -24);
    lv_obj_align(s_clk_lbl[1], LV_ALIGN_BOTTOM_MID, 32, -24);
    reset_game();                              /* 内含 s_clock 重置 + update_clock */
    if (!s_clock_timer) s_clock_timer = lv_timer_create(clock_tick, 1000, NULL);
    if (s_mode != CHESS_MODE_TWO) start_ai_task();
    maybe_trigger_ai();      /* 人=黑则 AI(红)先走 */
}

static void handle_idle(bsp_btn_t btn)
{
    if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
        int idx = s_cur_r * 9 + s_cur_f;
        int nxt = (btn == BSP_BTN_DOWN) ? next_own(idx, s_turn) : prev_own(idx, s_turn);
        if (nxt >= 0) {
            s_cur_r = (int8_t)(nxt / 9);
            s_cur_f = (int8_t)(nxt % 9);
            move_ring(s_cursor_ring, s_cur_r, s_cur_f);
        }
    } else if (btn == BSP_BTN_OK) {
        s_sel_r = s_cur_r; s_sel_f = s_cur_f;
        enter_selected();
    }
}
static void handle_selected(bsp_btn_t btn)
{
    if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
        int n = s_target_count + 1;
        s_sel_idx = (btn == BSP_BTN_DOWN) ? (s_sel_idx + 1) % n : (s_sel_idx - 1 + n) % n;
        if (s_sel_idx == 0) {
            lv_obj_add_flag(s_cursor_ring, LV_OBJ_FLAG_HIDDEN);
        } else {
            int8_t r, f;
            sel_cursor_sq(&r, &f);
            lv_obj_remove_flag(s_cursor_ring, LV_OBJ_FLAG_HIDDEN);
            move_ring(s_cursor_ring, r, f);
        }
    } else if (btn == BSP_BTN_OK) {
        if (s_sel_idx == 0) {
            clear_selected_visuals();
            s_state = CHESS_STATE_IDLE;
            lv_obj_remove_flag(s_cursor_ring, LV_OBJ_FLAG_HIDDEN);
            move_ring(s_cursor_ring, s_cur_r, s_cur_f);
        } else {
            execute_move(s_targets[s_sel_idx - 1]);
        }
    }
}
static void handle_over(bsp_btn_t btn)
{
    if (btn == BSP_BTN_OK) reset_game();
}

/* 模式选择:UP/DOWN 移光标+重画,OK 转移。 */
static void handle_select(bool up, bool dn, bool ok)
{
    int n;
    switch (s_state) {
        case CHESS_STATE_MODE_SELECT:  n = 2; break;
        case CHESS_STATE_DIFF_SELECT:  n = 4; break;
        case CHESS_STATE_SIDE_SELECT:  n = 3; break;
        default: return;
    }
    if (up || dn) {
        s_menu_idx = (dn ? (s_menu_idx + 1) : (s_menu_idx - 1 + n)) % n;
        render_current_select();
    } else if (ok) {
        switch (s_state) {
            case CHESS_STATE_MODE_SELECT:
                if (s_menu_idx == 0) { s_mode = CHESS_MODE_TWO; s_human_color = CHESS_RED; start_game(); }
                else { s_state = CHESS_STATE_DIFF_SELECT; s_menu_idx = 0; render_current_select(); }
                break;
            case CHESS_STATE_DIFF_SELECT:
                if (s_menu_idx == 3) { s_state = CHESS_STATE_MODE_SELECT; s_menu_idx = 0; render_current_select(); }
                else {
                    s_mode = (chess_mode_t)(CHESS_MODE_AI_EASY + s_menu_idx);
                    s_state = CHESS_STATE_SIDE_SELECT; s_menu_idx = 0; render_current_select();
                }
                break;
            case CHESS_STATE_SIDE_SELECT:
                if (s_menu_idx == 2) { s_state = CHESS_STATE_DIFF_SELECT; s_menu_idx = 0; render_current_select(); }
                else {
                    s_human_color = (s_menu_idx == 0) ? CHESS_RED : CHESS_BLACK;
                    start_game();
                }
                break;
            default: break;
        }
    }
}

void demo_chess_enter(void)
{
    s_scr = ui_pixel_screen_create("CHESS");
    s_mode = CHESS_MODE_TWO;
    s_human_color = CHESS_RED;
    s_state = CHESS_STATE_MODE_SELECT;
    s_menu_idx = 0;
    s_hist_count = 0;
    render_current_select();
    lv_screen_load(s_scr);
}

void demo_chess_exit(void)
{
    stop_ai_task();
    if (s_anim_piece)    lv_anim_delete(s_anim_piece, slide_cb);
    if (s_anim_captured) lv_anim_delete(s_anim_captured, fade_cb);
    if (s_cursor_ring)  lv_anim_delete(s_cursor_ring, flash_cb);
    if (s_clock_timer) { lv_timer_delete(s_clock_timer); s_clock_timer = NULL; }
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL; s_turn_lbl = NULL;
        s_clk_lbl[0] = NULL; s_clk_lbl[1] = NULL;
        s_cursor_ring = NULL; s_sel_ring = NULL; s_menu_panel = NULL;
        s_win_overlay = NULL; s_win_label = NULL; s_win_hint = NULL;
        for (int i = 0; i < 4; i++) s_menu_items[i] = NULL;
        for (int i = 0; i < 90; i++) s_pieces[i] = NULL;
        for (int i = 0; i < CHESS_MAX_MOVES; i++) s_hints[i] = NULL;
        s_target_count = 0; s_state = CHESS_STATE_MODE_SELECT; s_turn = CHESS_RED;
        s_animating = false; s_ai_busy = false;
        s_anim_piece = NULL; s_anim_captured = NULL;
        s_mode = CHESS_MODE_TWO; s_human_color = CHESS_RED; s_menu_idx = 0;
        s_hist_count = 0;
    }
}

void demo_chess_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    bool is_up = (btn == BSP_BTN_UP && ev == BSP_BTN_PRESS);
    bool is_dn = (btn == BSP_BTN_DOWN && ev == BSP_BTN_PRESS);
    bool is_ok = (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK);
    bool is_undo = (btn == BSP_BTN_UP && ev == BSP_BTN_LONG);
    if (!is_up && !is_dn && !is_ok && !is_undo) return;
    if (!s_scr) return;
    if (s_animating || s_ai_busy) return;     /* 动画/AI 思考期间丢这一帧 */
    if (!bsp_lvgl_lock(100)) return;
    if (is_undo) {
        switch (s_state) {
            case CHESS_STATE_IDLE:
            case CHESS_STATE_SELECTED:
            case CHESS_STATE_OVER:
                do_undo();
                break;
            default: break;             /* 模式选择三屏不响应悔棋 */
        }
    } else {
        switch (s_state) {
            case CHESS_STATE_MODE_SELECT:
            case CHESS_STATE_DIFF_SELECT:
            case CHESS_STATE_SIDE_SELECT:
                handle_select(is_up, is_dn, is_ok);
                break;
            case CHESS_STATE_IDLE:     handle_idle(btn); break;
            case CHESS_STATE_SELECTED: handle_selected(btn); break;
            case CHESS_STATE_OVER:     handle_over(btn); break;
        }
    }
    bsp_lvgl_unlock();
}
