using UnityEngine;

namespace SocialLangViewer
{
    /// <summary>
    /// Right/middle-mouse drag to pan, scroll wheel to zoom. Left click is reserved
    /// for AgentInspectorUI's "select nearest agent" so the two don't fight.
    /// </summary>
    public class SimulationCameraController : MonoBehaviour
    {
        public float zoomSpeed = 10f;
        public float minSize = 3f;
        public float maxSize = 400f;

        private Camera _cam;
        private Vector3 _lastMouse;
        private bool _dragging;

        private void Awake() => _cam = GetComponent<Camera>();

        private void Update()
        {
            if (_cam == null) return;

            float scroll = Input.GetAxis("Mouse ScrollWheel");
            if (Mathf.Abs(scroll) > 0.0001f)
            {
                _cam.orthographicSize = Mathf.Clamp(_cam.orthographicSize - scroll * zoomSpeed, minSize, maxSize);
            }

            if (Input.GetMouseButtonDown(1) || Input.GetMouseButtonDown(2))
            {
                _dragging = true;
                _lastMouse = Input.mousePosition;
            }
            if (Input.GetMouseButtonUp(1) || Input.GetMouseButtonUp(2))
            {
                _dragging = false;
            }
            if (_dragging && (Input.GetMouseButton(1) || Input.GetMouseButton(2)))
            {
                Vector3 delta = _cam.ScreenToViewportPoint(_lastMouse - Input.mousePosition);
                var move = new Vector3(
                    delta.x * _cam.orthographicSize * 2f * _cam.aspect,
                    delta.y * _cam.orthographicSize * 2f,
                    0f);
                transform.position += move;
                _lastMouse = Input.mousePosition;
            }
        }
    }
}
