// SPDX-License-Identifier: GPL-2.0-only
using System.Diagnostics;
using System.Globalization;
using System.Text.Json;
using System.Text.RegularExpressions;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;

namespace G25GfnBridgeService;

/// <summary>
/// Supervises the GeForce NOW bridge worker process: starts it, restarts it with
/// backoff if it crashes, stops it cleanly when the service stops, and turns its
/// stdout into a <see cref="BridgeStatus"/>.
/// </summary>
sealed partial class BridgeWorker(BridgeStatus status, ILogger<BridgeWorker> log) : BackgroundService
{
    private static readonly TimeSpan[] Backoff =
        [TimeSpan.FromSeconds(3), TimeSpan.FromSeconds(10), TimeSpan.FromSeconds(30)];

    protected override async Task ExecuteAsync(CancellationToken stopping)
    {
        var exe = Path.Combine(AppContext.BaseDirectory, "g25-gfn-wheel-bridge.exe");
        if (!File.Exists(exe))
        {
            status.Update(s => { s.State = "faulted"; s.LastError = "bridge worker exe not found"; });
            log.LogError("bridge worker not found at {Exe}", exe);
            return;
        }

        var cfg = Config.Load();
        var stopEventName = $@"Local\g25gfnbridge-stop-{Environment.ProcessId}";
        using var stopEvent = new EventWaitHandle(false, EventResetMode.ManualReset, stopEventName);

        var failures = 0;
        while (!stopping.IsCancellationRequested)
        {
            stopEvent.Reset();
            status.Update(s => { s.State = failures == 0 ? "starting" : "restarting"; s.LastError = null; });

            var args = string.Join(' ', BuildArgs(cfg, stopEventName));
            log.LogInformation("starting bridge: {Args}", args);

            using var proc = new Process
            {
                StartInfo = new ProcessStartInfo(exe, args)
                {
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    WorkingDirectory = AppContext.BaseDirectory,
                },
                EnableRaisingEvents = true,
            };
            proc.OutputDataReceived += (_, e) => OnLine(e.Data);
            proc.ErrorDataReceived += (_, e) => OnLine(e.Data, isError: true);

            try { proc.Start(); }
            catch (Exception ex)
            {
                status.Update(s => { s.State = "faulted"; s.LastError = ex.Message; });
                log.LogError(ex, "could not start bridge");
                return;
            }
            proc.BeginOutputReadLine();
            proc.BeginErrorReadLine();

            var exited = new TaskCompletionSource();
            proc.Exited += (_, _) => exited.TrySetResult();

            try { await exited.Task.WaitAsync(stopping); }
            catch (OperationCanceledException) { /* service is stopping */ }

            if (stopping.IsCancellationRequested)
            {
                status.Update(s => s.State = "stopping");
                try { stopEvent.Set(); } catch { }
                if (!proc.WaitForExit(10_000))
                {
                    log.LogWarning("bridge did not stop in 10s; killing");
                    try { proc.Kill(entireProcessTree: true); } catch { }
                }
                status.Update(s => { s.State = "stopped"; s.VirtualDevice = null; s.FfbActive = false; });
                return;
            }

            var code = proc.ExitCode;
            failures++;
            status.Update(s =>
            {
                s.State = "restarting";
                s.Restarts++;
                s.LastError ??= $"bridge exited with code {code}";
                s.VirtualDevice = null;
                s.FfbActive = false;
            });
            log.LogWarning("bridge exited ({Code}); restart {N}", code, failures);

            var wait = Backoff[Math.Min(failures - 1, Backoff.Length - 1)];
            try { await Task.Delay(wait, stopping); } catch (OperationCanceledException) { }
        }
    }

    private static IEnumerable<string> BuildArgs(Config cfg, string stopEventName)
    {
        yield return "bridge";
        yield return "--profile";
        yield return cfg.Profile;
        yield return "--wheel-range";
        yield return cfg.WheelRangeDegrees.ToString(CultureInfo.InvariantCulture);
        yield return "--stop-event";
        yield return stopEventName;
        if (cfg.InstallDriver) yield return "--install-driver";
        if (cfg.InvertBrake) yield return "--invert-brake";
        if (cfg.InvertClutch) yield return "--invert-clutch";
        if (cfg.InvertAccelerator) yield return "--invert-accelerator";
        if (!cfg.ForwardButtons) yield return "--no-buttons";
        if (!cfg.ForwardHat) yield return "--no-hat";
    }

    private void OnLine(string? line, bool isError = false)
    {
        if (string.IsNullOrWhiteSpace(line)) return;
        if (isError) log.LogWarning("bridge: {Line}", line);
        else log.LogDebug("bridge: {Line}", line);

        if (line.StartsWith("Bridge is running", StringComparison.Ordinal))
            status.Update(s => s.State = "running");
        else if (WheelLine().Match(line) is { Success: true } m)
            status.Update(s => s.Wheel = double.Parse(m.Groups[1].Value.Replace(',', '.'), CultureInfo.InvariantCulture));
        else if (line.StartsWith("Creating virtual wheel:", StringComparison.Ordinal))
            status.Update(s => s.VirtualDevice = line["Creating virtual wheel:".Length..].Trim());
        else if (line.StartsWith("OUT source=", StringComparison.Ordinal))
            status.Update(s => s.FfbActive = true);
        else if (isError && (line.Contains("error", StringComparison.OrdinalIgnoreCase) || line.Contains("failed", StringComparison.OrdinalIgnoreCase)))
            status.Update(s => s.LastError = line);
    }

    [GeneratedRegex(@"wheel=([0-9.,]+)\s")]
    private static partial Regex WheelLine();

    private sealed record Config
    {
        public string Profile { get; init; } = "logitech-g29-usbip";
        public int WheelRangeDegrees { get; init; } = 900;
        public bool InstallDriver { get; init; }
        public bool InvertBrake { get; init; }
        public bool InvertClutch { get; init; }
        public bool InvertAccelerator { get; init; }
        public bool ForwardButtons { get; init; } = true;
        public bool ForwardHat { get; init; } = true;

        public static Config Load()
        {
            var path = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),
                "g25gfnbridge", "config.json");
            try
            {
                if (File.Exists(path))
                    return JsonSerializer.Deserialize<Config>(File.ReadAllText(path),
                        new JsonSerializerOptions { PropertyNameCaseInsensitive = true }) ?? new Config();
            }
            catch { /* fall back to defaults */ }
            return new Config();
        }
    }
}
