// main/demo_chess.c —— 中国象棋:棋盘 + 输入选子落子 + 走子/被吃动画 + 胜负画面 + 将军提示。
// 规则由 chess_rules 纯逻辑引擎驱动;UI 只把"选择/落子"翻译成对象移动 + 动画。
// 棋子字形:chess_cjk_14(24 字,棋子+回合/将军/重开文本);胜负标题:chess_cjk_24(4 字,红黑方胜)。
#include "demo.h"
#include "bsp_display.h"      // bsp_lvgl_lock
#include "ui_pixel.h"
#include "chess_rules.h"
#include "lvgl.h"

extern const lv_font_t chess_cjk_14;   /* main/fonts/chess_cjk_14.c */
extern const lv_font_t chess_cjk_24;   /* main/fonts/chess_cjk_24.c */

/* 布局:9×10 交叉点,pitch 24px。rank 0=红底(屏底),rank 9=黑底(屏顶)。 */
#define CHESS_PITCH  24
#define CHESS_X0     24
#define CHESS_Y_TOP  55
#define CHESS_DISC   18
#define CHESS_RING   20
#define CHESS_RING_CURSOR 24   /* 光标环略大于提示环(20),套在同格时绿在外/金在内都可见 */

/* 动画时长(ms),ease-out。 */
#define CHESS_ANIM_MS 150
/* 走子滑动动画的数值范围(exec_cb 按 t/1024 线性插值 x/y)。 */
#define CHESS_ANIM_T 1024

/* 高亮配色:绿=己方光标/选中;金=可达目标。两者在红/黑/米白底上都可见。 */
#define CHESS_HL_SELF   0x12A050
#define CHESS_HL_TARGET 0xE8B600

typedef enum { CHESS_STATE_IDLE, CHESS_STATE_SELECTED, CHESS_STATE_OVER } chess_state_t;

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

/* 走子动画态(同一时刻最多一发动画,s_animating 期间锁输入)。 */
static bool     s_animating;
static lv_obj_t *s_anim_piece;          /* 滑动中的走子棋子 */
static lv_obj_t *s_anim_captured;       /* 淡出中的被吃棋子(无则 NULL) */
static int32_t  s_anim_from_x, s_anim_from_y, s_anim_to_x, s_anim_to_y;
static chess_move_t s_anim_move;         /* 滑动动画携带的走法,结束后交给 on_move_done */

/* 胜负画面(仅 OVER 时存在)。 */
static lv_obj_t *s_win_overlay, *s_win_label, *s_win_hint;

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

/* 把对象移到父对象最末(最高 z 序),让走子棋子盖在被吃棋子之上。 */
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
    to_foreground(ring);   /* 光标始终最上层,盖过同格的金色提示环 */
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

static void draw_pieces(lv_obj_t *parent)
{
    for (int r = 0; r <= 9; r++) {
        for (int f = 0; f <= 8; f++) {
            int idx = r * 9 + f;
            int8_t sq = s_board[idx];
            if (sq == 0) { s_pieces[idx] = NULL; continue; }
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
            s_pieces[idx] = p;
        }
    }
}

/* 回合标签:OVER 显胜方;被将显"X方被将";否则"X方走棋"。 */
static void update_turn(void)
{
    if (!s_turn_lbl) return;
    const char *t;
    if (s_state == CHESS_STATE_OVER) {
        t = (s_turn == CHESS_RED) ? "黑方胜" : "红方胜";   /* s_turn=败方,胜方=-s_turn */
    } else if (chess_in_check(s_board, s_turn)) {
        t = (s_turn == CHESS_RED) ? "红方被将" : "黑方被将";
    } else {
        t = (s_turn == CHESS_RED) ? "红方走棋" : "黑方走棋";
    }
    lv_label_set_text(s_turn_lbl, t);
    lv_obj_align(s_turn_lbl, LV_ALIGN_BOTTOM_MID, 0, -8);
}

static void sel_cursor_sq(int8_t *r, int8_t *f)
{
    if (s_sel_idx == 0) { *r = s_sel_r; *f = s_sel_f; }
    else {
        *r = s_targets[s_sel_idx - 1].tr;
        *f = s_targets[s_sel_idx - 1].tf;
    }
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
    /* 光标在选中棋子本身(s_sel_idx==0)时隐藏——选中环已标住,避免双绿环重叠。 */
    lv_obj_add_flag(s_cursor_ring, LV_OBJ_FLAG_HIDDEN);
}

/* 滑动动画逐帧回调:t=0..1024(ease-out 已由 path 应用),线性插值 x/y。 */
static void slide_cb(void *var, int32_t t)
{
    int32_t x = s_anim_from_x + (s_anim_to_x - s_anim_from_x) * t / CHESS_ANIM_T;
    int32_t y = s_anim_from_y + (s_anim_to_y - s_anim_from_y) * t / CHESS_ANIM_T;
    lv_obj_set_pos((lv_obj_t *)var, x, y);
}

