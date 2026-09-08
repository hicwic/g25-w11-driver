// SPDX-License-Identifier: GPL-2.0-only
using System.Diagnostics;
using System.Text.Json;
using Microsoft.Win32;

namespace G25VirtualG29;

/// <summary>
/// Drives HidHide (nefarius/HidHide, MIT) through its CLI so local DirectInput
/// games see only the virtual G29, not the physical G25.
///
/// GeForce NOW is unaffected either way - it already ignores the G25 (046D:C299
/// is not in its supported-wheel list). This is purely for local games that would
/// otherwise enumerate two wheels.
///
/// A cloak session hides the G25's HID nodes and whitelists this worker process
/// so the bridge itself keeps reading the wheel. <see cref="Session.Dispose"/>
/// reverts precisely (only the entries we added; the global cloak toggle is left
/// as we found it unless we were the one to switch it on).
/// </summary>
static class HidHide
{
    // Physical G25 in native mode. Compat mode (C294) is G HUB's problem, not ours.
    private const string G25NativeIdFragment = "VID_046D&PID_C299";

    private static readonly string StatePath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),
        "g25vg29", "hidhide-session.json");

    internal sealed record State(string[] HiddenPaths, string AppPath, bool WeEnabledCloak);

    /// <summary>Active cloak; dispose to revert.</summary>
    internal sealed class Session : IDisposable
    {
        private readonly string _cli;
        private State _state;
        private bool _reverted;

        internal Session(string cli, State state)
        {
            _cli = cli;
            _state = state;
        }

        public IReadOnlyList<string> HiddenPaths => _state.HiddenPaths;

        public void Dispose()
        {
            if (_reverted) return;
            _reverted = true;
            Revert(_cli, _state);
            TryDeleteState();
        }
    }

    /// <summary>
    /// Hide the G25 from everything except this process. Returns null (with a
    /// printed reason) when HidHide is not installed or there is no G25 to hide -
    /// the bridge then runs without cloaking.
    /// </summary>
    public static Session? Cloak(string appPath)
    {
        var cli = FindCli();
        if (cli == null)
        {
            Console.WriteLine("HidHide: not installed; the physical G25 stays visible to local games (GeForce NOW is unaffected).");
            return null;
        }

        // Undo anything a previous worker left behind after a hard kill.
        RevertLeftoverState(cli);

        var paths = GamingDevicePaths(cli, G25NativeIdFragment);
        if (paths.Count == 0)
        {
            Console.WriteLine("HidHide: no physical G25 (046D:C299) reported by the driver; nothing to hide.");
            return null;
        }

        var wasCloaked = CloakActive(cli);

        RunCli(cli, "--app-reg", appPath);
        foreach (var p in paths)
            RunCli(cli, "--dev-hide", p);
        if (!wasCloaked)
            RunCli(cli, "--cloak-on");

        var state = new State([.. paths], appPath, WeEnabledCloak: !wasCloaked);
        WriteState(state);
        Console.WriteLine($"HidHide: G25 hidden from local games ({paths.Count} node(s)); this bridge is whitelisted.");

        // HidHide is a HIDClass upper filter. If it was installed without a
        // reboot, it is registered for the class but not yet in the G25's live
        // device stack, so the cloak has no effect. Restart the device nodes
        // once to pull the filter in. Whitelisted, so we can still read the
        // wheel afterwards (G25Source.Open retries for 15s).
        EnsureFilterInStack(paths);

        return new Session(cli, state);
    }

    /// <summary>
    /// Best-effort revert of a cloak a previous run did not clean up (hard kill,
    /// crash). Safe to call when there is nothing to revert. Used by the
    /// <c>cleanup</c> command and at the start of every cloak.
    /// </summary>
    public static void RevertLeftoverState()
    {
        var cli = FindCli();
        if (cli != null) RevertLeftoverState(cli);
    }

    private static void RevertLeftoverState(string cli)
    {
        State? state = ReadState();
        if (state == null) return;
        Console.WriteLine("HidHide: reverting a cloak left by an earlier run.");
        Revert(cli, state);
        TryDeleteState();
    }

    // Instance IDs look like "HID\VID_..." or "USB\VID_..."; the "\\?\..."
    // interface symbolic link cannot be restarted.
    private static bool IsInstanceId(string p) =>
        !p.StartsWith(@"\\", StringComparison.Ordinal) && p.Contains('\\');

    private static void EnsureFilterInStack(IEnumerable<string> paths)
    {
        var instanceIds = paths.Where(IsInstanceId)
            .Distinct(StringComparer.OrdinalIgnoreCase)
            // Restart the parent USB node first; it rebuilds the HID child.
            .OrderBy(p => p.StartsWith(@"USB\", StringComparison.OrdinalIgnoreCase) ? 0 : 1)
            .ToArray();
        if (instanceIds.Length == 0) return;

        // If any node already has the filter, assume the class filter is live.
        foreach (var id in instanceIds)
        {
            var (code, stdout, _) = RunProcess("pnputil", "/enum-devices", "/instanceid", id, "/stack");
            if (code == 0 && stdout.Contains("hidhide", StringComparison.OrdinalIgnoreCase))
                return;
        }

        Console.WriteLine("HidHide: restarting the G25 so the filter attaches (one-time after a no-reboot install)...");
        var restarted = false;
        foreach (var id in instanceIds)
        {
            var (code, _, _) = RunProcess("pnputil", "/restart-device", id);
            restarted |= code == 0;
        }
        if (restarted)
            Thread.Sleep(2000);   // let the stack rebuild before G25Source.Open
        else
            Console.WriteLine("HidHide: could not restart the G25. If local games still see two wheels, reboot once - the filter then sticks.");
    }

    private static void Revert(string cli, State state)
    {
        foreach (var p in state.HiddenPaths)
            RunCli(cli, "--dev-unhide", p);
        if (!string.IsNullOrEmpty(state.AppPath))
            RunCli(cli, "--app-unreg", state.AppPath);
        if (state.WeEnabledCloak)
            RunCli(cli, "--cloak-off");
    }

    // --- HidHide CLI plumbing --------------------------------------------------

    private static string? FindCli()
    {
        foreach (var dir in InstallDirs())
        {
            foreach (var rel in new[] { "HidHideCLI.exe", @"x64\HidHideCLI.exe" })
            {
                var full = Path.Combine(dir, rel);
                if (File.Exists(full)) return full;
            }
        }
        return null;
    }

    private static IEnumerable<string> InstallDirs()
    {
        if (OperatingSystem.IsWindows())
        {
            foreach (var view in new[] { RegistryView.Registry64, RegistryView.Registry32 })
            {
                string? path = null;
                try
                {
                    using var hklm = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, view);
                    using var key = hklm.OpenSubKey(@"SOFTWARE\Nefarius Software Solutions e.U.\HidHide");
                    path = key?.GetValue("Path") as string;
                }
                catch { /* ignore */ }
                if (!string.IsNullOrEmpty(path)) yield return path;
            }
        }

        var pf = Environment.GetEnvironmentVariable("ProgramW6432")
                 ?? Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles);
        if (!string.IsNullOrEmpty(pf))
            yield return Path.Combine(pf, "Nefarius Software Solutions", "HidHide");
    }

    private static bool CloakActive(string cli)
    {
        var (_, stdout, _) = RunCli(cli, "--cloak-state");
        // "cloak-state" prints something like "enabled" / "disabled".
        return stdout.Contains("enabled", StringComparison.OrdinalIgnoreCase)
               && !stdout.Contains("disabled", StringComparison.OrdinalIgnoreCase);
    }

    private static IReadOnlyList<string> GamingDevicePaths(string cli, string idFragment)
    {
        var (code, stdout, _) = RunCli(cli, "--dev-gaming");
        if (code != 0 || string.IsNullOrWhiteSpace(stdout)) return Array.Empty<string>();

        var found = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
        try
        {
            using var doc = JsonDocument.Parse(stdout);
            // The schema has shifted across HidHide versions. Rather than bind to
            // it, collect every string in the document that looks like a device
            // instance path for our VID/PID (covers deviceInstancePath and
            // baseContainerDeviceInstancePath, whatever they are nested under).
            CollectMatchingStrings(doc.RootElement, idFragment, found);
        }
        catch (JsonException)
        {
            // ignore - treated as "nothing to hide"
        }
        return [.. found];
    }

    private static void CollectMatchingStrings(JsonElement el, string idFragment, SortedSet<string> into)
    {
        switch (el.ValueKind)
        {
            case JsonValueKind.Object:
                foreach (var prop in el.EnumerateObject())
                    CollectMatchingStrings(prop.Value, idFragment, into);
                break;
            case JsonValueKind.Array:
                foreach (var item in el.EnumerateArray())
                    CollectMatchingStrings(item, idFragment, into);
                break;
            case JsonValueKind.String:
                var s = el.GetString();
                if (!string.IsNullOrEmpty(s)
                    && s.Contains('\\')
                    && s.Contains(idFragment, StringComparison.OrdinalIgnoreCase))
                    into.Add(s);
                break;
        }
    }

    private static (int code, string stdout, string stderr) RunCli(string cli, params string[] args)
    {
        var r = RunProcess(cli, args);
        if (r.code != 0 && args.Length > 0 && args[0] != "--cloak-state")
            Console.Error.WriteLine($"HidHide: `{args[0]}` exited {r.code}. {r.stderr.Trim()}");
        return r;
    }

    private static (int code, string stdout, string stderr) RunProcess(string exe, params string[] args)
    {
        var psi = new ProcessStartInfo(exe)
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
        };
        foreach (var a in args) psi.ArgumentList.Add(a);

        try
        {
            using var p = Process.Start(psi);
            if (p == null) return (-1, "", $"could not start {exe}");
            var so = p.StandardOutput.ReadToEnd();
            var se = p.StandardError.ReadToEnd();
            if (!p.WaitForExit(30_000))
            {
                try { p.Kill(); } catch { }
                return (-1, so, $"{exe} timed out");
            }
            return (p.ExitCode, so, se);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"HidHide: could not run {exe} {args.FirstOrDefault()}: {ex.Message}");
            return (-1, "", ex.Message);
        }
    }

    // --- session state file (survives a hard kill) ---------------------------

    private static void WriteState(State state)
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(StatePath)!);
            File.WriteAllText(StatePath, JsonSerializer.Serialize(state));
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"HidHide: could not record session state: {ex.Message}");
        }
    }

    private static State? ReadState()
    {
        try
        {
            return File.Exists(StatePath)
                ? JsonSerializer.Deserialize<State>(File.ReadAllText(StatePath))
                : null;
        }
        catch { return null; }
    }

    private static void TryDeleteState()
    {
        try { if (File.Exists(StatePath)) File.Delete(StatePath); }
        catch { /* ignore */ }
    }
}
