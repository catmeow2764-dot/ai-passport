// main/demo_chess.c —— 中国象棋:棋盘 + 输入选子落子(第 3 步)。
// 棋盘/棋子/回合由 chess_rules 纯逻辑引擎驱动;UI 只把"选择"翻译成对象移动。
// 棋子字形来自自生成的 CJK 子集字体 chess_cjk_14(19 字,Flash ~15KB,0 RAM)。
#include "demo.h"
#include "bsp_display.h"      // bsp_lvgl_lock
#include "ui_pixel.h"
#include "chess_rules.h"
#include "lvgl.h"

extern const lv_font_t chess_cjk_14;   /* main/fonts/chess_cjk_14.c */

/* 布局:9×10 交叉点,pitch 24px。rank 0=红底(屏底),rank 9=黑底(屏顶)。 */
#define CHESS_PITCH  24
#define CHESS_X0     24          /* file 0 交叉点 x */
#define CHESS_Y_TOP  55          /* rank 9(顶)交叉点 y */
#define CHESS_DISC   18          /* 棋子圆盘直径 */
#define CHESS_RING   20          /* 光标/选中环外径(略大于棋盘,框住 18px 棋子) */

/* 高亮配色:绿=己方光标/选中;金=可达目标。两者在红/黑/米白底上都可见。 */
#define CHESS_HL_SELF   0x12A050
#define CHESS_HL_TARGET 0xE8B600

typedef enum { CHESS_STATE_IDLE, CHESS_STATE_SELECTED, CHESS_STATE_OVER } chess_state_t;

static lv_obj_t *s_scr;
static chess_sq_t s_board[90];
static int8_t s_turn;                          /* CHESS_RED / CHESS_BLACK,app 自管 */
static lv_obj_t *s_turn_lbl;

static chess_state_t s_state;
static int8_t s_cur_r, s_cur_f;                /* IDLE 时光标所在格(必为己方棋子) */
static int8_t s_sel_r, s_sel_f;               /* SELECTED 时选中的棋子格 */
static int s_sel_idx;                         /* SELECTED 时光标在循环列表里的下标 */
static chess_move_t s_targets[CHESS_MAX_MOVES]; /* 选中棋子的合法走法 */
static int s_target_count;
static lv_obj_t *s_pieces[90];               /* 每格的棋子对象(空=NULL),供落子移动 */
static lv_obj_t *s_cursor_ring;               /* 持久:进页/重开时建,只移动不重建 */
static lv_obj_t *s_sel_ring;                  /* SELECTED 时建,取消/落子时删 */
static lv_obj_t *s_hints[CHESS_MAX_MOVES];     /* SELECTED 时建,取消/落子时删 */

static int px_x(int f) { return CHESS_X0 + f * CHESS_PITCH; }
static int px_y(int r) { return CHESS_Y_TOP + (9 - r) * CHESS_PITCH; }

static bool is_own(chess_sq_t sq, int8_t color) {
    return sq != 0 && ((sq > 0) == (color > 0));
}

/* 从 from_idx(不含)起沿读序找下一枚己方棋子;from_idx=-1 即从头找第一枚。 */
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

/* 极简方块:去边框/内边距/圆角、纯色填充。仿 ui_pixel 内部 block()。 */
static lv_obj_t *chess_rect(lv_obj_t *parent, int x, int y, int w, int h,
                            uint32_t color)
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

/* 空心圆环:透明底 + 彩色边框,框住交叉点上的棋子。 */
static lv_obj_t *make_ring(lv_obj_t *parent, int8_t r, int8_t f,
                           uint32_t color, int border_w)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, px_x(f) - CHESS_RING / 2, px_y(r) - CHESS_RING / 2);
    lv_obj_set_size(o, CHESS_RING, CHESS_RING);
    lv_obj_set_style_radius(o, CHESS_RING / 2, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, border_w, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    return o;
}

/* 实心小点:标记空格目标(不吃子的走法)。 */
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
    if (ring) lv_obj_set_pos(ring, px_x(f) - CHESS_RING / 2, px_y(r) - CHESS_RING / 2);
}

