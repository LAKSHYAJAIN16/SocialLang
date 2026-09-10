using UnityEngine;

namespace SocialLangViewer
{
    /// <summary>
    /// Holds the one SimulationState every other script reads from. Created by
    /// Bootstrap at startup -- nothing needs to be wired up by hand in the Editor.
    /// </summary>
    public class SimulationHub : MonoBehaviour
    {
        public static SimulationHub Instance { get; private set; }

        public readonly SimulationState state = new SimulationState();

        private void Awake()
        {
            if (Instance != null && Instance != this)
            {
                Destroy(gameObject);
                return;
            }
            Instance = this;
            DontDestroyOnLoad(gameObject);
        }
    }
}
