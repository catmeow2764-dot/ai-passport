[简体中文](CONTEXT.zh_CN.md) · **English**

# AI Passport Chinese Chess

A Chinese Chess (Xiangqi) app on ESP32-C3: local rules engine + local AI + optional online JEV commentary.

## Glossary

**Local AI**:
negamax + alpha-beta + quiescence, three difficulty levels (beginner/intermediate/advanced), pure offline decision.
_Avoid_: offline AI, built-in AI

**JEV Commentary**:
Online call to the TypeSafe JEV API for positional semantic evaluation (who is better / tempo / phase), shown to the player as commentary once per turn. Does not participate in move decisions. Community build without a key shows "commentary unavailable" (no local fallback).
_Avoid_: JEV narration, AI review

**Community Build**:
The publicly released firmware, `MASTER_AI_KEY` empty, JEV commentary shows "unavailable".
_Avoid_: public build, release build

**Self-compiled Build**:
Firmware compiled locally by the author/user with a valid key filled in, JEV commentary enabled. Not publicly released.
_Avoid_: private build, author build

**JEV Decision** (rejected):
A `choice` question of the TypeSafe systemone API: input positional text + candidate-move criteria, returns the selected move key. Validated as unsuitable for chess (JEV is not a calculator); abandoned in favor of local AI.
_Avoid_: JEV model (confuses with LLM)

**Move Candidates** (rejected):
Top-N legal moves filtered by local negamax, submitted as JEV choice criteria. Belonged to the rejected JEV-decision design.
_Avoid_: candidate set, candidate list

**Key Placeholder**:
`#define MASTER_AI_KEY ""` in `chess_master_key.h`, committed to git, used by community build; a real key file is gitignored and locally included.
_Avoid_: empty key, template key
