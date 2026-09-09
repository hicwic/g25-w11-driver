// SPDX-License-Identifier: GPL-2.0-only
using System.Diagnostics;
using System.Security.Principal;
using HIDMaestro;
using SharpDX.DirectInput;

namespace G25VirtualG29;

static class Program
{
    // Signalled by Ctrl+C, --stop-event, or --duration. PumpG25 loops on it.
    static readonly CancellationTokenSource Shutdown = new();

    public static int Main(string[] args)
    {
        var command = args.Length > 0 ? args[0].ToLowerInvariant() : "help";
        var rest = args.Skip(1).ToArray();

        try
        {
            return command switch
            {
                "inspect" => Inspect(),
                "probe-virtual" => ProbeVirtual(rest),
                "latency" => LatencyCmd(rest),
                "dry-run" => DryRun(ParseOptions(rest)),
                "bridge" => Bridge(ParseOptions(rest)),
                "install-driver" => InstallDriverOnly(),
                "hidhide-revert" => HidHideRevert(),
                "cleanup" => Cleanup(),
                "help" or "--help" or "-h" => Help(),
                _ => Fail($"Unknown command: {command}")
            };
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine(ex);
            return 1;
        }
    }

    static int Help()
    {
        Console.WriteLine("G25 -> Virtual G29 wheel bridge");
        Console.WriteLine();
        Console.WriteLine("Commands:");
        Console.WriteLine("  inspect                 Read-only: list DirectInput and HIDMaestro/G29 devices");
        Console.WriteLine("  dry-run [options]       Read-only: print normalized G25 axes, no virtual device");
        Console.WriteLine("  latency [--seconds N]   Measure report rate + G25->G29 latency (needs G29 mode on)");
        Console.WriteLine("  bridge [options]        Create a virtual Logitech G29 and feed it from the G25");
        Console.WriteLine("  install-driver          Install/refresh the HIDMaestro driver, then exit");
        Console.WriteLine("  cleanup                 Remove HIDMaestro virtual devices");
        Console.WriteLine();
        Console.WriteLine("Bridge options:");
        Console.WriteLine("  --profile <id>          HIDMaestro profile, default logitech-g29-usbip");
        Console.WriteLine("  --profiles-dir <path>   Load additional HIDMaestro profiles from a directory");
        Console.WriteLine("  --keep-existing         Do not purge stale virtual G29 devices before starting");
        Console.WriteLine("  --wheel-range <deg>     G25 rotation range at startup, default 900 (0 to skip)");
        Console.WriteLine("  --stop-event <name>     Named event a supervising service signals to stop cleanly");
        Console.WriteLine("  --user-sid <sid>        Watch HKU\\<sid>\\Software\\g25-driver and apply Rotation changes live");
        Console.WriteLine("  --rate-hz <n>           Resubmit rate while the wheel is idle, default 250 (live input follows the wheel's own ~200 Hz)");
        Console.WriteLine("  --duration <seconds>    Stop automatically after N seconds");
        Console.WriteLine("  --install-driver        Allow HIDMaestro driver install/refresh before bridge");
        Console.WriteLine("  --no-buttons            Do not forward G25 wheel and shifter buttons");
        Console.WriteLine("  --no-hat                Do not forward the G25 directional pad");
        Console.WriteLine("  --invert-accelerator    Invert accelerator axis");
        Console.WriteLine("  --invert-brake          Invert brake axis");
        Console.WriteLine("  --invert-clutch         Invert clutch axis");
        Console.WriteLine("  --trace-output          Print host output/feature reports sent to the virtual G29");
        Console.WriteLine("  --no-ffb                Do not relay G29 force-feedback commands to the G25");
        Console.WriteLine("  --ffb-translate         Normalise GFN FFB reports to lg4ff form (default: passthrough)");
        Console.WriteLine("  --no-hide-g25           Leave the physical G25 visible to local games (needs HidHide otherwise)");
        Console.WriteLine();
        Console.WriteLine("First-run setup (Administrator):");
        Console.WriteLine("  powershell -ExecutionPolicy Bypass -File scripts\\Run-Bridge-Admin.ps1 -BridgeArgs '--install-driver'");
        return 0;
    }

