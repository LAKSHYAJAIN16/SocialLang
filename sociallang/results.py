from __future__ import annotations

import json
import os
from datetime import datetime, timezone

from .visualize import render_html


class ResultsLogger:
    def __init__(self, results_dir: str):
        self.results_dir = results_dir
        os.makedirs(results_dir, exist_ok=True)

    def save_run(self, sim_name: str, run_index: int, result: dict) -> str:
        timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        run_id = f"{sim_name}_{run_index:04d}_{timestamp}"

        with open(os.path.join(self.results_dir, f"{run_id}.json"), "w", encoding="utf-8") as f:
            json.dump({"sim_name": sim_name, **result}, f, indent=2, default=str)

        with open(os.path.join(self.results_dir, f"{run_id}.html"), "w", encoding="utf-8") as f:
            f.write(render_html(sim_name, result))

        return run_id
