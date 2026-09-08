// SPDX-License-Identifier: GPL-2.0-only
using System.Diagnostics;

namespace G25VirtualG29;

sealed record PnpDeviceInfo(string Status, string Class, string FriendlyName, string InstanceId, IReadOnlyList<string> HardwareIds);

/// <summary>
/// Read-only PnP inventory used by <c>inspect</c>. Shells out to PowerShell
/// because the managed CM_* surface is not available on this target framework
/// without extra native interop.
/// </summary>
static class PnpSnapshot
{
    public static IReadOnlyList<PnpDeviceInfo> FindInterestingDevices()
    {
        if (!OperatingSystem.IsWindows()) return Array.Empty<PnpDeviceInfo>();
        var script = "Get-PnpDevice -PresentOnly | Where-Object { $_.FriendlyName -match 'G25|G29|HIDMaestro' -or $_.InstanceId -match 'VID_046D&PID_C299|VID_046D&PID_C24F|HIDMAESTRO' } | ForEach-Object { $hw=(Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName DEVPKEY_Device_HardwareIds -ErrorAction SilentlyContinue).Data -join ';'; [pscustomobject]@{Status=$_.Status;Class=$_.Class;FriendlyName=$_.FriendlyName;InstanceId=$_.InstanceId;HardwareIds=$hw} } | ConvertTo-Json -Compress";
        var psi = new ProcessStartInfo("powershell.exe", "-NoProfile -Command " + QuoteForPowerShell(script))
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
        };
        using var p = Process.Start(psi);
        if (p == null) return Array.Empty<PnpDeviceInfo>();
        var output = p.StandardOutput.ReadToEnd();
        p.WaitForExit(5000);
        if (string.IsNullOrWhiteSpace(output)) return Array.Empty<PnpDeviceInfo>();
        return SimplePnpJson.Parse(output);
    }

    private static string QuoteForPowerShell(string s) => "'" + s.Replace("'", "''") + "'";
}

static class SimplePnpJson
{
    public static IReadOnlyList<PnpDeviceInfo> Parse(string json)
    {
        try
        {
            using var doc = System.Text.Json.JsonDocument.Parse(json);
            var root = doc.RootElement;
            var items = root.ValueKind == System.Text.Json.JsonValueKind.Array ? root.EnumerateArray().ToArray() : new[] { root };
            return items.Select(e => new PnpDeviceInfo(
                Get(e, "Status"), Get(e, "Class"), Get(e, "FriendlyName"), Get(e, "InstanceId"),
                Get(e, "HardwareIds").Split(';', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))).ToArray();
        }
        catch
        {
            return Array.Empty<PnpDeviceInfo>();
        }
    }

    private static string Get(System.Text.Json.JsonElement e, string name) =>
        e.TryGetProperty(name, out var p) && p.ValueKind != System.Text.Json.JsonValueKind.Null ? p.ToString() : "";
}
