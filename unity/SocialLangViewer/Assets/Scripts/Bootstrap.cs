using UnityEngine;
using UnityEngine.EventSystems;

namespace SocialLangViewer
{
    /// <summary>
    /// Builds the entire viewer at runtime -- camera, renderers, network/replay
    /// readers, UI -- so no scene needs to be hand-authored or opened by name. Press
    /// Play in any scene (even a brand-new empty one) and this assembles everything,
    /// then tries to connect to the default live bridge URL.
    /// </summary>
    public static class Bootstrap
    {
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
        private static void Init()
        {
            if (SimulationHub.Instance != null) return; // don't double-build on scene reload

            if (EventSystem.current == null)
            {
                GameObject esGo = new GameObject("EventSystem");
                esGo.AddComponent<EventSystem>();
                esGo.AddComponent<StandaloneInputModule>();
            }

            GameObject hubGo = new GameObject("SocialLangViewer");
            var hub = hubGo.AddComponent<SimulationHub>();

            // Configure whatever camera is already tagged MainCamera (Unity's own
            // default new-scene template ships with one) rather than only building
            // our own when none exists -- otherwise the common case (pressing Play
            // in the default sample scene) silently skips orthographic/pan-zoom
            // setup entirely, breaking click-to-select under a leftover perspective
            // camera.
            Camera cam = Camera.main;
            if (cam == null)
            {
                GameObject camGo = new GameObject("Main Camera");
                camGo.tag = "MainCamera";
                cam = camGo.AddComponent<Camera>();
            }
            cam.orthographic = true;
            cam.orthographicSize = 50f;
            cam.backgroundColor = new Color(0.08f, 0.08f, 0.1f);
            cam.clearFlags = CameraClearFlags.SolidColor;
            Vector3 pos = cam.transform.position;
            cam.transform.position = new Vector3(pos.x, pos.y, -10f);
            if (cam.GetComponent<SimulationCameraController>() == null)
            {
                cam.gameObject.AddComponent<SimulationCameraController>();
            }

            GameObject worldGo = new GameObject("WorldRenderer");
            worldGo.transform.SetParent(hubGo.transform);
            var worldRenderer = worldGo.AddComponent<WorldRenderer>();
            worldRenderer.hub = hub;

            GameObject agentsGo = new GameObject("AgentRenderer");
            agentsGo.transform.SetParent(hubGo.transform);
            var agentRenderer = agentsGo.AddComponent<AgentParticleRenderer>();
            agentRenderer.hub = hub;

            GameObject netGo = new GameObject("LiveBridgeClient");
            netGo.transform.SetParent(hubGo.transform);
            var client = netGo.AddComponent<LiveBridgeClient>();
            client.hub = hub;

            GameObject replayGo = new GameObject("ReplayFileReader");
            replayGo.transform.SetParent(hubGo.transform);
            var replay = replayGo.AddComponent<ReplayFileReader>();
            replay.hub = hub;

            AgentInspectorUI.Create(hub);
            PlaybackControls.Create(client, replay);

            Debug.Log($"[SocialLangViewer] bootstrapped. Connecting to {client.url} -- " +
                      "use the panel in the top-left to change the URL or load a replay file instead.");
            client.Connect();
        }
    }
}
