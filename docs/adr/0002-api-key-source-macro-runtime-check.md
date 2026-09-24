[简体中文](0002-api-key-source-macro-runtime-check.zh_CN.md) · **English**

# API key: source-file macro + runtime format check, never in git

Status: Accepted

The JEV commentary's TypeSafe API key lives in `chess_master_key.h` as `#define MASTER_AI_KEY`, with a placeholder value (empty string) committed to git for community builds; the real key file `chess_master_key_secret.h` is gitignored and locally included for self-compiled builds. At runtime `master_key_is_valid()` checks the macro is non-empty and format-valid (prefix + length) to decide whether JEV commentary attempts to go online; the commentary network code is always compiled into the binary (no `#ifdef` exclusion), so the community binary contains the network logic but no key. The community build (no key) shows "commentary unavailable" (no local fallback, by design); the self-compiled build (valid key) calls the JEV API to evaluate the position.

Three leak guards: placeholder in git + real key gitignored + a pre-push `grep -r` scan of sources and build artifacts to ensure no key literal. Runtime detection rather than compile-time `#ifdef` exclusion keeps a single-binary story (fill the key, recompile, it works); the cost is that the community binary exposes the public API endpoint (not secret, acceptable). Originally designed for the master AI, it migrated to commentary after the JEV decision was rejected; the principle is unchanged.
