// main/chess_commentary.c —— JEV 棋评:每回合联网调 TypeSafe systemone API
// 评局面语义(占优/攻势/阶段),结果通过 callback 回传 UI。社区版无 key / 断网 / 超时
// → callback "棋评不可用"。网络在独立 FreeRTOS task(不阻塞 LVGL)。
#include "chess_commentary.h"
#include "chess_master_key.h"
#include "chess_wifi.h"
#include "bsp_display.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

#define COMM_TASK_STACK   8192
#define COMM_TIMEOUT_MS   10000
#define COMM_STOP_TIMEOUT 2000
#define COMM_MAX_MOVES    128

static const char *TAG = "chess_commentary";

/* ---- 记谱(标准中文:炮二平五 / 马8进7)---- */
static const char *PIECE_ZH(int8_t p) {
    switch (p) {
        case 1: return "帅"; case 2: return "仕"; case 3: return "相";
        case 4: return "马"; case 5: return "车"; case 6: return "炮"; case 7: return "兵";
        case -1: return "将"; case -2: return "士"; case -3: return "象";
        case -4: return "马"; case -5: return "车"; case -6: return "炮"; case -7: return "卒";
        default: return "";
    }
}
static const char *CN_NUM[] = {"一","二","三","四","五","六","七","八","九"};

static void road_name(char *buf, int sz, int ff, int8_t color) {
    if (color > 0) {
        int r = 9 - ff;                  /* 红方:file→中文路数(右起一二三...) */
        if (r >= 1 && r <= 9) { strncpy(buf, CN_NUM[r-1], sz-1); buf[sz-1]=0; }
        else buf[0]=0;
    } else {
        snprintf(buf, sz, "%d", ff + 1); /* 黑方:阿拉伯 */
    }
}

static void notation(char *out, int sz, const chess_sq_t *board, chess_move_t m, int8_t color) {
    int8_t piece = board[m.fr * 9 + m.ff];
    int8_t t = piece > 0 ? piece : -piece;
    const char *pname = PIECE_ZH(piece);
    char rf[8], tf[8];
    road_name(rf, sizeof(rf), m.ff, color);
    road_name(tf, sizeof(tf), m.tf, color);
    if (m.tr == m.fr && (t == 5 || t == 6)) {
        snprintf(out, sz, "%s%s平%s", pname, rf, tf);          /* 车炮横走=平 */
    } else {
        int forward = (color > 0) ? (m.tr > m.fr) : (m.tr < m.fr);
        const char *act = forward ? "进" : "退";
        if (t == 5 || t == 6 || t == 7) {                       /* 车炮兵直行:步数 */
            int steps = (m.tr > m.fr) ? (m.tr - m.fr) : (m.fr - m.tr);
            if (color > 0) snprintf(out, sz, "%s%s%s%s", pname, rf, act, CN_NUM[steps-1]);
            else snprintf(out, sz, "%s%s%s%d", pname, rf, act, steps);
        } else {
            snprintf(out, sz, "%s%s%s%s", pname, rf, act, tf);  /* 马象士斜行:终点路 */
        }
    }
}

/* 生成 state:"红方走棋,已走15步,被将。走法历史:炮二平五 马8进7 ..." 最近10步 */
static void build_state(char *out, int sz, const chess_sq_t *board, int8_t turn,
                        const chess_move_t *moves, int n) {
    chess_sq_t tmp[90];
    chess_init(tmp);
    int start = n - 10;
    if (start < 0) start = 0;
    char hist[320];
    hist[0] = 0;
    int pos = 0;
    for (int i = 0; i < n; i++) {
        if (i >= start) {
            int8_t mover = (i % 2 == 0) ? CHESS_RED : CHESS_BLACK;
            char mbuf[16];
            notation(mbuf, sizeof(mbuf), tmp, moves[i], mover);
            int w = snprintf(hist + pos, sizeof(hist) - pos, "%s ", mbuf);
            if (w < 0) break;
            pos += w;
            if (pos >= (int)sizeof(hist) - 16) break;
        }
        chess_make_move(tmp, moves[i]);
    }
    const char *side = (turn > 0) ? "红方" : "黑方";
    const char *chk = chess_in_check(board, turn) ? ",被将" : "";
    snprintf(out, sz, "%s走棋,已走%d步%s。走法历史:%s", side, n, chk, hist);
}

