// SPDX-License-Identifier: GPL-2.0-only
using System.Diagnostics;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace G25VirtualG29;

/// <summary>
/// Measures the input pipeline between the physical G25 (046D:C299) and the
/// virtual G29 (046D:C24F): the HID report rate on each side, and the latency
/// from a G25 report arriving to the matching change being observable on the
/// virtual G29.
///
/// Method: read both HID input streams on their own threads, timestamp every
/// report with QueryPerformanceCounter. The user gives a series of sharp inputs
/// (a pedal stab is the cleanest step); for each detected edge on the G25 side
/// the tool finds the matching edge on the G29 side and records the delay. It
/// also cross-correlates the steering channel as an independent check.
///
/// Excluded from the number: the wheel/pedal sensor -> USB frame delay (fixed,
/// hardware) and everything downstream of the virtual G29 (a game's poll rate,
/// or GeForce NOW's sampling + network + remote host).
/// </summary>
static class Latency
{
    private const int LogitechVid = 0x046D;
    private const int G25Pid = 0xC299;
    private const int G29Pid = 0xC24F;

    // channel name -> (physical G25 selector, virtual G29 report decoder).
    // The virtual G29 input report (logitech-g29-usbip profile, no report ID):
    //   byte 0..3 hat+buttons, 4..5 X (LE16), 6 Z, 7 Rz, 8 Y, 9..11 vendor.
    // Windows may prefix a synthetic report-ID 0 byte -> `o` is that offset.
    private static readonly (string name, Func<Libg25.InputState, double> g25, Func<byte[], int, double> g29, double fullScale)[] Channels =
    {
        ("wheel",    s => s.Wheel,    (r, o) => r[o + 4] | (r[o + 5] << 8), 65535),
        ("throttle", s => s.Throttle, (r, o) => r[o + 6],                    255),
        ("brake",    s => s.Brake,    (r, o) => r[o + 7],                    255),
        ("clutch",   s => s.Clutch,   (r, o) => r[o + 8],                    255),
    };

