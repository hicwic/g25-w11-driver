// SPDX-License-Identifier: GPL-2.0-only
using System.Runtime.InteropServices;

namespace G25VirtualG29;

/// <summary>
/// P/Invoke over <c>libg25.dll</c> (built from the driver's <c>src/libg25/</c>).
/// The G25 report layout and command bytes live there, not here.
/// </summary>
static partial class Libg25
{
    private const string Dll = "libg25";

    [StructLayout(LayoutKind.Sequential)]
    public struct InputState
    {
        public ushort Wheel;      // 0..16383
        public byte Throttle;     // 0..255, 255 = released
        public byte Brake;
        public byte Clutch;
        public byte Hat;          // 0..7 = 45 deg steps, >=8 = centred
        public uint Buttons;      // bit 0 = button 1
    }

    public enum Model { Unknown = 0, G25 = 1, G27 = 2, G29 = 3 }

    [LibraryImport(Dll, EntryPoint = "g25_identify_model")]
    public static partial int IdentifyModel(ushort pid, ushort revision);

    [LibraryImport(Dll, EntryPoint = "g25_decode_input")]
    private static partial int g25_decode_input(ReadOnlySpan<byte> report, int len, out InputState state);

    [LibraryImport(Dll, EntryPoint = "g25_cmd_native_mode")]
    private static partial void g25_cmd_native_mode(Span<byte> out8);

    [LibraryImport(Dll, EntryPoint = "g25_cmd_stop_all")]
    private static partial void g25_cmd_stop_all(Span<byte> out8);

    [LibraryImport(Dll, EntryPoint = "g25_cmd_disable_autocenter")]
    private static partial void g25_cmd_disable_autocenter(Span<byte> out8);

    [LibraryImport(Dll, EntryPoint = "g25_cmd_set_range")]
    private static partial int g25_cmd_set_range(int degrees, Span<byte> out8);

    [LibraryImport(Dll, EntryPoint = "g25_ffb_translate")]
    private static partial int g25_ffb_translate(int mode, ReadOnlySpan<byte> input, int inLen, Span<byte> output, int outCap);

    /// <summary>0 = passthrough, 1 = normalise GFN's G29 constant-force report to lg4ff form.</summary>
    public enum FfbMode { Passthrough = 0, Translate = 1 }

    [LibraryImport(Dll, EntryPoint = "g25_libg25_version", StringMarshalling = StringMarshalling.Utf8)]
    public static partial string Version();

    /// <summary>Decode a 12-byte Windows HID input report. Returns null on a malformed report.</summary>
    public static InputState? DecodeInput(ReadOnlySpan<byte> report)
        => g25_decode_input(report, report.Length, out var s) == 0 ? s : null;

    public static byte[] NativeMode() { var b = new byte[8]; g25_cmd_native_mode(b); return b; }
    public static byte[] StopAll() { var b = new byte[8]; g25_cmd_stop_all(b); return b; }
    public static byte[] DisableAutocenter() { var b = new byte[8]; g25_cmd_disable_autocenter(b); return b; }

    /// <summary>SET_RANGE report (8 bytes) for 40..900 degrees, or null if out of range.</summary>
    public static byte[]? SetRange(int degrees)
    {
        var b = new byte[8];
        return g25_cmd_set_range(degrees, b) == 0 ? b : null;
    }

    /// <summary>
    /// Translate one output report the virtual G29 received into 0..N eight-byte
    /// G25 output reports.
    /// </summary>
    public static List<byte[]> FfbTranslate(ReadOnlySpan<byte> virtualG29Report, FfbMode mode)
    {
        var buf = new byte[8 * 4];
        var n = g25_ffb_translate((int)mode, virtualG29Report, virtualG29Report.Length, buf, 4);
        var result = new List<byte[]>(Math.Max(0, n));
        for (var i = 0; i < n; i++)
            result.Add(buf.AsSpan(i * 8, 8).ToArray());
        return result;
    }
}
