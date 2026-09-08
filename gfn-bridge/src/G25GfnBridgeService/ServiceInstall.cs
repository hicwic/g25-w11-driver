// SPDX-License-Identifier: GPL-2.0-only
using System.Diagnostics;

namespace G25GfnBridgeService;

/// <summary>
/// Registers / removes the service via sc.exe. Runs elevated (the bridge
/// installer, or an admin shell).
/// </summary>
static class ServiceInstall
{
    public const string ServiceName = "g25gfnbridge";
    public const string DisplayName = "G25 GeForce NOW bridge";

    // Grant the interactive user group (IU) START, STOP and QUERY_STATUS on top
    // of the default descriptor so the unprivileged tray can drive the service.
    //  RP = SERVICE_START, WP = SERVICE_STOP, LC = SERVICE_QUERY_STATUS,
    //  CC = SERVICE_QUERY_CONFIG, RC = READ_CONTROL.
    private const string Sddl =
        "D:(A;;CCLCSWRPWPDTLOCRRC;;;SY)(A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;BA)" +
        "(A;;CCLCSWLOCRRC;;;IU)(A;;CCLCSWLOCRRC;;;SU)(A;;RPWPLCRC;;;IU)";

    public static int Install()
    {
        var exe = Environment.ProcessPath ?? throw new InvalidOperationException("ProcessPath");
        int rc;
        rc = Sc($"create {ServiceName} binPath= \"\\\"{exe}\\\" run\" start= demand DisplayName= \"{DisplayName}\"");
        if (rc != 0 && rc != 1073) return Fail("create", rc);       // 1073 = already exists
        Sc($"description {ServiceName} \"Supervises the GeForce NOW wheel bridge. On-demand; started by G25 Control.\"");
        Sc($"failure {ServiceName} reset= 300 actions= restart/5000/restart/15000/\"\"/0");
        rc = Sc($"sdset {ServiceName} \"{Sddl}\"");
        if (rc != 0) return Fail("sdset", rc);
        Console.WriteLine($"{ServiceName} installed (start on demand, tray-controllable).");
        return 0;
    }

    public static int Uninstall()
    {
        Sc($"stop {ServiceName}", quiet: true);                      // ok if already stopped / absent
        var rc = Sc($"delete {ServiceName}");
        if (rc != 0 && rc != 1060) return Fail("delete", rc);        // 1060 = not installed
        Console.WriteLine($"{ServiceName} removed.");
        return 0;
    }

    private static int Sc(string arguments, bool quiet = false)
    {
        var psi = new ProcessStartInfo("sc.exe", arguments)
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
        };
        using var p = Process.Start(psi)!;
        var output = p.StandardOutput.ReadToEnd() + p.StandardError.ReadToEnd();
        p.WaitForExit();
        if (p.ExitCode != 0 && !quiet)
            Console.Error.WriteLine($"sc {arguments.Split(' ')[0]} -> {p.ExitCode}: {output.Trim()}");
        return p.ExitCode;
    }

    private static int Fail(string step, int rc)
    {
        Console.Error.WriteLine($"service {step} failed ({rc}).");
        return 1;
    }
}
