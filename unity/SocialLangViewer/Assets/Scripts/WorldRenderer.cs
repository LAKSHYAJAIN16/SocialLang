using System.Collections.Generic;
using UnityEngine;

namespace SocialLangViewer
{
    /// <summary>
    /// Draws every procedurally-placed Location (from the sim's world {} block) as a
    /// point marker, refreshed whenever a new "world" message arrives. Same
    /// particle-based rendering technique as AgentParticleRenderer, just redrawn on
    /// change rather than every frame since locations never move once placed.
    /// </summary>
    [RequireComponent(typeof(ParticleSystem))]
    public class WorldRenderer : MonoBehaviour
    {
        public SimulationHub hub;
        public float pointSize = 2.5f;
        public Color color = new Color(0.85f, 0.85f, 0.85f, 0.9f);

        private ParticleSystem _ps;
        private ParticleSystem.Particle[] _particles = new ParticleSystem.Particle[2048];

        private void Awake()
        {
            if (hub == null) hub = SimulationHub.Instance;

            _ps = GetComponent<ParticleSystem>();
            var main = _ps.main;
            main.loop = false;
            main.playOnAwake = false;
            main.maxParticles = 8000;
            main.simulationSpace = ParticleSystemSimulationSpace.World;
            main.startLifetime = 1f;

            var emission = _ps.emission;
            emission.enabled = false;
            var shape = _ps.shape;
            shape.enabled = false;

            _ps.Play();

            if (hub != null) hub.state.OnWorldChanged += Refresh;
            Refresh();
        }

        private void OnDestroy()
        {
            if (hub != null && hub.state != null) hub.state.OnWorldChanged -= Refresh;
        }

        private void Refresh()
        {
            if (hub == null) return;
            Dictionary<string, LocationState> locations = hub.state.Locations;
            if (locations.Count == 0)
            {
                _ps.SetParticles(_particles, 0);
                return;
            }
            if (locations.Count > _particles.Length)
            {
                _particles = new ParticleSystem.Particle[Mathf.NextPowerOfTwo(locations.Count)];
            }

            int i = 0;
            foreach (KeyValuePair<string, LocationState> kv in locations)
            {
                LocationState loc = kv.Value;
                _particles[i].position = new Vector3(loc.x, loc.y, 0.1f);
                _particles[i].startColor = color;
                _particles[i].startSize = pointSize;
                _particles[i].remainingLifetime = 1f;
                _particles[i].startLifetime = 1f;
                i++;
            }
            _ps.SetParticles(_particles, i);
        }
    }
}