    static int Inspect()
    {
        Console.WriteLine("DirectInput game controllers:");
        using var di = new DirectInput();
        var devices = di.GetDevices(DeviceClass.GameControl, DeviceEnumerationFlags.AllDevices);
        foreach (var d in devices)
        {
            Console.WriteLine($"- {d.InstanceName} / {d.ProductName}");
            Console.WriteLine($"  InstanceGuid={d.InstanceGuid}");
            Console.WriteLine($"  ProductGuid ={d.ProductGuid}");
        }

        Console.WriteLine();
        Console.WriteLine("PnP devices matching G25/G29/HIDMaestro:");
        foreach (var d in PnpSnapshot.FindInterestingDevices())
        {
            Console.WriteLine($"- [{d.Status}] {d.Class}: {d.FriendlyName}");
            Console.WriteLine($"  {d.InstanceId}");
            if (d.HardwareIds.Count > 0) Console.WriteLine($"  HW: {string.Join(", ", d.HardwareIds)}");
        }

        return 0;
    }

    // Reads the raw HID input report of the virtual G29 (046D:C24F) from a
    // second process, to check whether the bridge's SubmitState actually reaches
    // the Windows HID input report while the bridge is running.
    static int ProbeVirtual(string[] args)
    {
        int vid = 0x046D, pid = 0xC24F;
        for (var i = 0; i < args.Length - 1; i++)
        {
            if (args[i] == "--vid") vid = Convert.ToInt32(args[i + 1], 16);
            if (args[i] == "--pid") pid = Convert.ToInt32(args[i + 1], 16);
        }

        var devices = HidSharp.DeviceList.Local.GetHidDevices(vid, pid).ToArray();
        if (devices.Length == 0) return Fail($"No HID device {vid:X4}:{pid:X4} found. Is the bridge running?");

        foreach (var device in devices)
        {
            Console.WriteLine($"{vid:X4}:{pid:X4} in={device.GetMaxInputReportLength()} out={device.GetMaxOutputReportLength()} path={device.DevicePath}");
            if (!device.TryOpen(out var stream)) { Console.WriteLine("  (could not open)"); continue; }
            using (stream)
            {
                stream.ReadTimeout = 500;
                var buf = new byte[device.GetMaxInputReportLength()];
                var stop = false;
                Console.CancelKeyPress += (_, e) => { e.Cancel = true; stop = true; };
                Console.WriteLine("  reading input reports, Ctrl+C to stop. Turn the physical wheel.");
                string? last = null;
                while (!stop)
                {
                    try
                    {
                        var n = stream.Read(buf, 0, buf.Length);
                        var hex = Convert.ToHexString(buf.AsSpan(0, n));
                        if (hex != last) { Console.WriteLine($"  IN {hex}"); last = hex; }
                    }
                    catch (TimeoutException) { }
                }
            }
        }
        return 0;
    }

    // Ctrl+C and --stop-event <name> both trigger a clean shutdown. A service
    // supervising this process signals the named event instead of Ctrl+C.
    static void RegisterShutdownSignals(BridgeOptions options)
    {
        Console.CancelKeyPress += (_, e) => { e.Cancel = true; Shutdown.Cancel(); };
        if (string.IsNullOrEmpty(options.StopEventName)) return;
        if (!EventWaitHandle.TryOpenExisting(options.StopEventName, out var stopEvent))
        {
            Console.Error.WriteLine($"--stop-event: no event named '{options.StopEventName}'; Ctrl+C still works.");
            return;
        }
        var t = new Thread(() =>
        {
            try { stopEvent.WaitOne(); } catch { }
            Console.WriteLine("Stop requested by the supervising service.");
            Shutdown.Cancel();
        }) { IsBackground = true, Name = "stop-event waiter" };
        t.Start();
    }

