using System;
using System.Net.WebSockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Newtonsoft.Json;
using UnityEngine;

namespace SocialLangViewer
{
    /// <summary>
    /// Connects to `sociallang run --live` (sociallang/engine/live.py's WebSocketSink)
    /// and applies every message it receives straight into SimulationHub.state.
    ///
    /// Uses .NET's built-in System.Net.WebSockets.ClientWebSocket rather than a third
    /// -party package -- no extra dependency to fetch, and it's fine for a desktop/
    /// Editor viewer (not targeting WebGL, where raw sockets aren't available).
    ///
    /// Unity installs a SynchronizationContext that resumes `await` continuations on
    /// the main thread by default, so ReceiveLoop can touch SimulationState/Unity APIs
    /// directly after each `await` without any manual thread marshalling.
    /// </summary>
    public class LiveBridgeClient : MonoBehaviour
    {
        [Tooltip("Matches `sociallang run --live --live-host --live-port`.")]
        public string url = "ws://localhost:8765";

        public SimulationHub hub;

        private ClientWebSocket _socket;
        private CancellationTokenSource _cts;

        public bool IsConnected => _socket != null && _socket.State == WebSocketState.Open;
        public string LastError { get; private set; }

        public async void Connect()
        {
            Disconnect();
            _cts = new CancellationTokenSource();
            _socket = new ClientWebSocket();
            LastError = null;

            try
            {
                await _socket.ConnectAsync(new Uri(url), _cts.Token);
                Debug.Log($"[LiveBridgeClient] connected to {url}");
                if (hub != null) hub.state.Reset();
                _ = ReceiveLoop(_cts.Token);
            }
            catch (Exception e)
            {
                LastError = e.Message;
                Debug.LogWarning($"[LiveBridgeClient] connect to {url} failed: {e.Message}");
            }
        }

        public void Disconnect()
        {
            try { _cts?.Cancel(); } catch (Exception) { /* already disposed */ }
            _socket?.Dispose();
            _socket = null;
        }

        private async Task ReceiveLoop(CancellationToken token)
        {
            var buffer = new byte[8192];
            var sb = new StringBuilder();
            try
            {
                while (_socket != null && _socket.State == WebSocketState.Open && !token.IsCancellationRequested)
                {
                    sb.Clear();
                    WebSocketReceiveResult result;
                    do
                    {
                        result = await _socket.ReceiveAsync(new ArraySegment<byte>(buffer), token);
                        if (result.MessageType == WebSocketMessageType.Close)
                        {
                            Debug.Log("[LiveBridgeClient] server closed the connection");
                            return;
                        }
                        sb.Append(Encoding.UTF8.GetString(buffer, 0, result.Count));
                    } while (!result.EndOfMessage);

                    var json = sb.ToString();
                    try
                    {
                        var msg = JsonConvert.DeserializeObject<EventMsg>(json);
                        if (hub != null) hub.state.Apply(msg);
                    }
                    catch (Exception parseEx)
                    {
                        Debug.LogWarning($"[LiveBridgeClient] failed to parse message: {parseEx.Message}\n{json}");
                    }
                }
            }
            catch (OperationCanceledException)
            {
                // expected on Disconnect()
            }
            catch (Exception e)
            {
                LastError = e.Message;
                Debug.LogWarning($"[LiveBridgeClient] receive loop ended: {e.Message}");
            }
        }

        private void OnDestroy() => Disconnect();
    }
}
