import asyncio
import json
import threading
import time

import websockets

from sociallang.engine.live import WebSocketSink
from sociallang.lang.interpreter import run_source
from sociallang.providers.base import ChatProvider, ProviderResponse


class FixedProvider(ChatProvider):
    def __init__(self, text: str):
        super().__init__(model_id="fixed", api_key=None)
        self.text = text

    def complete(self, system_prompt, user_prompt, temperature=0.9, max_tokens=500, timeout=60):
        return ProviderResponse(text=self.text)


SOURCE = """
sim LiveTest {
  agents: 2
  role P { team: "t" memory: full_history sees: none count: 2 }
  world {
    width: 10
    height: 10
    location Spot { tag: "x", count: 2 }
  }
  win_condition { return "done" }
  loop {
    spawn_agents_at(alive())
    broadcast("round happened")
    return check_win()
  }
}
"""


def _collect_messages_while_running(sim_source: str, max_rounds: int) -> list[dict]:
    sink = WebSocketSink(host="localhost", port=0)
    received: list[dict] = []
    ready = threading.Event()

    async def client_loop() -> None:
        uri = f"ws://localhost:{sink.port}"
        async with websockets.connect(uri) as ws:
            ready.set()
            while True:
                try:
                    msg = await asyncio.wait_for(ws.recv(), timeout=5)
                except asyncio.TimeoutError:
                    break
                data = json.loads(msg)
                received.append(data)
                if data.get("type") == "done":
                    break

    client_thread = threading.Thread(target=lambda: asyncio.run(client_loop()))
    try:
        client_thread.start()
        assert ready.wait(timeout=5), "client never connected"
        time.sleep(0.1)  # let the server register the connection before we start emitting

        roster = {f"a{i}": (None, FixedProvider("ok")) for i in range(2)}
        run_source(sim_source, roster, seed=0, max_rounds=max_rounds, sink=sink)

        client_thread.join(timeout=5)
    finally:
        sink.close()
    return received


def test_live_bridge_streams_world_snapshots_events_and_done_in_order():
    received = _collect_messages_while_running(SOURCE, max_rounds=1)
    types = [m["type"] for m in received]

    assert types[0] == "world"
    assert received[0]["locations"] and len(received[0]["locations"]) == 2
    assert "agents_snapshot" in types
    assert "event" in types
    assert types[-1] == "done"
    assert received[-1]["winner"] == "done"

    event_msgs = [m for m in received if m["type"] == "event"]
    assert [m["seq"] for m in event_msgs] == sorted(m["seq"] for m in event_msgs)
    assert any(m["text"] == "round happened" for m in event_msgs)


def test_live_bridge_agents_snapshot_reflects_spawned_positions():
    received = _collect_messages_while_running(SOURCE, max_rounds=1)
    snapshots = [m for m in received if m["type"] == "agents_snapshot"]
    assert snapshots
    last = snapshots[-1]["agents"]
    assert len(last) == 2
    assert all(a["x"] is not None and a["y"] is not None for a in last)