    public static int Measure(int seconds)
    {
        using var g25 = HidReader.Open(LogitechVid, G25Pid);
        using var g29 = HidReader.Open(LogitechVid, G29Pid);
        if (g25 == null) return Fail("physical G25 (046D:C299) not found. Is it in native mode?");
        if (g29 == null) return Fail("virtual G29 (046D:C24F) not found. Enable G29 mode (or run `bridge`) first.");
        Console.WriteLine($"G25: {g25.Info}");
        Console.WriteLine($"G29: {g29.Info}");

        var freq = (double)Stopwatch.Frequency;
        var g25Raw = new List<(long t, double[] v)>(seconds * 400);
        var g29Raw = new List<(long t, double[] v)>(seconds * 400);
        var stop = new CancellationTokenSource();
        var t0 = Stopwatch.GetTimestamp();

        var tA = Reader(g25, g25Raw, stop.Token, r =>
        {
            if (Libg25.DecodeInput(r) is not { } s) return null;
            return Channels.Select(c => c.g25(s)).ToArray();
        });
        var tB = Reader(g29, g29Raw, stop.Token, r =>
        {
            var o = r.Length >= 13 ? 1 : 0;           // skip a synthetic report-ID byte
            if (r.Length < o + 9) return null;
            var v = new double[Channels.Length];
            for (var i = 0; i < Channels.Length; i++) v[i] = Channels[i].g29(r, o);
            return v;
        });

        Console.WriteLine();
        Console.WriteLine($"Measuring for {seconds}s - START NOW: stab a pedal fully to the floor and release,");
        Console.WriteLine("about once a second, the whole time. (Flicking the wheel lock-to-lock also works.)");
        Console.WriteLine();
        for (var s = seconds; s > 0; s--) { Console.Write($"\r  {s,3}s  keep stabbing "); Thread.Sleep(1000); }
        Console.WriteLine("\r  done.               ");

        stop.Cancel();
        tA.Join(1000);
        tB.Join(1000);

        var a = Order(g25Raw, t0, freq);
        var b = Order(g29Raw, t0, freq);
        if (a.Count < 100 || b.Count < 100)
            return Fail($"too few samples (G25 {a.Count}, G29 {b.Count}).");

        ReportRate("G25 physical (046D:C299)", a);
        ReportRate("G29 virtual  (046D:C24F)", b);

        // Per-edge latency: match midpoint crossings of a real step (a pedal
        // stab or a wheel flick), pooled across every channel that was used.
        var all = new List<double>();
        var usedChannels = new List<string>();
        var totalDropped = 0;
        var busy = -1; var busyRange = 0.0;
        for (var ch = 0; ch < Channels.Length; ch++)
        {
            var s25 = a.Select(x => (x.t, x.v[ch])).ToList();
            var s29 = b.Select(x => (x.t, x.v[ch])).ToList();
            var range = s25.Max(p => p.Item2) - s25.Min(p => p.Item2);
            if (range < 0.15 * Channels[ch].fullScale) continue;   // channel not exercised
            if (range > busyRange) { busyRange = range; busy = ch; }

            var (e, dropped) = CrossingLatenciesMs(s25, s29);
            totalDropped += dropped;
            if (e.Count > 0) { all.AddRange(e); usedChannels.Add($"{Channels[ch].name}x{e.Count}"); }
        }

        var xc = busy < 0 ? (0.0, 0.0) : CrossCorrLagMs(
            a.Select(x => (x.t, x.v[busy])).ToList(),
            b.Select(x => (x.t, x.v[busy])).ToList());

        Console.WriteLine();
        Console.WriteLine("Latency  (G25 HID report in -> G29 report observable)");
        if (all.Count >= 3)
        {
            all.Sort();
            Console.WriteLine($"  per-edge   n={all.Count,3}   median {Pct(all, 50),4:0.0} ms   "
                              + $"p10 {Pct(all, 10),4:0.0}   p90 {Pct(all, 90),4:0.0} ms"
                              + (totalDropped > 0 ? $"   ({totalDropped} unmatched)" : ""));
            Console.WriteLine($"             from: {string.Join(", ", usedChannels)}");
        }
        else
        {
            Console.WriteLine("  per-edge   : no clean step captured - stab a pedal fully to the floor a few times");
        }
        if (busy >= 0 && xc.Item2 >= 0.80)
            Console.WriteLine($"  x-corr     {xc.Item1,4:0.0} ms   (on '{Channels[busy].name}', fit r={xc.Item2:0.00})");
        else
            Console.WriteLine("  x-corr     inconclusive (needs a smooth wheel sweep)");
        Console.WriteLine();
        Console.WriteLine("Excludes the sensor->USB delay and anything past the virtual G29");
        Console.WriteLine("(game poll rate, or GeForce NOW sampling + network).");
        return 0;
    }

    // --- rate ------------------------------------------------------------------

    private static void ReportRate(string label, List<(double t, double[] v)> s)
    {
        var span = s[^1].t - s[0].t;
        var rate = (s.Count - 1) / (span / 1000.0);
        var iv = new List<double>(s.Count);
        for (var i = 1; i < s.Count; i++) iv.Add(s[i].t - s[i - 1].t);
        iv.Sort();
        Console.WriteLine($"{label} : {rate,6:0} Hz   gap min {iv[0],4:0.0}  median {Pct(iv, 50),4:0.0}  "
                          + $"p95 {Pct(iv, 95),4:0.0}  max {iv[^1],5:0.0} ms   (n={s.Count})");
    }

    // --- edges: match midpoint crossings of a step input --------------------

