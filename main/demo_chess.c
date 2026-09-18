// main/demo_chess.c —— 中国象棋:棋盘静态渲染(第 2 步)。
// 棋子字形来自自生成的 CJK 子集字体 chess_cjk_14(19 字,Flash ~14KB,0 RAM)。
// 棋盘/棋子/回合由 chess_rules 纯逻辑引擎驱动;输入选子落子在第 3 步接。
#include "demo.h"
#include "ui_pixel.h"
#include "chess_rules.h"
#include "lvgl.h"

extern const lv_font_t chess_cjk_14;   /* main/fonts/chess_cjk_14.c */

/* 布局:9×10 交叉点,pitch 24px。rank 0=红底(屏底),rank 9=黑底(屏顶)。 */
#define CHESS_PITCH  24
#define CHESS_X0     24          /* file 0 交叉点 x */
#define CHESS_Y_TOP  55          /* rank 9(顶)交叉点 y */
#define CHESS_DISC   18          /* 棋子圆盘直径 */

static lv_obj_t *s_scr;
static chess_sq_t s_board[90];
static int8_t s_turn;            /* CHESS_RED / CHESS_BLACK,app 自管(引擎不记) */
static lv_obj_t *s_turn_lbl;

static int px_x(int f) { return CHESS_X0 + f * CHESS_PITCH; }
static int px_y(int r) { return CHESS_Y_TOP + (9 - r) * CHESS_PITCH; }

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
    /* 9 条竖线(每 file 一条,第 1 版不断河;九宫斜线暂不画——
       蹩马腿/塞象眼/九宫/将帅照面等规则由 chess_rules 强制,不靠画)。 */
    for (int f = 0; f <= 8; f++)
        chess_rect(parent, px_x(f) - 1, CHESS_Y_TOP, 2, 9 * CHESS_PITCH, UI_INK);
}

static void draw_pieces(lv_obj_t *parent)
{
    /* 每枚棋子用单个 lv_label 兼任底盘+文字(原来是 disc+label 两个对象)。
       LVGL 池子只有 48KB,合并后 32 枚棋子从 64 对象降到 32,留出余量。
       文字居中:text_align CENTER 横向 + pad_top/bottom=(DISC-字高)/2 纵向。 */
    for (int r = 0; r <= 9; r++) {
        for (int f = 0; f <= 8; f++) {
            int8_t sq = chess_at(s_board, r, f);
            if (sq == 0) continue;
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
        }
    }
}

static void update_turn(void)
{
    if (!s_turn_lbl) return;
    lv_label_set_text(s_turn_lbl, s_turn == CHESS_RED ? "红方走棋" : "黑方走棋");
    lv_obj_align(s_turn_lbl, LV_ALIGN_BOTTOM_MID, 0, -8);
}

void demo_chess_enter(void)
{
    s_scr = ui_pixel_screen_create("CHESS");
    chess_init(s_board);
    s_turn = CHESS_RED;
    draw_grid(s_scr);
    draw_pieces(s_scr);
    s_turn_lbl = ui_pixel_label(s_scr, "", &chess_cjk_14, UI_PAPER);
    update_turn();
    lv_screen_load(s_scr);
}

void demo_chess_exit(void)
{
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; s_turn_lbl = NULL; }
}

void demo_chess_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    (void)btn; (void)ev;   /* 第 3 步:UP/DOWN/OK 选子、落子、判胜 */
}
