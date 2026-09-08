// SPDX-License-Identifier: GPL-2.0-only
using System.Text.Json;
using System.Text.Json.Serialization;

namespace G25GfnBridgeService;

/// <summary>Shared, thread-safe snapshot of what the bridge worker is doing.</summary>
sealed class BridgeStatus
{
    private readonly object _gate = new();
    private Snapshot _snap = new();

    public event Action<string>? Changed;

    public string Json
    {
        get { lock (_gate) return _snap.ToJson(); }
    }

    public void Update(Action<Snapshot> mutate)
    {
        string json;
        lock (_gate)
        {
            mutate(_snap);
            _snap.UpdatedUtc = DateTimeOffset.UtcNow;
            json = _snap.ToJson();
        }
        Changed?.Invoke(json);
    }

    public sealed class Snapshot
    {
        [JsonPropertyName("state")] public string State { get; set; } = "stopped";
        [JsonPropertyName("virtualDevice")] public string? VirtualDevice { get; set; }
        [JsonPropertyName("ffbActive")] public bool FfbActive { get; set; }
        [JsonPropertyName("wheel")] public double Wheel { get; set; }
        [JsonPropertyName("restarts")] public int Restarts { get; set; }
        [JsonPropertyName("lastError")] public string? LastError { get; set; }
        [JsonPropertyName("updatedUtc")] public DateTimeOffset UpdatedUtc { get; set; } = DateTimeOffset.UtcNow;

        private static readonly JsonSerializerOptions Opts = new() { DefaultIgnoreCondition = JsonIgnoreCondition.Never };
        public string ToJson() => JsonSerializer.Serialize(this, Opts);
    }
}