    // A crossing: the signal passes its own midpoint with hysteresis. Time is
    // linearly interpolated between the two bracketing samples (sub-sample).
    private static List<(double t, int dir)> Crossings(List<(double t, double v)> s)
    {
        var lo = s.Min(p => p.v);
        var hi = s.Max(p => p.v);
        var mid = 0.5 * (lo + hi);
        var band = 0.20 * (hi - lo);
        var res = new List<(double, int)>();

        var armedLow = s[0].v < mid;   // ready to report a rising crossing
        var armedHigh = s[0].v > mid;
        for (var i = 1; i < s.Count; i++)
        {
            var v = s[i].v;
            if (armedLow && v > mid + band)
            {
                res.Add((Interp(s[i - 1], s[i], mid), +1));
                armedLow = false; armedHigh = true;
            }
            else if (armedHigh && v < mid - band)
            {
                res.Add((Interp(s[i - 1], s[i], mid), -1));
                armedHigh = false; armedLow = true;
            }
        }
        return res;
    }

    private static double Interp((double t, double v) a, (double t, double v) b, double y)
        => Math.Abs(b.v - a.v) < 1e-9 ? b.t : a.t + (y - a.v) / (b.v - a.v) * (b.t - a.t);

    // Returns (matched latencies ms, count of G25 edges with no confident match).
    private static (List<double> lat, int dropped) CrossingLatenciesMs(
        List<(double t, double v)> g25, List<(double t, double v)> g29)
    {
        var res = new List<double>();
        var dropped = 0;
        var xa = Crossings(g25);
        var xb = Crossings(g29);
        if (xa.Count == 0 || xb.Count == 0) return (res, xa.Count);

        const double lo = -3, hi = 30;   // a plausible pipeline window (ms)
        foreach (var (tc, dir) in xa)
        {
            // mutual nearest neighbour: the G25 edge's closest same-direction G29
            // edge must also have this G25 edge as its closest. Kills mispairs
            // when the user pumps fast and an edge is missed on one side.
            int bj = Nearest(xb, tc, dir);
            if (bj < 0) { dropped++; continue; }
            var dt = xb[bj].t - tc;
            if (dt < lo || dt > hi || Nearest(xa, xb[bj].t, dir) is var ba && ba >= 0 && xa[ba].t != tc)
            { dropped++; continue; }
            res.Add(dt);
        }
        return (res, dropped);
    }

    private static int Nearest(List<(double t, int dir)> xs, double t, int dir)
    {
        var best = -1; var bestAbs = double.MaxValue;
        for (var i = 0; i < xs.Count; i++)
        {
            if (xs[i].dir != dir) continue;
            var d = Math.Abs(xs[i].t - t);
            if (d < bestAbs) { bestAbs = d; best = i; }
        }
        return best;
    }

    // --- cross-correlation --------------------------------------------------

    private static (double lagMs, double quality) CrossCorrLagMs(
        List<(double t, double v)> x, List<(double t, double v)> y)
    {
        const double grid = 0.5, maxLagMs = 60;
        var (a, b) = Resample(x, y, grid);
        if (a.Length == 0) return (0, 0);
        var da = Diff(a); var db = Diff(b);
        ZeroMean(da); ZeroMean(db);

        var maxLag = (int)(maxLagMs / grid);
        var best = 0; var bestScore = double.NegativeInfinity;
        for (var lag = 0; lag <= maxLag; lag++)
        {
            var score = NormDot(db, da, lag);
            if (score > bestScore) { bestScore = score; best = lag; }
        }
        return (best * grid, bestScore);
    }

    // --- resampling / numerics --------------------------------------------

    private static (double[] a, double[] b) Resample(
        List<(double t, double v)> x, List<(double t, double v)> y, double grid)
    {
        var start = Math.Max(x[0].t, y[0].t);
        var end = Math.Min(x[^1].t, y[^1].t);
        var n = (int)((end - start) / grid);
        if (n < 50) return (Array.Empty<double>(), Array.Empty<double>());
        return (Grid(x, start, grid, n), Grid(y, start, grid, n));
    }

    private static double[] Grid(List<(double t, double v)> s, double start, double step, int n)
    {
        var g = new double[n];
        var j = 0;
        for (var i = 0; i < n; i++)
        {
            var tt = start + i * step;
            while (j + 1 < s.Count && s[j + 1].t <= tt) j++;
            g[i] = s[j].v;
        }
        return g;
    }

