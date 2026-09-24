[简体中文](0001-master-ai-uses-jev-decision-api.zh_CN.md) · **English**

# JEV decision rejected for chess; JEV repurposed as commentary

Status: Rejected (original plan) / Accepted (repurpose)

The original plan was an online "master AI" that called the TypeSafe JEV decision API to pick the best move from candidates. Validation rejected it: the JEV docs state plainly that it "is not a calculator" and "struggles with tasks that require numeric precision or additional levels of indirection" — exactly the weaknesses chess tactics (captures, checks, multi-step lookahead) demand. A probe confirmed it: in a tactical endgame (a red chariot could capture an undefended black horse) none of three state proposals chose the capture, matching the documented gap.

The fallback — a local negamax depth boost (depth 6→8) plus an opening book — was also rejected: depth 8 raises stack/time cost on the ESP32-C3 and an opening book needs curated data, while the existing depth-6 "advanced" level is already strong enough.

JEV is instead repurposed as "commentary": a once-per-turn positional semantic evaluation (who is better / tempo / phase) shown to the player, with no role in move decisions. This plays to JEV's strength (semantic judgment) and keeps it out of calculation it cannot do.