    static int DryRun(BridgeOptions options)
    {
        RegisterShutdownSignals(options);
        using var source = G25Source.Open();
        Console.WriteLine($"Reading: {source.Name}");
        Console.WriteLine("No virtual device is created in dry-run mode. Ctrl+C stops.");
        return PumpG25(null, source, null, options, printEveryFrame: false);
    }

    static int Bridge(BridgeOptions options)
    {
        if (!IsAdministrator())
        {
            return Fail("The bridge command must run as Administrator because HIDMaestro creates a virtual HID device.");
        }

        RegisterShutdownSignals(options);

        using var ctx = new HMContext();
        var loaded = ctx.LoadDefaultProfiles();
        Console.WriteLine($"Loaded HIDMaestro profiles: {loaded}");

        var bundledProfiles = Path.Combine(AppContext.BaseDirectory, "profiles");
        if (Directory.Exists(bundledProfiles))
        {
            var bundledLoaded = ctx.LoadProfilesFromDirectory(bundledProfiles);
            Console.WriteLine($"Loaded bundled HIDMaestro profiles: {bundledLoaded} from {bundledProfiles}");
        }

        if (!string.IsNullOrWhiteSpace(options.ProfilesDirectory))
        {
            var customLoaded = ctx.LoadProfilesFromDirectory(options.ProfilesDirectory);
            Console.WriteLine($"Loaded custom HIDMaestro profiles: {customLoaded} from {options.ProfilesDirectory}");
        }

        if (options.InstallDriver)
        {
            Console.WriteLine("Installing or refreshing HIDMaestro driver because --install-driver was provided...");
            ctx.InstallDriver();
        }
        else
        {
            Console.WriteLine("Skipping driver install/refresh. Add --install-driver for first-run setup.");
        }

        var profile = ctx.GetProfile(options.Profile);
        if (profile == null) return Fail($"HIDMaestro profile not found: {options.Profile}");

        if (!options.KeepExisting)
            PurgeStaleVirtualWheels(profile.VendorId, profile.ProductId);

        Console.WriteLine($"Creating virtual wheel: {profile.Name} ({profile.VendorId:X4}:{profile.ProductId:X4})");
        using var target = ctx.CreateController(profile);
        ctx.FinalizeNames();

        // Hide the physical G25 from local games so they bind the virtual G29.
        // Reverted when this scope unwinds (Ctrl+C, stop-event, or a throw).
        using var hidHide = options.HideLocalG25 ? HidHide.Cloak(Environment.ProcessPath ?? "") : null;

        // Serialise G25 HID output with g25tool / g25ff.dll / g25tray, which take
        // the same mutex around their writes.
        using var writerLock = AcquireWriterLock();

        // Creating a USB/IP controller can re-enumerate the physical USB tree.
        // Open the source only after the virtual controller has settled so the
        // DirectInput handle does not become stale during startup.
        using var source = G25Source.Open(TimeSpan.FromSeconds(15));
        using var forceFeedback = options.RelayForceFeedback ? G25ForceFeedbackRelay.Open(options.FfbTranslate ? Libg25.FfbMode.Translate : Libg25.FfbMode.Passthrough) : null;
        Console.WriteLine($"Wheel range: {options.WheelRangeDegrees} deg");
        forceFeedback?.SendWheelInit(options.WheelRangeDegrees);

        // Apply tray "Maximum rotation" changes live (SET_RANGE only) instead of
        // needing a G29-mode restart. Only meaningful when we actually drive the
        // wheel (forceFeedback != null) and the service told us whose tray to watch.
        using var rangeWatcher = forceFeedback != null && !string.IsNullOrEmpty(options.UserSid)
            ? TrayRangeWatcher.Start(options.UserSid!, options.WheelRangeDegrees, forceFeedback.SetRange, Shutdown.Token)
            : null;

        if (forceFeedback != null || options.TraceOutput)
        {
            target.OutputReceived += (_, packet) =>
            {
                // HIDMaestro's USB/IP backend currently exposes byte zero as a
                // report ID, although this G29 descriptor does not use report IDs.
                var raw = packet.ReportId == 0
                    ? packet.Data.ToArray()
                    : new[] { packet.ReportId }.Concat(packet.Data.ToArray()).ToArray();
                if (options.TraceOutput)
                    Console.WriteLine($"OUT source={packet.Source} raw={Convert.ToHexString(raw)}");
                forceFeedback?.Enqueue(raw);
            };
        }

        Console.WriteLine("Bridge is running. Wheel, independent pedals, buttons and directional pad are enabled.");
        Console.WriteLine(forceFeedback != null
            ? "Force feedback relay: virtual G29 -> physical G25 enabled (raw passthrough, see docs/ffb-protocol.md)."
            : "Force feedback relay: disabled.");
        Console.WriteLine("Tip: press each pedal fully to the floor once - the G25 firmware calibrates pedal travel per power-cycle.");
        Console.WriteLine("Ctrl+C removes the virtual device and exits.");
        return PumpG25(target, source, forceFeedback, options, printEveryFrame: false);
    }

