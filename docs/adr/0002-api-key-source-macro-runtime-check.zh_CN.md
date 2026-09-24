**简体中文** · [English](0002-api-key-source-macro-runtime-check.md)

# API key:源码宏 + 运行时格式检测,不入 git

状态:已接受

JEV 棋评的 TypeSafe API key 存于 `chess_master_key.h` 的 `#define MASTER_AI_KEY`,占位值(空串)进 git 供社区版编译;真 key 文件 `chess_master_key_secret.h` gitignore,自编译版本地 include。运行时 `master_key_is_valid()` 检测宏值非空且格式合法(前缀+长度)决定 JEV 棋评是否尝试联网;棋评网络代码始终编译进 bin(不 `#ifdef` 排除),社区版 bin 含网络逻辑但无 key。社区版(无 key)显示"棋评不可用"(不本地简化,设计如此);自编译版(合法 key)调 JEV API 评局面。

防泄漏三道:占位进 git + 真 key gitignore + push 前 `grep -r` 扫源码与 build 产物确保无 key 字面量。选运行时检测而非编译时 `#ifdef` 排除,是为单 bin 逻辑(填 key 重编译即启用);代价是社区版 bin 暴露公开 API 端点(非秘密,可接受)。原为大师 AI 设计,JEV 决策否决后迁移到棋评,原理不变。