    private static double[] Diff(double[] x)
    {
        var d = new double[x.Length];
        for (var i = 1; i < x.Length; i++) d[i] = x[i] - x[i - 1];
        return d;
    }

    private static void ZeroMean(double[] x)
    {
        var m = x.Average();
        for (var i = 0; i < x.Length; i++) x[i] -= m;
    }

    private static double NormDot(double[] y, double[] x, int lag)
    {
        double dot = 0, ny = 0, nx = 0;
        for (var i = lag; i < y.Length; i++)
        {
            dot += y[i] * x[i - lag];
            ny += y[i] * y[i];
            nx += x[i - lag] * x[i - lag];
        }
        return ny > 0 && nx > 0 ? dot / Math.Sqrt(ny * nx) : 0;
    }

    private static double Pct(List<double> data, double p)
    {
        if (data.Count == 0) return double.NaN;
        var s = data;
        for (var i = 1; i < s.Count; i++) if (s[i] < s[i - 1]) { s = new List<double>(data); s.Sort(); break; }
        return s[Math.Clamp((int)Math.Round(p / 100.0 * (s.Count - 1)), 0, s.Count - 1)];
    }

    private static List<(double t, double[] v)> Order(List<(long t, double[] v)> raw, long t0, double freq)
    {
        lock (raw)
            return raw.Select(s => ((s.t - t0) / freq * 1000.0, s.v)).OrderBy(s => s.Item1).ToList();
    }

    private static Thread Reader(HidReader r, List<(long, double[])> into, CancellationToken ct,
        Func<byte[], double[]?> extract)
    {
        var t = new Thread(() =>
        {
            while (!ct.IsCancellationRequested)
            {
                var n = r.Read();
                var ts = Stopwatch.GetTimestamp();
                if (n <= 0) continue;
                var report = new byte[n];
                Array.Copy(r.Buffer, report, n);   // HidP / libg25 need the exact report length
                var v = extract(report);
                if (v != null) lock (into) into.Add((ts, v));
            }
        }) { IsBackground = true, Priority = ThreadPriority.AboveNormal };
        t.Start();
        return t;
    }

    private static int Fail(string m) { Console.Error.WriteLine(m); return 1; }

    // --- HID reader --------------------------------------------------------

    private sealed class HidReader : IDisposable
    {
        private readonly SafeFileHandle _h;
        private readonly IntPtr _pp;
        public byte[] Buffer { get; }
        public string Info { get; }

        private HidReader(SafeFileHandle h, IntPtr pp, Native.HIDP_CAPS caps)
        {
            _h = h; _pp = pp;
            Buffer = new byte[Math.Max((int)caps.InputReportByteLength, 64)];
            Info = $"usage {caps.UsagePage:X2}:{caps.Usage:X2}, input report {caps.InputReportByteLength} B";
        }

        public static HidReader? Open(int vid, int pid)
        {
            HidReader? fallback = null;
            foreach (var path in Native.EnumHidPaths())
            {
                if (!path.Contains($"vid_{vid:x4}&pid_{pid:x4}", StringComparison.OrdinalIgnoreCase)) continue;
                var h = Native.CreateFile(path, Native.GENERIC_READ, Native.FILE_SHARE_RW, IntPtr.Zero,
                    Native.OPEN_EXISTING, 0, IntPtr.Zero);
                if (h.IsInvalid) continue;
                if (!Native.HidD_GetPreparsedData(h, out var pp)) { h.Dispose(); continue; }
                Native.HidP_GetCaps(pp, out var caps);
                if (caps.InputReportByteLength == 0) { Native.HidD_FreePreparsedData(pp); h.Dispose(); continue; }

                var reader = new HidReader(h, pp, caps);
                // Prefer the Generic-Desktop joystick/gamepad collection.
                if (caps.UsagePage == 0x01 && caps.Usage is 0x04 or 0x05)
                    return reader;
                fallback ??= reader;
            }
            return fallback;
        }