    static int Cleanup()
    {
        if (!IsAdministrator()) return Fail("cleanup must run as Administrator.");
        Console.WriteLine("Removing HIDMaestro virtual controllers...");
        HMContext.RemoveAllVirtualControllers();
        PurgeStaleVirtualWheels(0x046D, 0xC24F);
        HidHide.RevertLeftoverState();
        Console.WriteLine("Cleanup done.");
        return 0;
    }

    static int LatencyCmd(string[] args)
    {
        var seconds = 20;
        for (var i = 0; i < args.Length - 1; i++)
            if (args[i] is "--seconds" or "-s" && int.TryParse(args[i + 1], out var n))
                seconds = Math.Clamp(n, 5, 120);
        return Latency.Measure(seconds);
    }

    // Undo a HidHide cloak a bridge worker left behind after a hard kill. The
    // service calls this on stop; harmless (no-op) when nothing is pending.
    static int HidHideRevert()
    {
        HidHide.RevertLeftoverState();
        return 0;
    }

    // Install / refresh the HIDMaestro UMDF driver only - for the installer, so
    // the first service start does not have to do it.
    static int InstallDriverOnly()
    {
        if (!IsAdministrator()) return Fail("install-driver must run as Administrator.");
        using var ctx = new HMContext();
        Console.WriteLine("Installing or refreshing the HIDMaestro driver...");
        ctx.InstallDriver();
        Console.WriteLine("HIDMaestro driver installed.");
        return 0;
    }

