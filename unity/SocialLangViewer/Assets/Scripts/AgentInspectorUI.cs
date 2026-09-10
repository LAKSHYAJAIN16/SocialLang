using System.Linq;
using System.Text;
using UnityEngine;
using UnityEngine.UI;

namespace SocialLangViewer
{
    /// <summary>
    /// Left-click the nearest agent to see its seat/role/team/status and recent
    /// events in a side panel. Built entirely at runtime (see Create()) so no scene
    /// or prefab needs to be hand-authored.
    /// </summary>
    public class AgentInspectorUI : MonoBehaviour
    {
        private SimulationHub _hub;
        private Text _label;
        private Camera _cam;
        private string _selectedSeat;

        public static AgentInspectorUI Create(SimulationHub hub)
        {
            GameObject canvasGo = new GameObject("InspectorCanvas");
            var canvas = canvasGo.AddComponent<Canvas>();
            canvas.renderMode = RenderMode.ScreenSpaceOverlay;
            canvasGo.AddComponent<CanvasScaler>();
            canvasGo.AddComponent<GraphicRaycaster>();

            GameObject panelGo = new GameObject("Panel");
            panelGo.transform.SetParent(canvasGo.transform, false);
            panelGo.AddComponent<Image>().color = new Color(0f, 0f, 0f, 0.55f);
            var panelRect = panelGo.GetComponent<RectTransform>();
            panelRect.anchorMin = new Vector2(1f, 0f);
            panelRect.anchorMax = new Vector2(1f, 1f);
            panelRect.pivot = new Vector2(1f, 0.5f);
            panelRect.sizeDelta = new Vector2(340f, 0f);
            panelRect.anchoredPosition = Vector2.zero;

            GameObject textGo = new GameObject("Label");
            textGo.transform.SetParent(panelGo.transform, false);
            var text = textGo.AddComponent<Text>();
            text.font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
            text.fontSize = 15;
            text.alignment = TextAnchor.UpperLeft;
            text.color = Color.white;
            text.horizontalOverflow = HorizontalWrapMode.Wrap;
            text.verticalOverflow = VerticalWrapMode.Overflow;
            var textRect = textGo.GetComponent<RectTransform>();
            textRect.anchorMin = Vector2.zero;
            textRect.anchorMax = Vector2.one;
            textRect.offsetMin = new Vector2(12f, 12f);
            textRect.offsetMax = new Vector2(-12f, -12f);

            var behaviour = canvasGo.AddComponent<AgentInspectorUI>();
            behaviour._hub = hub;
            behaviour._label = text;
            behaviour._label.text = "Click an agent to inspect it.\nRight/middle-drag to pan, scroll to zoom.";
            return behaviour;
        }

        private void Update()
        {
            if (_cam == null) _cam = Camera.main;
            if (_cam == null || _hub == null) return;

            if (Input.GetMouseButtonDown(0))
            {
                Vector3 world = _cam.ScreenToWorldPoint(Input.mousePosition);
                _selectedSeat = FindNearestAgent(world, maxDistance: 3f);
            }

            RefreshLabel();
        }

        private string FindNearestAgent(Vector3 world, float maxDistance)
        {
            string best = null;
            float bestDist = maxDistance;
            foreach (var kv in _hub.state.Agents)
            {
                AgentState a = kv.Value;
                if (!a.x.HasValue || !a.y.HasValue) continue;
                float d = Vector2.Distance(new Vector2(a.x.Value, a.y.Value), new Vector2(world.x, world.y));
                if (d < bestDist)
                {
                    bestDist = d;
                    best = kv.Key;
                }
            }
            return best;
        }

        private void RefreshLabel()
        {
            if (string.IsNullOrEmpty(_selectedSeat) || !_hub.state.Agents.TryGetValue(_selectedSeat, out AgentState agent))
            {
                return;
            }

            var sb = new StringBuilder();
            sb.AppendLine($"<b>{agent.seat}</b>  ({(agent.alive ? "alive" : "eliminated")})");
            sb.AppendLine($"role: {agent.role}");
            sb.AppendLine($"team: {agent.team}");
            if (!agent.alive && !string.IsNullOrEmpty(agent.death_cause))
            {
                sb.AppendLine($"cause: {agent.death_cause}");
            }
            if (!string.IsNullOrEmpty(agent.location_id))
            {
                sb.AppendLine($"at: {agent.location_id}");
            }
            sb.AppendLine();
            sb.AppendLine("<b>recent events</b>");

            if (_hub.state.RecentEventsByAgent.TryGetValue(_selectedSeat, out var events) && events.Count > 0)
            {
                foreach (EventMsg e in events.AsEnumerable().Reverse().Take(12))
                {
                    sb.AppendLine($"[r{e.round}] {e.text}");
                }
            }
            else
            {
                sb.AppendLine("(none yet)");
            }

            _label.text = sb.ToString();
        }
    }
}