/* ---- choice key → 中文映射 ---- */
static const char *map_better(const char *k) {
    if (!k) return "均势";
    if (strcmp(k, "red") == 0) return "红方略优";
    if (strcmp(k, "black") == 0) return "黑方略优";
    return "均势";
}
static const char *map_tempo(const char *k) {
    if (!k) return NULL;
    if (strcmp(k, "attack") == 0) return "攻势积极";
    if (strcmp(k, "defend") == 0) return "守势稳健";
    return NULL;                       /* even 省略 */
}
static const char *map_phase(const char *k) {
    if (!k) return "";
    if (strcmp(k, "opening") == 0) return "开局";
    if (strcmp(k, "middle") == 0) return "中局";
    if (strcmp(k, "endgame") == 0) return "残局";
    return "";
}

/* ---- 任务状态 ---- */
static commentary_cb_t s_cb = NULL;
static void *s_cb_user = NULL;
static TaskHandle_t s_task = NULL;
static SemaphoreHandle_t s_stopped = NULL;
static volatile bool s_cancel = false;

static chess_sq_t s_board_snap[90];
static int8_t s_turn_snap;
static chess_move_t s_moves_snap[COMM_MAX_MOVES];
static int s_moves_n;

static char s_resp[2048];
static int s_resp_len;

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        int copy = evt->data_len;
        if (s_resp_len + copy >= (int)sizeof(s_resp)) copy = (int)sizeof(s_resp) - 1 - s_resp_len;
        if (copy > 0) {
            memcpy(s_resp + s_resp_len, evt->data, copy);
            s_resp_len += copy;
        }
    }
    return ESP_OK;
}

static void call_cb(const char *text) {
    if (s_cb && bsp_lvgl_lock(500)) {
        s_cb(text, s_cb_user);
        bsp_lvgl_unlock();
    }
}

/* 调 JEV API,成功填 text,失败返回 false */
static bool query_jev(const char *state, char *text, int text_sz) {
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "state", state);
    cJSON_AddStringToObject(body, "model", "jev-latest");
    cJSON *qs = cJSON_CreateObject();
    cJSON *q1 = cJSON_CreateObject();
    cJSON_AddStringToObject(q1, "type", "choice");
    cJSON_AddStringToObject(q1, "instructions", "判断当前局面哪方占优");
    cJSON *c1 = cJSON_CreateObject();
    cJSON_AddStringToObject(c1, "red", "红方占优");
    cJSON_AddStringToObject(c1, "even", "均势");
    cJSON_AddStringToObject(c1, "black", "黑方占优");
    cJSON_AddItemToObject(q1, "criteria", c1);
    cJSON_AddItemToObject(qs, "better", q1);
    cJSON *q2 = cJSON_CreateObject();
    cJSON_AddStringToObject(q2, "type", "choice");
    cJSON_AddStringToObject(q2, "instructions", "评估当前走子方的攻势");
    cJSON *c2 = cJSON_CreateObject();
    cJSON_AddStringToObject(c2, "defend", "守势");
    cJSON_AddStringToObject(c2, "even", "均势");
    cJSON_AddStringToObject(c2, "attack", "攻势");
    cJSON_AddItemToObject(q2, "criteria", c2);
    cJSON_AddItemToObject(qs, "tempo", q2);
    cJSON *q3 = cJSON_CreateObject();
    cJSON_AddStringToObject(q3, "type", "choice");
    cJSON_AddStringToObject(q3, "instructions", "判断当前局面处于哪个阶段");
    cJSON *c3 = cJSON_CreateObject();
    cJSON_AddStringToObject(c3, "opening", "开局");
    cJSON_AddStringToObject(c3, "middle", "中局");
    cJSON_AddStringToObject(c3, "endgame", "残局");
    cJSON_AddItemToObject(q3, "criteria", c3);
    cJSON_AddItemToObject(qs, "phase", q3);
    cJSON_AddItemToObject(body, "questions", qs);

    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!body_str) return false;

    esp_http_client_config_t cfg = {
        .host = "api.typesafe.ai",
        .path = "/v1/systemone",
        .method = HTTP_METHOD_POST,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .timeout_ms = COMM_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event_handler,
        .user_data = NULL,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) { free(body_str); return false; }
    char auth[128];
    snprintf(auth, sizeof(auth), "Bearer %s", MASTER_AI_KEY);
    esp_http_client_set_header(cli, "Authorization", auth);
    esp_http_client_set_header(cli, "Content-Type", "application/json");
    esp_http_client_set_post_field(cli, body_str, (int)strlen(body_str));

    s_resp_len = 0;
    esp_err_t err = esp_http_client_perform(cli);
    int status = esp_http_client_get_status_code(cli);
    esp_http_client_cleanup(cli);
    free(body_str);
    int total = s_resp_len;
    s_resp[total] = 0;
    ESP_LOGI(TAG, "perform: err=%s status=%d total=%d", esp_err_to_name(err), status, total);

    if (err != ESP_OK || status != 200 || total == 0) {
        ESP_LOGW(TAG, "request failed: err=%s status=%d total=%d resp=%.200s",
                 esp_err_to_name(err), status, total, s_resp);
        return false;
    }

    cJSON *root = cJSON_Parse(s_resp);
    if (!root) { ESP_LOGW(TAG, "cJSON parse failed: %.200s", s_resp); return false; }
    cJSON *answers = cJSON_GetObjectItem(root, "answers");
    if (!answers) { cJSON_Delete(root); return false; }
    cJSON *b = cJSON_GetObjectItem(answers, "better");
    cJSON *tp = cJSON_GetObjectItem(answers, "tempo");
    cJSON *ph = cJSON_GetObjectItem(answers, "phase");
    const char *bk = b ? cJSON_GetStringValue(cJSON_GetObjectItem(b, "choice")) : NULL;
    const char *tk = tp ? cJSON_GetStringValue(cJSON_GetObjectItem(tp, "choice")) : NULL;
    const char *pk = ph ? cJSON_GetStringValue(cJSON_GetObjectItem(ph, "choice")) : NULL;

    const char *bs = map_better(bk);
    const char *ts = map_tempo(tk);
    const char *ps = map_phase(pk);
    int w = snprintf(text, text_sz, "%s", bs);
    if (ts && w >= 0) w = snprintf(text + w, text_sz - w, "·%s", ts);
    if (ps[0] && w >= 0) snprintf(text + strlen(text), text_sz - strlen(text), "·%s", ps);
    cJSON_Delete(root);
    ESP_LOGI(TAG, "commentary ok: %s", text);
    return true;
}