/* abs(sq) 1..7 → 红方/黑方各自的棋子字(sq 符号=颜色,绝对值=类型)。 */
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
    /* 棋盘底板(米白),四周留 8px 边。 */
    chess_rect(parent, CHESS_X0 - 8, CHESS_Y_TOP - 8,
               8 * CHESS_PITCH + 16, 9 * CHESS_PITCH + 16, UI_PAPER);
    /* 10 条横线(每 rank 一条,2px 居中压在交叉点 y 上)。 */
    for (int r = 0; r <= 9; r++)
        chess_rect(parent, CHESS_X0, px_y(r) - 1, 8 * CHESS_PITCH, 2, UI_INK);
    /* 9 条竖线;九宫斜线/河界/蹩马腿等规则由 chess_rules 强制,不靠画。 */
    for (int f = 0; f <= 8; f++)
        chess_rect(parent, px_x(f) - 1, CHESS_Y_TOP, 2, 9 * CHESS_PITCH, UI_INK);
}

static void draw_pieces(lv_obj_t *parent)
{
    /* 每枚棋子用单个 lv_label 兼任底盘+文字,并登记到 s_pieces 供落子移动。
       文字居中:text_align CENTER 横向 + pad_top/bottom=(DISC-行高)/2 纵向。 */
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

static void update_turn(void)
{
    if (!s_turn_lbl) return;
    lv_label_set_text(s_turn_lbl, s_turn == CHESS_RED ? "红方走棋" : "黑方走棋");
    lv_obj_align(s_turn_lbl, LV_ALIGN_BOTTOM_MID, 0, -8);
}

/* 选中棋子时 SELECTED 光标所在的格(下标 0=选中棋子本身,1..N=各合法目标)。 */
static void sel_cursor_sq(int8_t *r, int8_t *f)
{
    if (s_sel_idx == 0) { *r = s_sel_r; *f = s_sel_f; }
    else {
        *r = s_targets[s_sel_idx - 1].tr;
        *f = s_targets[s_sel_idx - 1].tf;
    }
}

/* 清掉 SELECTED 的视觉(选中环 + 目标点),不动状态机。 */
static void clear_selected_visuals(void)
{
    if (s_sel_ring) { lv_obj_delete(s_sel_ring); s_sel_ring = NULL; }
    for (int i = 0; i < CHESS_MAX_MOVES; i++) {
        if (s_hints[i]) { lv_obj_delete(s_hints[i]); s_hints[i] = NULL; }
    }
    s_target_count = 0;
}

/* IDLE → SELECTED:算选中棋子的合法目标,建选中环 + 目标点,光标归位到选中棋子。 */
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
    s_sel_ring = make_ring(s_scr, s_sel_r, s_sel_f, CHESS_HL_SELF, 4);
    for (int i = 0; i < s_target_count; i++) {
        int8_t tr = s_targets[i].tr, tf = s_targets[i].tf;
        if (chess_at(s_board, tr, tf) != 0)
            s_hints[i] = make_ring(s_scr, tr, tf, CHESS_HL_TARGET, 2);  /* 吃子 */
        else
            s_hints[i] = make_dot(s_scr, tr, tf, CHESS_HL_TARGET);       /* 平移 */
    }
    move_ring(s_cursor_ring, s_sel_r, s_sel_f);
}