        public int Read() => Native.ReadFile(_h, Buffer, Buffer.Length, out var n, IntPtr.Zero) ? n : -1;

        public void Dispose()
        {
            if (_pp != IntPtr.Zero) Native.HidD_FreePreparsedData(_pp);
            _h.Dispose();
        }
    }

    private static class Native
    {
        public const uint GENERIC_READ = 0x80000000;
        public const uint FILE_SHARE_RW = 0x3;
        public const uint OPEN_EXISTING = 3;

        [DllImport("hid.dll")] public static extern void HidD_GetHidGuid(out Guid g);
        [DllImport("hid.dll")] public static extern bool HidD_GetPreparsedData(SafeFileHandle h, out IntPtr pp);
        [DllImport("hid.dll")] public static extern bool HidD_FreePreparsedData(IntPtr pp);
        [DllImport("hid.dll")] public static extern int HidP_GetCaps(IntPtr pp, out HIDP_CAPS caps);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern SafeFileHandle CreateFile(string name, uint access, uint share, IntPtr sa,
            uint disp, uint flags, IntPtr template);
        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern bool ReadFile(SafeFileHandle h, byte[] buf, int n, out int read, IntPtr overlapped);

        [DllImport("setupapi.dll", CharSet = CharSet.Unicode)]
        static extern IntPtr SetupDiGetClassDevs(ref Guid g, string? e, IntPtr w, int f);
        [DllImport("setupapi.dll")]
        static extern bool SetupDiEnumDeviceInterfaces(IntPtr s, IntPtr d, ref Guid g, int i, ref SP_DIDATA a);
        [DllImport("setupapi.dll", CharSet = CharSet.Unicode)]
        static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr s, ref SP_DIDATA a, IntPtr det, int sz,
            ref int req, IntPtr dd);
        [DllImport("setupapi.dll", CharSet = CharSet.Unicode)]
        static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr s, ref SP_DIDATA a, ref SP_DIDETAIL det,
            int sz, ref int req, IntPtr dd);
        [DllImport("setupapi.dll")] static extern bool SetupDiDestroyDeviceInfoList(IntPtr s);

        [StructLayout(LayoutKind.Sequential)]
        struct SP_DIDATA { public int cbSize; public Guid g; public int f; public IntPtr r; }
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        struct SP_DIDETAIL { public int cbSize; [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)] public string p; }

        [StructLayout(LayoutKind.Sequential)]
        public struct HIDP_CAPS
        {
            public ushort Usage, UsagePage, InputReportByteLength, OutputReportByteLength, FeatureReportByteLength;
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 17)] public ushort[] Reserved;
            public ushort NumberLinkCollectionNodes, NumberInputButtonCaps, NumberInputValueCaps, NumberInputDataIndices;
            public ushort NumberOutputButtonCaps, NumberOutputValueCaps, NumberOutputDataIndices;
            public ushort NumberFeatureButtonCaps, NumberFeatureValueCaps, NumberFeatureDataIndices;
        }

        public static IEnumerable<string> EnumHidPaths()
        {
            HidD_GetHidGuid(out var guid);
            var set = SetupDiGetClassDevs(ref guid, null, IntPtr.Zero, 0x12);
            try
            {
                var a = new SP_DIDATA { cbSize = Marshal.SizeOf<SP_DIDATA>() };
                for (var i = 0; SetupDiEnumDeviceInterfaces(set, IntPtr.Zero, ref guid, i, ref a); i++)
                {
                    var req = 0;
                    SetupDiGetDeviceInterfaceDetail(set, ref a, IntPtr.Zero, 0, ref req, IntPtr.Zero);
                    var det = new SP_DIDETAIL { cbSize = IntPtr.Size == 8 ? 8 : 6 };
                    if (SetupDiGetDeviceInterfaceDetail(set, ref a, ref det, req, ref req, IntPtr.Zero))
                        yield return det.p;
                }
            }
            finally { SetupDiDestroyDeviceInfoList(set); }
        }
    }
}
