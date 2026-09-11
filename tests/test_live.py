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


def test_broadcast_sync_does_not_crash_when_a_slow_client_times_out(monkeypatch):
    # _broadcast_sync waits up to 5s for the broadcast coroutine; a slow/stuck
    # client's ws.send() can block past that via websockets' own backpressure
    # handling. That used to raise TimeoutError straight out of emit_event() and
    # crash the whole simulation -- simulate it (without an actual 5s wait) and
    # confirm every emit_* call now just swallows it instead.
    import sociallang.engine.live as live_module

    class FakeFuture:
        def result(self, timeout=None):
            raise TimeoutError("simulated slow/stuck client")

    def fake_run_coroutine_threadsafe(coro, loop):
        coro.close()  # avoid an "coroutine was never awaited" warning
        return FakeFuture()

    sink = WebSocketSink(host="localhost", port=0)
    try:
        monkeypatch.setattr(live_module.asyncio, "run_coroutine_threadsafe", fake_run_coroutine_threadsafe)
        sink.emit_world(10, 10, [])
        sink.emit_agents_snapshot([])
        sink.emit_event({"seq": 1, "round": 1, "kind": "broadcast", "text": "hi", "author": None, "visible_to": None})
        sink.emit_done("done", 1)
    finally:
        monkeypatch.undo()
        sink.close()


def test_late_connecting_client_receives_catch_up_world_and_agents_snapshot():
    # Interpreter.run() emits "world"/"agents_snapshot" exactly once each, right
    # before the round loop starts -- a client connecting (or reconnecting) any
    # time after that used to just never see them. Simulate a run that already
    # emitted its initial state before any viewer connected, then connect one.
    sink = WebSocketSink(host="localhost", port=0)
    try:
        sink.emit_world(10, 10, [{"id": "Spot_0", "type": "Spot", "tag": "x", "capacity": None, "x": 1.0, "y": 2.0}])
        sink.emit_agents_snapshot([
            {"seat": "P1", "role": "P", "team": "t", "model": "m", "alive": True,
             "death_cause": None, "x": 1.0, "y": 2.0, "location_id": "Spot_0"},
        ])

        received: list[dict] = []

        async def client_loop() -> None:
            uri = f"ws://localhost:{sink.port}"
            async with websockets.connect(uri) as ws:
                for _ in range(2):
                    msg = await asyncio.wait_for(ws.recv(), timeout=5)
                    received.append(json.loads(msg))

        asyncio.run(client_loop())
    finally:
        sink.close()

    assert [m["type"] for m in received] == ["world", "agents_snapshot"]
    assert received[0]["locations"][0]["id"] == "Spot_0"
    assert received[1]["agents"][0]["seat"] == "P1"


def test_print_reaches_the_live_sink_like_every_other_event():
    # print() used to only append to run_log, bypassing the sink entirely -- so it
    # showed up when a saved run was replayed (it's right there in the JSON's "log")
    # but never reached a --live viewer watching the identical run in progress.
    class RecordingSink:
        def __init__(self):
            self.events = []

        def emit_world(self, width, height, locations):
            pass

        def emit_agents_snapshot(self, agents):
            pass

        def emit_event(self, event):
            self.events.append(event)

        def emit_done(self, winner, rounds):
            pass

    source = """
    sim PrintTest {
      agents: 1
      role Solo { team: "t" memory: full_history sees: none count: 1 }
      win_condition { return "done" }
      loop {
        print("hello from print")
        return check_win()
      }
    }
    """
    roster = {"a": (None, FixedProvider("ok"))}
    sink = RecordingSink()
    run_source(source, roster, seed=0, max_rounds=1, sink=sink)
    assert any(e["kind"] == "print" and e["text"] == "hello from print" for e in sink.events)


def test_live_bridge_agents_snapshot_reflects_spawned_positions():
    received = _collect_messages_while_running(SOURCE, max_rounds=1)
    snapshots = [m for m in received if m["type"] == "agents_snapshot"]
    assert snapshots
    last = snapshots[-1]["agents"]
    assert len(last) == 2
    assert all(a["x"] is not None and a["y"] is not None for a in last)
