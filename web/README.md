# SocialLang Precinct — browser IDE

A single self-contained HTML page (`index.html`) that ports SocialLang's lexer,
parser, and interpreter to JavaScript so you can write and run a `.sl` program
entirely client-side — no Python, no server, no API keys. It's a faithful port of
`sociallang/lang/{lexer,parser,interpreter,memory}.py`: same grammar, same builtins,
same event/memory semantics (including `world {}` and the bulk `ask_all`/
`ask_choice_all` builtins).

Open it directly:

```
python -m http.server 8000 --directory web
```

then visit `http://localhost:8000/`. (Opening the file directly via `file://` also
works in most browsers; a local server just avoids any browser-specific file:// quirks.)

Also published as a Claude Artifact for quick sharing — same file, hosted.

## What's different from the real thing

- **No real LLM calls.** A public page can't safely hold API keys, so every `ask()`/
  `ask_choice()` is answered by a mock provider (picks a legal option when one's
  offered, otherwise a generic line) — matching
  `sociallang/providers/mock_provider.py`'s behavior exactly.
- **No real embeddings.** `generative(k)` memory uses the lexical (Jaccard-overlap)
  relevance path only — the real embedding providers in
  `sociallang/providers/embeddings.py` have no browser equivalent here.
- **`ask_all`/`ask_choice_all` run sequentially**, not concurrently — there's nothing
  to parallelize against a synchronous in-page mock provider. The Python
  implementation's thread-pooled concurrency (see DESIGN.md) is what actually matters
  for real API latency at scale.
- **No spatial rendering.** `world {}` positions are computed exactly as the real
  interpreter computes them, but this page is a text console, not a map — see the
  repo's `unity/` viewer for that.

## Files

- `index.html` — the whole thing: engine, UI, and the five `games/*.sl` example
  files (base64-embedded, decoded at load time so quoting inside their comments
  can't collide with the page's own JavaScript). Editing this file means editing
  the JS engine, the editor/syntax-highlighting UI, and the embedded example
  sources all in one place — there's no build step.

Keeping this in sync with the real language: any change to
`sociallang/lang/{lexer,parser,interpreter,memory}.py`'s grammar or builtins should
be mirrored here too, since this is a hand-maintained port, not a generated one.
