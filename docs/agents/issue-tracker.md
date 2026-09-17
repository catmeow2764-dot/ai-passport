<p align="right">
  <a href="issue-tracker.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Issue tracker: Local Markdown

Issues and specs for this repo are stored as markdown files under `.scratch/`.

## Conventions

- One directory per feature: `.scratch/<feature-slug>/`
- Spec: `.scratch/<feature-slug>/spec.md`
- Implementation issues: one file per ticket, at `.scratch/<feature-slug>/issues/<NN>-<slug>.md`, numbered from `01`; never a combined tickets file
- Triage state is recorded as a `Status:` line near the top of each issue file (role strings in `triage-labels.md`)
- Comments and conversation history are appended under a `## Comments` heading at the bottom of the file

## When a skill says "publish to the issue tracker"

Create a new file under `.scratch/<feature-slug>/` (create the directory if needed).

## When a skill says "fetch the relevant ticket"

Read the file at the referenced path. Users usually pass the path or issue number directly.

## Wayfinding operations

For `/wayfinder` use. The **map** is one file, each ticket is a **child** file.

- **Map**: `.scratch/<effort>/map.md` — Notes / Decisions-so-far / Fog body.
- **Child ticket**: `.scratch/<effort>/issues/NN-<slug>.md`, numbered from `01`, with the question in the body. A `Type:` line records the ticket type (`research`/`prototype`/`grilling`/`task`); a `Status:` line records `claimed`/`resolved`.
- **Blocking**: a `Blocked by: NN, NN` line near the top. A ticket is unblocked when every file it lists is `resolved`.
- **Frontier**: scan `.scratch/<effort>/issues/` for files that are open, unblocked, and unclaimed; the first by number wins.
- **Claim**: set `Status: claimed` and save before any work begins.
- **Resolve**: append the answer under a `## Answer` heading, set `Status: resolved`, then append a context pointer (gist + link) to Decisions-so-far in `map.md`.