/* 被吃棋子逐帧:整对象 opa 从 COVER 淡到 TRANSP。 */
static void fade_cb(void *var, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

/* 被吃淡出结束:删被吃对象。 */
static void fade_done_cb(lv_anim_t *a)
{
    (void)a;
    if (s_anim_captured) {
        lv_obj_delete(s_anim_captured);
        s_anim_captured = NULL;
    }
}

/* 走子滑动结束:同步 s_pieces + 引擎,换回合,判胜/将军,光标归位。跑在 LVGL 任务。 */
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
        s_state = CHESS_STATE_OVER;
        if (s_cursor_ring) lv_obj_add_flag(s_cursor_ring, LV_OBJ_FLAG_HIDDEN);
        update_turn();
        /* 胜负画面:暗化遮罩 + 24px 标题 + 重开提示。 */
        s_win_overlay = lv_obj_create(s_scr);
        lv_obj_remove_flag(s_win_overlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(s_win_overlay, CHESS_X0 - 8, CHESS_Y_TOP - 8);
        lv_obj_set_size(s_win_overlay, 8 * CHESS_PITCH + 16, 9 * CHESS_PITCH + 16);
        lv_obj_set_style_radius(s_win_overlay, 0, 0);
        lv_obj_set_style_border_width(s_win_overlay, 0, 0);
        lv_obj_set_style_pad_all(s_win_overlay, 0, 0);
        lv_obj_set_style_bg_color(s_win_overlay, lv_color_hex(UI_INK), 0);
        lv_obj_set_style_bg_opa(s_win_overlay, 153, 0);   /* ~60% */
        const char *win = (s_turn == CHESS_RED) ? "黑方胜" : "红方胜";
        s_win_label = ui_pixel_label(s_win_overlay, win, &chess_cjk_24, UI_PAPER);
        lv_obj_align(s_win_label, LV_ALIGN_CENTER, 0, -16);
        s_win_hint = ui_pixel_label(s_win_overlay, "OK 重开", &chess_cjk_14, UI_PAPER);
        lv_obj_align(s_win_hint, LV_ALIGN_CENTER, 0, 16);
    } else {
        s_state = CHESS_STATE_IDLE;
        update_turn();   /* 含将军提示 */
        int first = next_own(-1, s_turn);
        if (first >= 0) {
            s_cur_r = (int8_t)(first / 9);
            s_cur_f = (int8_t)(first % 9);
            if (s_cursor_ring) {
                lv_obj_remove_flag(s_cursor_ring, LV_OBJ_FLAG_HIDDEN);
                move_ring(s_cursor_ring, s_cur_r, s_cur_f);
            }
        }
    }
}

/* 踢出走子 + 被吃动画;board/turn/光标延后到 on_move_done。 */
static void execute_move(chess_move_t m)
{
    int from = m.fr * 9 + m.ff;
    int to   = m.tr * 9 + m.tf;
    s_anim_move = m;
    s_anim_piece = s_pieces[from];
    s_anim_captured = s_pieces[to];          /* 可能为 NULL(不吃子) */
    s_anim_from_x = px_x(m.ff) - CHESS_DISC / 2;
    s_anim_from_y = px_y(m.fr) - CHESS_DISC / 2;
    s_anim_to_x = px_x(m.tf) - CHESS_DISC / 2;
    s_anim_to_y = px_y(m.tr) - CHESS_DISC / 2;

    clear_selected_visuals();
    if (s_cursor_ring) lv_obj_add_flag(s_cursor_ring, LV_OBJ_FLAG_HIDDEN);
    to_foreground(s_anim_piece);             /* 走子棋子盖在被吃棋子之上 */
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

static void reset_game(void)
{
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
    update_turn();
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
        s_sel_idx = (btn == BSP_BTN_DOWN) ? (s_sel_idx + 1) % n
                                          : (s_sel_idx - 1 + n) % n;
        if (s_sel_idx == 0) {
            /* 光标回到选中棋子:隐藏光标环(选中环已标住)。 */
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
            /* 恢复光标环到原棋子(取消选中,回 IDLE 导航)。 */
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

void demo_chess_enter(void)
{
    s_scr = ui_pixel_screen_create("CHESS");
    draw_grid(s_scr);
    s_turn_lbl = ui_pixel_label(s_scr, "", &chess_cjk_14, UI_PAPER);
    reset_game();
    lv_screen_load(s_scr);
}

void demo_chess_exit(void)
{
    /* 先取消在飞动画,再删对象——否则动画回调打到已释放对象上会崩。 */
    if (s_anim_piece)    lv_anim_delete(s_anim_piece, slide_cb);
    if (s_anim_captured) lv_anim_delete(s_anim_captured, fade_cb);
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL; s_turn_lbl = NULL;
        s_cursor_ring = NULL; s_sel_ring = NULL;
        s_win_overlay = NULL; s_win_label = NULL; s_win_hint = NULL;
        for (int i = 0; i < 90; i++) s_pieces[i] = NULL;
        for (int i = 0; i < CHESS_MAX_MOVES; i++) s_hints[i] = NULL;
        s_target_count = 0; s_state = CHESS_STATE_IDLE; s_turn = CHESS_RED;
        s_animating = false; s_anim_piece = NULL; s_anim_captured = NULL;
    }
}

void demo_chess_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    bool is_up = (btn == BSP_BTN_UP && ev == BSP_BTN_PRESS);
    bool is_dn = (btn == BSP_BTN_DOWN && ev == BSP_BTN_PRESS);
    bool is_ok = (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK);
    if (!is_up && !is_dn && !is_ok) return;
    if (!s_scr) return;
    if (s_animating) return;    /* 走子/被吃动画期间丢这一帧输入 */
    if (!bsp_lvgl_lock(100)) return;
    switch (s_state) {
        case CHESS_STATE_IDLE:     handle_idle(btn); break;
        case CHESS_STATE_SELECTED: handle_selected(btn); break;
        case CHESS_STATE_OVER:     handle_over(btn); break;
    }
    bsp_lvgl_unlock();
}
