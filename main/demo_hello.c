// main/demo_hello.c —— 最小演示页:屏上显示 Hello World。
#include "demo.h"
#include "ui_pixel.h"
#include "lvgl.h"

static lv_obj_t *s_scr;

void demo_hello_enter(void) {
    s_scr = ui_pixel_screen_create("HELLO");
    lv_obj_t *label = lv_label_create(s_scr);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_INK), 0);
    lv_label_set_text(label, "Hello World!");
    lv_obj_center(label);
    ui_pixel_mascot_create(s_scr, 101, 242);
    lv_screen_load(s_scr);
}

void demo_hello_exit(void) {
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; }
}

void demo_hello_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    (void)btn; (void)ev;
}
