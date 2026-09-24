**简体中文** · [English](CONTEXT.md)

# AI Passport 中国象棋

ESP32-C3 上的中国象棋 app:本地规则引擎 + 本地 AI + 可选联网 JEV 棋评。

## 术语

**本地 AI (Local AI)**:
negamax+alpha-beta+quiescence,三档难度(初级/中级/高级),纯离线决策。
_Avoid_: 离线 AI,内置 AI

**JEV 棋评 (JEV Commentary)**:
联网调 TypeSafe JEV API 做局面语义评估(谁占优/攻势/阶段),每回合一次显示给玩家作棋评。不参与走法决策。社区版无 key 时显示"棋评不可用"(不本地简化)。
_Avoid_: JEV 解说,AI 评棋

**社区版 (Community Build)**:
公开发布的固件,`MASTER_AI_KEY` 为空,JEV 棋评显示"不可用"。
_Avoid_: 公开版,发布版

**自编译版 (Self-compiled Build)**:
作者/用户本地填入有效 key 编译的固件,JEV 棋评启用。不公开发布。
_Avoid_: 私有版,作者版

**JEV 决策 (JEV Decision)**(已否决):
TypeSafe systemone API 的 `choice` question:输入局面文本 + 候选走法 criteria,返回选中的走法 key。验证为不适合象棋(JEV 不擅长计算);放弃,改用本地 AI。
_Avoid_: JEV 模型(易与 LLM 混淆)

**走法候选 (Move Candidates)**(已否决):
本地 negamax 筛选的 top-N 合法走法,作为 JEV choice criteria 提交。属已否决的 JEV 决策设计。
_Avoid_: 候选集,候选列表

**key 占位 (Key Placeholder)**:
`chess_master_key.h` 中 `#define MASTER_AI_KEY ""`,进 git,社区版编译用;真 key 文件 gitignore,本地 include。
_Avoid_: 空 key,模板 key
