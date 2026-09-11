using System;
using System.Collections.Generic;

namespace SocialLangViewer
{
    // Mirrors sociallang/engine/live.py's message schema field-for-field. Every field
    // that can be Python `None` is a nullable/reference type here on purpose -- Json.NET
    // throws converting an explicit JSON null into a non-nullable value type (e.g. `int`),
    // and several of these genuinely are null sometimes (e.g. print-kind log entries have
    // seq: null, an agent that never spawned has x/y: null).

    [Serializable]
    public class AgentState
    {
        public string seat;
        public string role;
        public string team;
        public string model;
        public bool alive;
        public string death_cause;
        public float? x;
        public float? y;
        public string location_id;
    }

    [Serializable]
    public class LocationState
    {
        public string id;
        public string type;
        public string tag;
        public int? capacity;
        public float x;
        public float y;
    }

    // One struct covers every message shape (world/agents_snapshot/event/done) --
    // simplest way to deserialize a schema that varies by `type` with Json.NET without
    // hand-writing a converter; unused fields for a given type are just left at default.
    [Serializable]
    public class EventMsg
    {
        public string type;

        // event
        public int? seq;
        public int round;
        public string kind;
        public string text;
        public string author;
        public List<string> visible_to;

        // world
        public int width;
        public int height;
        public List<LocationState> locations;

        // agents_snapshot
        public List<AgentState> agents;

        // done
        public object winner;
        public int rounds;
    }

    /// <summary>
    /// The single source of truth for what a viewer currently knows about a
    /// simulation, fed by either LiveBridgeClient (as messages arrive over the
    /// WebSocket bridge) or ReplayFileReader (paced out of a saved results/*.json).
    /// Renderers and UI read from here; nothing else touches the network or the
    /// filesystem directly.
    /// </summary>
    public class SimulationState
    {
        public int WorldWidth { get; private set; }
        public int WorldHeight { get; private set; }
        public readonly Dictionary<string, LocationState> Locations = new Dictionary<string, LocationState>();
        public readonly Dictionary<string, AgentState> Agents = new Dictionary<string, AgentState>();

        public const int MaxEventsPerAgent = 30;
        public readonly Dictionary<string, List<EventMsg>> RecentEventsByAgent = new Dictionary<string, List<EventMsg>>();

        public object Winner { get; private set; }
        public int Rounds { get; private set; }
        public bool IsDone { get; private set; }

        public event Action OnWorldChanged;
        public event Action OnAgentsChanged;
        public event Action<EventMsg> OnEvent;
        public event Action OnDone;

        public void Reset()
        {
            WorldWidth = 0;
            WorldHeight = 0;
            Locations.Clear();
            Agents.Clear();
            RecentEventsByAgent.Clear();
            Winner = null;
            Rounds = 0;
            IsDone = false;
        }

        public void Apply(EventMsg msg)
        {
            if (msg == null || string.IsNullOrEmpty(msg.type)) return;

            switch (msg.type)
            {
                case "world":
                    WorldWidth = msg.width;
                    WorldHeight = msg.height;
                    Locations.Clear();
                    if (msg.locations != null)
                    {
                        foreach (var loc in msg.locations) Locations[loc.id] = loc;
                    }
                    OnWorldChanged?.Invoke();
                    break;

                case "agents_snapshot":
                    // Every agents_snapshot is the COMPLETE current roster (see
                    // Interpreter._agents_snapshot() on the Python side -- it always
                    // builds from the full self.agents list, never a delta), so this
                    // has to replace, not merge. `sociallang run --games N --live`
                    // reuses one WebSocketSink across multiple interp.run() calls
                    // (cli.py), and a role like `agents: 6..10` redraws a different
                    // population size each game -- merging without clearing would
                    // leave a previous game's now-nonexistent seats frozen in here
                    // forever, rendered/tracked alongside the current game's roster.
                    Agents.Clear();
                    if (msg.agents != null)
                    {
                        foreach (var a in msg.agents) Agents[a.seat] = a;
                    }
                    OnAgentsChanged?.Invoke();
                    break;

                case "event":
                    if (!string.IsNullOrEmpty(msg.author))
                    {
                        if (!RecentEventsByAgent.TryGetValue(msg.author, out var list))
                        {
                            list = new List<EventMsg>();
                            RecentEventsByAgent[msg.author] = list;
                        }
                        list.Add(msg);
                        if (list.Count > MaxEventsPerAgent) list.RemoveAt(0);
                    }
                    OnEvent?.Invoke(msg);
                    break;

                case "done":
                    Winner = msg.winner;
                    Rounds = msg.rounds;
                    IsDone = true;
                    OnDone?.Invoke();
                    break;
            }
        }
    }
}
