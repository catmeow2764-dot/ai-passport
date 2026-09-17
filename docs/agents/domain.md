<p align="right">
  <a href="domain.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Domain Docs

How engineering skills consume this repo's domain documentation when exploring the codebase.

## Before exploring, read these

- **`CONTEXT.md`** at the repo root, or
- **`CONTEXT-MAP.md`** at the repo root (if it exists) — it points to one `CONTEXT.md` per context. Read each file relevant to the current topic.
- **`docs/adr/`** — read ADRs relevant to the area you are about to work on. In multi-context repos, also check `src/<context>/docs/adr/` for context-scoped decisions.

If these files do not exist, **continue silently**. Do not flag the absence; do not suggest creating them up front. The `/domain-modeling` skill (via `/grill-with-docs` and `/improve-codebase-architecture`) lazily creates them when terms or decisions are actually settled.

## File structure

Single-context repo (most repos):

```
/
├── CONTEXT.md
├── docs/adr/
│   ├── 0001-event-sourced-orders.md
│   └── 0002-postgres-for-write-model.md
└── src/
```

Multi-context repo (root has `CONTEXT-MAP.md`):

```
/
├── CONTEXT-MAP.md
├── docs/adr/                          ← system-wide decisions
└── src/
    ├── ordering/
    │   ├── CONTEXT.md
    │   └── docs/adr/                  ← context-specific decisions
    └── billing/
        ├── CONTEXT.md
        └── docs/adr/
```

## Use the glossary's vocabulary

When your output names a domain concept (issue title, refactor proposal, hypothesis, test name), use the term defined in `CONTEXT.md`. Do not drift to synonyms the glossary explicitly avoids.

If a concept you need is not yet in the glossary, that is a signal: either you are inventing language the project does not use (reconsider), or there is a genuine gap (capture it for `/domain-modeling`).

## Flag ADR conflicts

If your output contradicts an existing ADR, call it out explicitly rather than silently overriding:

> _Contradicts ADR-0007 (event-sourced orders) — but worth reopening because…_
