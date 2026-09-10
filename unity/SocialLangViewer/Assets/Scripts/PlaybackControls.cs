using UnityEngine;
using UnityEngine.UI;

namespace SocialLangViewer
{
    /// <summary>
    /// A small top-left panel: a URL field + "Connect (live)" button, and a file-path
    /// field + "Load & play replay file" button. Built entirely at runtime, same as
    /// AgentInspectorUI -- see Create().
    /// </summary>
    public class PlaybackControls : MonoBehaviour
    {
        private LiveBridgeClient _client;
        private ReplayFileReader _replay;
        private InputField _urlField;
        private InputField _pathField;
        private Text _status;

        public static PlaybackControls Create(LiveBridgeClient client, ReplayFileReader replay)
        {
            GameObject canvasGo = new GameObject("PlaybackCanvas");
            var canvas = canvasGo.AddComponent<Canvas>();
            canvas.renderMode = RenderMode.ScreenSpaceOverlay;
            canvasGo.AddComponent<CanvasScaler>();
            canvasGo.AddComponent<GraphicRaycaster>();

            GameObject panelGo = new GameObject("Panel");
            panelGo.transform.SetParent(canvasGo.transform, false);
            panelGo.AddComponent<Image>().color = new Color(0f, 0f, 0f, 0.55f);
            var rect = panelGo.GetComponent<RectTransform>();
            rect.anchorMin = new Vector2(0f, 1f);
            rect.anchorMax = new Vector2(0f, 1f);
            rect.pivot = new Vector2(0f, 1f);
            rect.sizeDelta = new Vector2(380f, 172f);
            rect.anchoredPosition = new Vector2(12f, -12f);

            var layout = panelGo.AddComponent<VerticalLayoutGroup>();
            layout.padding = new RectOffset(10, 10, 10, 10);
            layout.spacing = 6f;
            layout.childControlHeight = false;
            layout.childControlWidth = true;
            layout.childForceExpandWidth = true;

            var behaviour = canvasGo.AddComponent<PlaybackControls>();
            behaviour._client = client;
            behaviour._replay = replay;

            behaviour._urlField = CreateInputField(panelGo.transform, client.url);
            Button connectBtn = CreateButton(panelGo.transform, "Connect (live)");
            connectBtn.onClick.AddListener(() =>
            {
                client.url = behaviour._urlField.text;
                client.Connect();
            });

            behaviour._pathField = CreateInputField(panelGo.transform, "results/Mafia_0000_....json");
            Button playBtn = CreateButton(panelGo.transform, "Load & play replay file");
            playBtn.onClick.AddListener(() => replay.LoadAndPlay(behaviour._pathField.text));

            behaviour._status = CreateLabel(panelGo.transform, "");
            return behaviour;
        }

        private void Update()
        {
            if (_status == null) return;
            bool connected = _client != null && _client.IsConnected;
            string err = _client != null && !string.IsNullOrEmpty(_client.LastError) ? $" ({_client.LastError})" : "";
            _status.text = connected ? "live: connected" : $"live: not connected{err}";
        }

        private static InputField CreateInputField(Transform parent, string placeholder)
        {
            GameObject go = new GameObject("Input");
            go.transform.SetParent(parent, false);
            go.AddComponent<Image>().color = new Color(1f, 1f, 1f, 0.1f);
            go.AddComponent<LayoutElement>().preferredHeight = 24f;
            var input = go.AddComponent<InputField>();

            GameObject textGo = new GameObject("Text");
            textGo.transform.SetParent(go.transform, false);
            var text = textGo.AddComponent<Text>();
            text.font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
            text.fontSize = 13;
            text.color = Color.white;
            SetStretch(textGo.GetComponent<RectTransform>());
            input.textComponent = text;
            input.text = placeholder;
            return input;
        }

        private static Button CreateButton(Transform parent, string label)
        {
            GameObject go = new GameObject("Button");
            go.transform.SetParent(parent, false);
            go.AddComponent<Image>().color = new Color(1f, 1f, 1f, 0.15f);
            go.AddComponent<LayoutElement>().preferredHeight = 26f;
            var button = go.AddComponent<Button>();

            GameObject textGo = new GameObject("Text");
            textGo.transform.SetParent(go.transform, false);
            var text = textGo.AddComponent<Text>();
            text.font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
            text.fontSize = 13;
            text.alignment = TextAnchor.MiddleCenter;
            text.color = Color.white;
            text.text = label;
            SetStretch(textGo.GetComponent<RectTransform>());
            return button;
        }

        private static Text CreateLabel(Transform parent, string label)
        {
            GameObject go = new GameObject("Status");
            go.transform.SetParent(parent, false);
            go.AddComponent<LayoutElement>().preferredHeight = 18f;
            var text = go.AddComponent<Text>();
            text.font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
            text.fontSize = 12;
            text.color = new Color(1f, 1f, 1f, 0.8f);
            text.text = label;
            return text;
        }

        private static void SetStretch(RectTransform rect)
        {
            rect.anchorMin = Vector2.zero;
            rect.anchorMax = Vector2.one;
            rect.offsetMin = new Vector2(6f, 2f);
            rect.offsetMax = new Vector2(-6f, -2f);
        }
    }
}
