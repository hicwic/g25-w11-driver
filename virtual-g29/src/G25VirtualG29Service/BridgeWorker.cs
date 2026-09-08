// SPDX-License-Identifier: GPL-2.0-only
using System.Diagnostics;
using System.Globalization;
using System.Text.Json;
using System.Text.RegularExpressions;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;

namespace G25VirtualG29Service;

/// <summary>
/// Supervises the Virtual G29 bridge worker process: starts it, restarts it with
/// backoff if it crashes, stops it cleanly when the service stops, and turns its
/// stdout into a <see cref="BridgeStatus"/>.
/// </summary>
sealed partial class BridgeWorker(
    BridgeStatus status, ILogger<BridgeWorker> log, IHostApplicationLifetime lifetime) : BackgroundService
{
    private static readonly TimeSpan[] Backoff =
        [TimeSpan.FromSeconds(3), TimeSpan.FromSeconds(10), TimeSpan.FromSeconds(30)];

    // Exit code the worker uses for "the physical G25 was working, then went
    // away for good". Restarting would just loop, so we stop the service.
    private const int ExitWheelGone = 3;
    private const int MaxRestartsBeforeGivingUp = 4;

    private volatile bool _wheelGone;

    protected override async Task ExecuteAsync(CancellationToken stopping)
    {
        var exe = Path.Combine(AppContext.BaseDirectory, "g25-virtual-g29.exe");
        if (!File.Exists(exe))
        {
            status.Update(s => { s.State = "faulted"; s.LastError = "bridge worker exe not found"; });
            log.LogError("bridge worker not found at {Exe}", exe);
            return;
        }

        var stopEventName = $@"Local\g25vg29-stop-{Environment.ProcessId}";
        using var stopEvent = new EventWaitHandle(false, EventResetMode.ManualReset, stopEventName);

        var failures = 0;
        while (!stopping.IsCancellationRequested)
        {
            stopEvent.Reset();
            _wheelGone = false;
            status.Update(s => { s.State = failures == 0 ? "starting" : "restarting"; s.LastError = null; });

            // Re-read on every (re)start so a config / tray change is picked up.
            var cfg = Config.Load();
            var wheelRange = TrayRotationDegrees() ?? cfg.WheelRangeDegrees;
            var args = string.Join(' ', BuildArgs(cfg, wheelRange, stopEventName));
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
                RevertHidHide(exe);
                status.Update(s => { s.State = "stopped"; s.VirtualDevice = null; s.FfbActive = false; });
                return;
            }

            var code = proc.ExitCode;
            failures++;

            if (code == ExitWheelGone || _wheelGone || failures > MaxRestartsBeforeGivingUp)
            {
                var why = code == ExitWheelGone || _wheelGone
                    ? "physical G25 disconnected"
                    : $"bridge failed to start {failures} times";
                log.LogWarning("stopping the service: {Why}", why);
                RevertHidHide(exe);
                status.Update(s =>
                {
                    s.State = "stopped";
                    s.LastError = why;
                    s.VirtualDevice = null;
                    s.FfbActive = false;
                });
                lifetime.StopApplication();   // SCM -> Stopped; tray shows G29 mode off
                return;
            }

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

    private static IEnumerable<string> BuildArgs(Config cfg, int wheelRange, string stopEventName)
    {
        yield return "bridge";
        yield return "--profile";
        yield return cfg.Profile;
        yield return "--wheel-range";
        yield return wheelRange.ToString(CultureInfo.InvariantCulture);
        yield return "--stop-event";
        yield return stopEventName;
        if (cfg.InstallDriver) yield return "--install-driver";
        if (cfg.InvertBrake) yield return "--invert-brake";
        if (cfg.InvertClutch) yield return "--invert-clutch";
        if (cfg.InvertAccelerator) yield return "--invert-accelerator";
        if (cfg.FfbTranslate) yield return "--ffb-translate";
        if (!cfg.HideLocalG25) yield return "--no-hide-g25";
        if (!cfg.ForwardButtons) yield return "--no-buttons";
        if (!cfg.ForwardHat) yield return "--no-hat";
    }

    // The tray writes the user's chosen wheel range to HKCU\Software\g25-driver
    // \Rotation. The service runs as SYSTEM, so read it from HKEY_USERS. Returns
    // null when no interactive user has set it (fall back to config / 900).
    private int? TrayRotationDegrees()
    {
        if (!OperatingSystem.IsWindows()) return null;
        try
        {
            using var users = Microsoft.Win32.RegistryKey.OpenBaseKey(
                Microsoft.Win32.RegistryHive.Users, Microsoft.Win32.RegistryView.Default);
            foreach (var sid in users.GetSubKeyNames())
            {
                if (sid.StartsWith(".DEFAULT", StringComparison.OrdinalIgnoreCase) ||
                    sid.EndsWith("_Classes", StringComparison.OrdinalIgnoreCase) ||
                    sid is "S-1-5-18" or "S-1-5-19" or "S-1-5-20")
                    continue;
                try
                {
                    using var key = users.OpenSubKey($@"{sid}\Software\g25-driver");
                    if (key?.GetValue("Rotation") is int r && r is 180 or 360 or 540 or 900)
                    {
                        log.LogInformation("using tray wheel range {Deg} deg", r);
                        return r;
                    }
                }
                catch { /* not this hive */ }
            }
        }
        catch (Exception ex) { log.LogDebug(ex, "could not read tray rotation"); }
        return null;
    }

    // If the worker was killed rather than exiting cleanly, its HidHide cloak
    // may still be in place. A no-op when the worker already reverted.
    private void RevertHidHide(string exe)
    {
        try
        {
            using var p = Process.Start(new ProcessStartInfo(exe, "hidhide-revert")
            {
                UseShellExecute = false,
                CreateNoWindow = true,
                WorkingDirectory = AppContext.BaseDirectory,
            });
            p?.WaitForExit(20_000);
        }
        catch (Exception ex)
        {
            log.LogWarning(ex, "hidhide-revert failed");
        }
    }

    private void OnLine(string? line, bool isError = false)
    {
        if (string.IsNullOrWhiteSpace(line)) return;
        if (isError) log.LogWarning("bridge: {Line}", line);
        else log.LogDebug("bridge: {Line}", line);

        if (line.StartsWith("Bridge is running", StringComparison.Ordinal))
            status.Update(s => { s.State = "running"; s.LastError = null; });
        else if (line.StartsWith("WHEEL: lost", StringComparison.Ordinal))
            status.Update(s => { s.State = "wheel-lost"; s.LastError = "physical G25 not responding"; });
        else if (line.StartsWith("WHEEL: reconnected", StringComparison.Ordinal))
            status.Update(s => { s.State = "running"; s.LastError = null; });
        else if (line.StartsWith("WHEEL: gone", StringComparison.Ordinal))
            _wheelGone = true;
        else if (TelemetryLine().Match(line) is { Success: true } m)
            status.Update(s =>
            {
                s.Wheel = double.Parse(m.Groups[1].Value.Replace(',', '.'), CultureInfo.InvariantCulture);
                s.FfbActive = int.Parse(m.Groups[2].Value, CultureInfo.InvariantCulture) > 0;
            });
        else if (line.StartsWith("Creating virtual wheel:", StringComparison.Ordinal))
            status.Update(s => s.VirtualDevice = line["Creating virtual wheel:".Length..].Trim());
        else if (isError && (line.Contains("error", StringComparison.OrdinalIgnoreCase) || line.Contains("failed", StringComparison.OrdinalIgnoreCase)))
            status.Update(s => s.LastError = line);
    }

    [GeneratedRegex(@"wheel=([0-9.,]+).*ffbHz=(\d+)")]
    private static partial Regex TelemetryLine();

    private sealed record Config
    {
        public string Profile { get; init; } = "logitech-g29-usbip";
        public int WheelRangeDegrees { get; init; } = 900;
        public bool InstallDriver { get; init; }
        public bool InvertBrake { get; init; }
        public bool InvertClutch { get; init; }
        public bool InvertAccelerator { get; init; }
        public bool FfbTranslate { get; init; }
        public bool HideLocalG25 { get; init; } = true;
        public bool ForwardButtons { get; init; } = true;
        public bool ForwardHat { get; init; } = true;

        public static Config Load()
        {
            var path = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),
                "g25vg29", "config.json");
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