    // GeForce NOW keys wheels on VID:PID:bcdDevice. A stale virtual G29 from an
    // earlier run - especially a HIDMaestro UMDF node that enumerates as
    // 046D:C24F:0100 - makes GFN log "No known device with interface number 0"
    // and refuse wheel input entirely. Remove anything that is clearly one of
    // our virtual wheels before creating a fresh one. The physical G25 (C299 /
    // C294) and a genuine retail G29 (other bcdDevice) are left alone.
    static void PurgeStaleVirtualWheels(int vendorId, int productId)
    {
        try { HMContext.RemoveAllVirtualControllers(); } catch { /* best effort */ }

        if (!OperatingSystem.IsWindows()) return;

        var vidpid = $"VID_{vendorId:X4}&PID_{productId:X4}";
        var script =
            "$ErrorActionPreference='SilentlyContinue';" +
            "Get-PnpDevice | Where-Object {" +
            $"  ($_.InstanceId -match '{vidpid}' -or $_.FriendlyName -match 'Driving Force Racing Wheel') -and" +
            "   $_.InstanceId -notmatch 'VID_046D&PID_C299|VID_046D&PID_C294' -and" +
            "   ( $_.Status -ne 'OK' -or $_.InstanceId -match 'REV_8900|REV_0100|^ROOT\\\\HIDCLASS' -or" +
            "     ((Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName DEVPKEY_Device_DriverInfPath).Data -match 'oem') )" +
            "} | ForEach-Object {" +
            "  Write-Output ('purge ' + $_.Status + ' ' + $_.InstanceId);" +
            "  pnputil.exe /remove-device $_.InstanceId 2>&1 | Out-Null" +
            "}; pnputil.exe /scan-devices | Out-Null";

        try
        {
            var psi = new ProcessStartInfo("powershell.exe", "-NoProfile -NonInteractive -Command " + "\"" + script.Replace("\"", "\\\"") + "\"")
            {
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true,
            };
            using var p = Process.Start(psi);
            if (p == null) return;
            var output = p.StandardOutput.ReadToEnd();
            p.WaitForExit(15000);
            foreach (var line in output.Split('\n', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
                Console.WriteLine("  " + line);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"Could not purge stale virtual wheels: {ex.Message}");
        }
    }

    static int PumpG25(HMController? target, G25Source source, G25ForceFeedbackRelay? ffb, BridgeOptions options, bool printEveryFrame)
    {
        var axes = new Dictionary<HMAxis, float>
        {
            [HMAxis.X] = 0.5f,
            [HMAxis.Z] = 0f,
            [HMAxis.Rz] = 0f,
            [HMAxis.Y] = 0f,
        };
        var state = new HMGamepadState { Axes = axes, Hat = HMHat.None, Buttons = 0 };
        var deadlineUtc = options.DurationSeconds > 0 ? DateTime.UtcNow.AddSeconds(options.DurationSeconds) : DateTime.MaxValue;

        // Event-driven: WaitFrame returns as soon as the reader thread decodes a
        // new G25 report (~200 Hz), so the virtual wheel is fed at the source
        // rate. The timeout only bounds how long we sit on a silent wheel before
        // resubmitting the last frame; keep it clear of the ~5 ms frame cadence.
        var stallTimeoutMs = Math.Clamp(1000 / Math.Clamp(options.RateHz, 10, 100), 10, 100);
        var sw = Stopwatch.StartNew();
        long lastPrint = -1;
        var submits = 0;

        while (!Shutdown.IsCancellationRequested && DateTime.UtcNow < deadlineUtc)
        {
            G25Frame frame;
            try { frame = source.WaitFrame(stallTimeoutMs); }
            catch (IOException)
            {
                // The physical G25 was read at least once and then went away for
                // good (WHEEL: gone already printed). Exit 3 so the service tears
                // the virtual wheel down instead of restart-looping.
                Console.Error.WriteLine("Physical G25 disconnected; stopping the bridge.");
                return 3;
            }
            axes[HMAxis.X] = frame.Wheel;
            axes[HMAxis.Z] = MaybeInvert(frame.Accelerator, options.InvertAccelerator);
            axes[HMAxis.Rz] = MaybeInvert(frame.Brake, options.InvertBrake);
            axes[HMAxis.Y] = MaybeInvert(frame.Clutch, options.InvertClutch);
            state.Buttons = options.ForwardButtons ? (HMButton)frame.Buttons : 0;
            state.Hat = options.ForwardHat ? HatFromPov(frame.Pov) : HMHat.None;

            target?.SubmitState(in state);
            submits++;

            var sec = sw.ElapsedMilliseconds / 1000;
            if (printEveryFrame || sec != lastPrint)
            {
                lastPrint = sec;
                var ffbHz = ffb?.TakeReceivedDelta() ?? 0;
                var inHz = submits;
                submits = 0;
                Console.WriteLine($"wheel={axes[HMAxis.X]:0.000} accel={axes[HMAxis.Z]:0.000} brake={axes[HMAxis.Rz]:0.000} clutch={axes[HMAxis.Y]:0.000} buttons=0x{(uint)state.Buttons:X} hat={state.Hat} inHz={inHz} ffbHz={ffbHz}");
            }
        }

        return 0;
    }

    static BridgeOptions ParseOptions(string[] args)
    {
        var options = new BridgeOptions();
        for (var i = 0; i < args.Length; i++)
        {
            var arg = args[i].ToLowerInvariant();
            switch (arg)
            {
                case "--profile":
                    options.Profile = RequireValue(args, ref i, "--profile");
                    break;
                case "--profiles-dir":
                    options.ProfilesDirectory = RequireValue(args, ref i, "--profiles-dir");
                    break;
                case "--rate-hz":
                    if (!int.TryParse(RequireValue(args, ref i, "--rate-hz"), out var rate)) throw new ArgumentException("--rate-hz must be an integer");
                    options.RateHz = rate;
                    break;
                case "--duration":
                    if (!double.TryParse(RequireValue(args, ref i, "--duration"), System.Globalization.CultureInfo.InvariantCulture, out var duration)) throw new ArgumentException("--duration must be a number of seconds");
                    options.DurationSeconds = duration;
                    break;
                case "--install-driver": options.InstallDriver = true; break;
                case "--buttons": options.ForwardButtons = true; break;
                case "--hat": options.ForwardHat = true; break;
                case "--no-buttons": options.ForwardButtons = false; break;
                case "--no-hat": options.ForwardHat = false; break;
                case "--invert-accelerator": options.InvertAccelerator = true; break;
                case "--invert-brake": options.InvertBrake = true; break;
                case "--invert-clutch": options.InvertClutch = true; break;
                case "--trace-output": options.TraceOutput = true; break;
                case "--no-ffb": options.RelayForceFeedback = false; break;
                case "--ffb-translate": options.FfbTranslate = true; break;
                case "--hide-g25": options.HideLocalG25 = true; break;
                case "--no-hide-g25": options.HideLocalG25 = false; break;
                case "--keep-existing": options.KeepExisting = true; break;
                case "--wheel-range":
                    if (!int.TryParse(RequireValue(args, ref i, "--wheel-range"), out var deg)) throw new ArgumentException("--wheel-range must be an integer (40-900, or 0 to skip)");
                    options.WheelRangeDegrees = deg;
                    break;
                case "--stop-event":
                    options.StopEventName = RequireValue(args, ref i, "--stop-event");
                    break;
                case "--user-sid":
                    options.UserSid = RequireValue(args, ref i, "--user-sid");
                    break;
                default: throw new ArgumentException($"Unknown option: {args[i]}");
            }
        }
        return options;
    }

    static string RequireValue(string[] args, ref int index, string option)
    {
        if (index + 1 >= args.Length) throw new ArgumentException($"{option} requires a value");
        index++;
        return args[index];
    }

    static int Fail(string message)
    {
        Console.Error.WriteLine(message);
        return 1;
    }

    static bool IsAdministrator()
    {
        using var identity = WindowsIdentity.GetCurrent();
        return new WindowsPrincipal(identity).IsInRole(WindowsBuiltInRole.Administrator);
    }

    // Same name g25tool / g25ff.dll / g25tray use (src/device/g25_device.cpp).
    const string WriterMutexName = @"Local\g25tool-output-v1";

    static IDisposable AcquireWriterLock()
    {
        var mutex = new Mutex(false, WriterMutexName);
        bool held;
        try { held = mutex.WaitOne(TimeSpan.FromSeconds(5)); }
        catch (AbandonedMutexException) { held = true; }
        if (!held)
            Console.Error.WriteLine("Could not take the shared G25 writer lock in 5s; proceeding anyway.");
        return new Releaser(mutex, held);
    }

    sealed class Releaser(Mutex mutex, bool held) : IDisposable
    {
        public void Dispose()
        {
            if (held) { try { mutex.ReleaseMutex(); } catch { } }
            mutex.Dispose();
        }
    }

    static float MaybeInvert(float value, bool invert) => invert ? 1f - value : value;

    static HMHat HatFromPov(int pov) => pov switch
    {
        0 => HMHat.North,
        4500 => HMHat.NorthEast,
        9000 => HMHat.East,
        13500 => HMHat.SouthEast,
        18000 => HMHat.South,
        22500 => HMHat.SouthWest,
        27000 => HMHat.West,
        31500 => HMHat.NorthWest,
        _ => HMHat.None,
    };
}
