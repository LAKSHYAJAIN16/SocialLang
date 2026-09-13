"""Renders a run_source() result dict into a single self-contained HTML replay page --
no external assets, so the file works standalone from disk or shared as-is.
"""

from __future__ import annotations

import html
import json

_STYLE = """
body { font-family: -apple-system, Segoe UI, sans-serif; max-width: 900px; margin: 2rem auto;
       padding: 0 1rem; background: #0f1115; color: #e6e6e6; }
h1 { font-size: 1.4rem; }
.meta { color: #9aa0a6; margin-bottom: 1.5rem; }
table { border-collapse: collapse; width: 100%; margin-bottom: 2rem; }
th, td { text-align: left; padding: 0.4rem 0.6rem; border-bottom: 1px solid #2a2d33; font-size: 0.9rem; }
th { color: #9aa0a6; font-weight: 600; }
.alive { color: #7ee787; }
.dead { color: #ff7b72; text-decoration: line-through; }
.event { padding: 0.5rem 0.7rem; margin-bottom: 0.4rem; border-radius: 6px; border-left: 3px solid #3d4148; }
.event.broadcast { border-left-color: #58a6ff; }
.event.whisper { border-left-color: #d29922; background: #1c1a12; }
.event.note { border-left-color: #8957e5; }
.event.reflection { border-left-color: #db61a2; }
.event.plan { border-left-color: #f0883e; }
.event.dialogue { border-left-color: #56d4dd; }
.event.ask { display: none; }
.event.print { border-left-color: #3fb950; }
.event-meta { color: #9aa0a6; font-size: 0.75rem; margin-bottom: 0.15rem; }
.round-divider { color: #565a63; margin: 1.2rem 0 0.4rem; font-size: 0.85rem; letter-spacing: 0.05em; }
.toggle { margin-bottom: 1rem; }
"""


def render_html(sim_name: str, result: dict, show_private: bool = False) -> str:
    agents_rows = "\n".join(
        f'<tr><td>{html.escape(a["seat"])}</td><td>{html.escape(a["role"])}</td>'
        f'<td>{html.escape(a["team"])}</td><td>{html.escape(a["model"])}</td>'
        f'<td class="{"alive" if a["alive"] else "dead"}">'
        f'{"alive" if a["alive"] else html.escape(a["death_cause"] or "eliminated")}</td></tr>'
        for a in result["agents"]
    )

    events_html = []
    last_round = None
    for e in result["log"]:
        if e["round"] != last_round:
            events_html.append(f'<div class="round-divider">Round {e["round"]}</div>')
            last_round = e["round"]
        kind = e["kind"]
        if kind == "ask" and not show_private:
            continue
        visibility = "public" if not e.get("visible_to") else f'visible to: {", ".join(e["visible_to"])}'
        author = f'{html.escape(e["author"])} — ' if e.get("author") else ""
        events_html.append(
            f'<div class="event {html.escape(kind)}">'
            f'<div class="event-meta">{author}{kind} · {visibility}</div>'
            f'<div>{html.escape(e["text"])}</div></div>'
        )

    winner = result.get("winner")
    winner_text = html.escape(str(winner)) if winner is not None else "(no winner -- hit max_rounds)"

    return f"""<!doctype html>
<html><head><meta charset="utf-8"><title>{html.escape(sim_name)} replay</title>
<style>{_STYLE}</style></head>
<body>
<h1>{html.escape(sim_name)}</h1>
<p class="meta">Winner: <strong>{winner_text}</strong> &middot; {result["rounds"]} round(s) &middot;
{len(result["agents"])} agent(s) &middot; {len(result["log"])} logged event(s)</p>

<table>
<tr><th>Seat</th><th>Role</th><th>Team</th><th>Model</th><th>Status</th></tr>
{agents_rows}
</table>

{"".join(events_html)}

<script>
// raw result JSON, for anyone who wants to poke at it in the console
window.__result = {json.dumps(result)};
</script>
</body></html>
"""
