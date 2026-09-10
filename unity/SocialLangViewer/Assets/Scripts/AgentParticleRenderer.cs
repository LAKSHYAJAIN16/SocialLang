using System.Collections.Generic;
using UnityEngine;

namespace SocialLangViewer
{
    /// <summary>
    /// Draws every agent as one particle in a single ParticleSystem, driven by
    /// SetParticles() each frame -- the standard trick for cheaply rendering
    /// thousands of dynamic points (games/city.sl and games/outbreak.sl both run into
    /// the thousands/hundreds of agents), instead of one GameObject per agent.
    /// </summary>
    [RequireComponent(typeof(ParticleSystem))]
    public class AgentParticleRenderer : MonoBehaviour
    {
        public SimulationHub hub;
        public float pointSize = 1.2f;
        public float deadPointSize = 0.8f;

        private ParticleSystem _ps;
        private ParticleSystem.Particle[] _particles = new ParticleSystem.Particle[4096];
        private readonly Dictionary<string, Color> _teamColors = new Dictionary<string, Color>();

        private static readonly Color[] Palette =
        {
            new Color(0.30f, 0.55f, 0.95f),
            new Color(0.90f, 0.35f, 0.35f),
            new Color(0.95f, 0.75f, 0.25f),
            new Color(0.40f, 0.80f, 0.55f),
            new Color(0.65f, 0.45f, 0.85f),
            new Color(0.95f, 0.55f, 0.75f),
        };

        private void Awake()
        {
            if (hub == null) hub = SimulationHub.Instance;

            _ps = GetComponent<ParticleSystem>();
            var main = _ps.main;
            main.loop = false;
            main.playOnAwake = false;
            main.maxParticles = 20000;
            main.simulationSpace = ParticleSystemSimulationSpace.World;
            main.startLifetime = 1f;

            var emission = _ps.emission;
            emission.enabled = false;
            var shape = _ps.shape;
            shape.enabled = false;

            _ps.Play();
        }

        private Color ColorForTeam(string team)
        {
            if (string.IsNullOrEmpty(team)) return Color.white;
            if (!_teamColors.TryGetValue(team, out Color c))
            {
                c = Palette[_teamColors.Count % Palette.Length];
                _teamColors[team] = c;
            }
            return c;
        }

        private void LateUpdate()
        {
            if (hub == null || hub.state == null) return;
            Dictionary<string, AgentState> agentsDict = hub.state.Agents;
            if (agentsDict.Count == 0) return;

            if (agentsDict.Count > _particles.Length)
            {
                _particles = new ParticleSystem.Particle[Mathf.NextPowerOfTwo(agentsDict.Count)];
            }

            int i = 0;
            foreach (KeyValuePair<string, AgentState> kv in agentsDict)
            {
                AgentState a = kv.Value;
                if (!a.x.HasValue || !a.y.HasValue) continue; // never spawned/positioned

                _particles[i].position = new Vector3(a.x.Value, a.y.Value, 0f);
                _particles[i].startColor = a.alive ? ColorForTeam(a.team) : new Color(0.3f, 0.3f, 0.3f, 0.5f);
                _particles[i].startSize = a.alive ? pointSize : deadPointSize;
                _particles[i].remainingLifetime = 1f;
                _particles[i].startLifetime = 1f;
                i++;
            }
            _ps.SetParticles(_particles, i);
        }
    }
}
