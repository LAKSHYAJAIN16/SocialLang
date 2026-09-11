using System.Collections;
using System.Collections.Generic;
using System.IO;
using Newtonsoft.Json.Linq;
using UnityEngine;

namespace SocialLangViewer
{
    /// <summary>
    /// Reads a results/*.json file saved by `sociallang run` (see results.py /
    /// Interpreter.run()'s returned dict) and paces it back out through the exact
    /// same SimulationState.Apply() pipeline LiveBridgeClient uses -- so the rest of
    /// the viewer (renderers, inspector UI) doesn't know or care whether it's
    /// watching a live run or a replay.
    ///
    /// Honest limitation: a saved run only has the FINAL agent positions (Interpreter
    /// doesn't persist a snapshot per round into the JSON, only the live stream does),
    /// so replay mode shows the world layout and the full event feed scrolling by at
    /// the right pace, but agents don't animate between locations -- they appear at
    /// their end-of-run position. Full movement playback needs --live.
    /// </summary>
    public class ReplayFileReader : MonoBehaviour
    {
        public SimulationHub hub;

        [Tooltip("Log entries replayed per second -- a pacing knob, not a physics rate.")]
        public float eventsPerSecond = 8f;

        private Coroutine _playing;

        public void LoadAndPlay(string path)
        {
            if (hub == null) hub = SimulationHub.Instance;
            if (string.IsNullOrWhiteSpace(path) || !File.Exists(path))
            {
                Debug.LogWarning($"[ReplayFileReader] file not found: '{path}'");
                return;
            }
            if (_playing != null) StopCoroutine(_playing);
            _playing = StartCoroutine(PlayRoutine(path));
        }

        public void Stop()
        {
            if (_playing != null) StopCoroutine(_playing);
            _playing = null;
        }

        private IEnumerator PlayRoutine(string path)
        {
            string json = File.ReadAllText(path);
            JObject root = JObject.Parse(json);
            hub.state.Reset();

            JToken worldToken = root["world"];
            if (worldToken != null && worldToken.Type != JTokenType.Null)
            {
                var worldMsg = worldToken.ToObject<EventMsg>();
                worldMsg.type = "world";
                hub.state.Apply(worldMsg);
            }

            // Applied BEFORE the log pacing loop below, not after -- a saved run only
            // has final agent positions (see the class doc comment), so the honest
            // way to show that is agents visible at their end-of-run spot for the
            // whole replay, not an empty map until the last coroutine step. Applying
            // this last would leave SimulationState.Agents empty (and every renderer/
            // the inspector reading it finding nothing) for the entire scrolling
            // phase on any run with more than a handful of events.
            if (root["agents"] is JArray agentsToken)
            {
                var snapshot = new EventMsg
                {
                    type = "agents_snapshot",
                    agents = agentsToken.ToObject<List<AgentState>>(),
                };
                hub.state.Apply(snapshot);
            }

            float delay = eventsPerSecond > 0f ? 1f / eventsPerSecond : 0f;
            if (root["log"] is JArray log)
            {
                foreach (JToken entry in log)
                {
                    var msg = entry.ToObject<EventMsg>();
                    msg.type = "event";
                    hub.state.Apply(msg);
                    if (delay > 0f) yield return new WaitForSeconds(delay);
                }
            }

            var doneMsg = new EventMsg
            {
                type = "done",
                winner = root["winner"]?.ToObject<object>(),
                rounds = root["rounds"]?.ToObject<int>() ?? 0,
            };
            hub.state.Apply(doneMsg);
            _playing = null;
        }
    }
}
