#ifndef CHESS_WIFI_H
#define CHESS_WIFI_H
#include <stdbool.h>

/* 开发期硬编码 Wi-Fi(方向 A,验证 JEV 棋评联网):
 * - SSID/密码占位空进 git,社区版不连接(JEV 棋评显示"不可用")
 * - 真 SSID/密码放 chess_wifi_secret.h(gitignore),自编译版本地 include */
#ifndef WIFI_SSID
#  if __has_include("chess_wifi_secret.h")
#    include "chess_wifi_secret.h"
#  else
#    define WIFI_SSID ""
#    define WIFI_PASS ""
#  endif
#endif

/* 初始化 Wi-Fi(异步,后台连接)。SSID 空则不启动(社区版) */
void chess_wifi_init(void);

/* 是否已获 IP(联网就绪) */
bool chess_wifi_is_connected(void);

#endif