static void commentary_task(void *arg) {
    (void)arg;
    for (;;) {
        uint32_t v = 0;
        if (xTaskNotifyWait(0, UINT32_MAX, &v, portMAX_DELAY) != pdTRUE) continue;
        if (v == 1) break;                       /* STOP */
        if (!master_key_is_valid()) {
            continue;       /* 社区版无 key:不显示棋评(label 保持空) */
        }
        if (!chess_wifi_is_connected()) {
            call_cb("棋评不可用");
            continue;
        }
        char state[768];
        build_state(state, sizeof(state), s_board_snap, s_turn_snap, s_moves_snap, s_moves_n);
        call_cb("棋评中...");
        char text[48];
        if (s_cancel) continue;
        if (query_jev(state, text, sizeof(text))) {
            call_cb(text);
        } else {
            call_cb("棋评不可用");
        }
    }
    if (s_stopped) xSemaphoreGive(s_stopped);
    s_task = NULL;
    vTaskDelete(NULL);
}

void commentary_init(commentary_cb_t cb, void *user) {
    s_cb = cb;
    s_cb_user = user;
    if (s_task) return;
    if (s_stopped) { vSemaphoreDelete(s_stopped); s_stopped = NULL; }
    s_stopped = xSemaphoreCreateBinary();
    if (!s_stopped) return;
    s_cancel = false;
    xTaskCreate(commentary_task, "commentary", COMM_TASK_STACK / sizeof(StackType_t),
                NULL, 4, &s_task);
}

void commentary_request(const chess_sq_t *board, int8_t turn,
                        const chess_move_t *moves, int n) {
    if (!s_task || n < 0) return;
    if (n > COMM_MAX_MOVES) n = COMM_MAX_MOVES;
    memcpy(s_board_snap, board, sizeof(s_board_snap));
    s_turn_snap = turn;
    if (n > 0) memcpy(s_moves_snap, moves, sizeof(chess_move_t) * n);
    s_moves_n = n;
    xTaskNotify(s_task, 0, eNoAction);
}

void commentary_stop(void) {
    TaskHandle_t task = s_task;
    if (!task) {
        if (s_stopped) { vSemaphoreDelete(s_stopped); s_stopped = NULL; }
        return;
    }
    s_cancel = true;
    xTaskNotify(task, 1, eSetValueWithOverwrite);   /* STOP */
    if (!s_stopped ||
        xSemaphoreTake(s_stopped, pdMS_TO_TICKS(COMM_STOP_TIMEOUT)) != pdTRUE) {
        vTaskDelete(task);
        s_task = NULL;
    }
    if (s_stopped) { vSemaphoreDelete(s_stopped); s_stopped = NULL; }
    s_cancel = false;
}