/* 执行落子:移棋子对象(+删被吃子)、引擎同步、拆 SELECTED 视觉、换回合、判胜。 */
static void execute_move(chess_move_t m)
{
    int from = m.fr * 9 + m.ff;
    int to = m.tr * 9 + m.tf;
    if (s_pieces[to]) { lv_obj_delete(s_pieces[to]); s_pieces[to] = NULL; }
    if (s_pieces[from]) {
        lv_obj_set_pos(s_pieces[from], px_x(m.tf) - CHESS_DISC / 2,
                       px_y(m.tr) - CHESS_DISC / 2);
        s_pieces[to] = s_pieces[from];
        s_pieces[from] = NULL;
    }
    chess_make_move(s_board, m);
    clear_selected_visuals();
    s_turn = (int8_t)(-s_turn);
    if (!chess_has_legal_move(s_board, s_turn)) {
        s_state = CHESS_STATE_OVER;
        lv_label_set_text(s_turn_lbl, s_turn == CHESS_RED ? "黑方胜" : "红方胜");
        lv_obj_align(s_turn_lbl, LV_ALIGN_BOTTOM_MID, 0, -8);
    } else {
        s_state = CHESS_STATE_IDLE;
        update_turn();
        int first = next_own(-1, s_turn);
        if (first >= 0) {
            s_cur_r = (int8_t)(first / 9);
            s_cur_f = (int8_t)(first % 9);
            move_ring(s_cursor_ring, s_cur_r, s_cur_f);
        }
    }
}

/* 重开/首开:清棋子+视觉,chess_init,重画棋子,光标→红方首枚,回合=红。 */
static void reset_game(void)
{
    clear_selected_visuals();
    if (s_cursor_ring) { lv_obj_delete(s_cursor_ring); s_cursor_ring = NULL; }
    for (int i = 0; i < 90; i++) {
        if (s_pieces[i]) { lv_obj_delete(s_pieces[i]); s_pieces[i] = NULL; }
    }
    chess_init(s_board);
    draw_pieces(s_scr);                              /* 重建在棋子之上 */
    s_cursor_ring = make_ring(s_scr, 0, 0, CHESS_HL_SELF, 2);
    s_turn = CHESS_RED;
    s_state = CHESS_STATE_IDLE;
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
        int n = s_target_count + 1;                  /* 选中棋子本身 + 各目标 */
        s_sel_idx = (btn == BSP_BTN_DOWN) ? (s_sel_idx + 1) % n
                                          : (s_sel_idx - 1 + n) % n;
        int8_t r, f;
        sel_cursor_sq(&r, &f);
        move_ring(s_cursor_ring, r, f);
    } else if (btn == BSP_BTN_OK) {
        if (s_sel_idx == 0) {
            /* 再按一次 OK=取消,光标留在原棋子上(s_cur == s_sel) */
            clear_selected_visuals();
            s_state = CHESS_STATE_IDLE;
        } else {
            execute_move(s_targets[s_sel_idx - 1]);
        }
    }
}

static void handle_over(bsp_btn_t btn)
{
    if (btn == BSP_BTN_OK) reset_game();             /* OK-CLICK = 重开 */
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
    if (s_scr) {
        lv_obj_delete(s_scr);   /* 级联删除所有子对象 */
        s_scr = NULL; s_turn_lbl = NULL; s_cursor_ring = NULL; s_sel_ring = NULL;
        for (int i = 0; i < 90; i++) s_pieces[i] = NULL;
        for (int i = 0; i < CHESS_MAX_MOVES; i++) s_hints[i] = NULL;
        s_target_count = 0; s_state = CHESS_STATE_IDLE; s_turn = CHESS_RED;
    }
}

void demo_chess_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    /* 只认 UP/DOWN-PRESS(跟手)和 OK-CLICK(稳重);OK-LONG 由 main 拦截退页。 */
    bool is_up = (btn == BSP_BTN_UP && ev == BSP_BTN_PRESS);
    bool is_dn = (btn == BSP_BTN_DOWN && ev == BSP_BTN_PRESS);
    bool is_ok = (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK);
    if (!is_up && !is_dn && !is_ok) return;

    if (!s_scr) return;
    if (!bsp_lvgl_lock(100)) return;   /* 非阻塞:拿不到锁就丢这一帧输入 */
    switch (s_state) {
        case CHESS_STATE_IDLE:     handle_idle(btn); break;
        case CHESS_STATE_SELECTED: handle_selected(btn); break;
        case CHESS_STATE_OVER:     handle_over(btn); break;
    }
    bsp_lvgl_unlock();
}
