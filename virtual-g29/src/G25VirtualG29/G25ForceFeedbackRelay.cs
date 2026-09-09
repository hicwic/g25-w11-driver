// SPDX-License-Identifier: GPL-2.0-only
using System.Collections.Concurrent;
using HidSharp;

namespace G25VirtualG29;

/// <summary>
/// Forwards force-feedback output reports received by the virtual G29 to the
/// physical G25 output endpoint.
/// </summary>
/// <remarks>
/// Translation is delegated to <c>libg25.dll</c> (<c>g25_ffb_translate</c>).
/// Phase 1 of that function is a passthrough of the classic-format reports the
/// virtual G29 receives; a protocol-accurate table is future work
/// (docs/ffb-protocol.md, hid-lg4ff.c).
/// </remarks>
sealed class G25ForceFeedbackRelay : IDisposable
{
    private const int LogitechVendorId = 0x046D;
    private const int G25NativeProductId = 0xC299;

    private static readonly TimeSpan ReconnectWindow = TimeSpan.FromSeconds(30);

    private HidStream _stream;
    private readonly BlockingCollection<byte[]> _commands = new(512);
    private readonly Thread _writer;
    private int _dropped;
    private long _received;
    private volatile bool _stopping;
    private Exception? _failure;

    /// <summary>Game FFB reports received since the last call (for telemetry).</summary>
    public long TakeReceivedDelta()
    {
        var now = Interlocked.Read(ref _received);
        return now - Interlocked.Exchange(ref _lastReported, now);
    }
    private long _lastReported;

    private readonly Libg25.FfbMode _ffbMode;

    private G25ForceFeedbackRelay(HidStream stream, string productName, Libg25.FfbMode ffbMode)
    {
        _stream = stream;
        _ffbMode = ffbMode;
        _writer = new Thread(WriteLoop) { IsBackground = true, Name = "G25 force-feedback relay" };
        _writer.Start();
        Console.WriteLine($"Force feedback output opened: {productName} (mode: {ffbMode})");
    }

    /// <summary>
    /// Bring the physical G25 to a known state: stop forces, disable autocenter,
    /// set rotation range. Without G HUB running, nothing else does this.
    /// </summary>
    public void SendWheelInit(int rangeDegrees)
    {
        _commands.TryAdd(Libg25.StopAll());
        _commands.TryAdd(Libg25.DisableAutocenter());
        var range = Libg25.SetRange(rangeDegrees);
        if (range != null)
        {
            _commands.TryAdd(range);
            Console.WriteLine($"Wheel init: stop forces, autocenter off, range {rangeDegrees} deg.");
        }
        else
        {
            Console.WriteLine("Wheel init: stop forces, autocenter off.");
        }
    }

    /// <summary>
    /// Apply a new steering range mid-session (SET_RANGE only - no stop-forces /
    /// autocenter reset, so the wheel stays centred and forces keep flowing).
    /// Queued on the same writer thread as the FFB reports.
    /// </summary>
    public void SetRange(int degrees)
    {
        if (_failure != null) return;
        var range = Libg25.SetRange(degrees);
        if (range == null) return;
        _commands.TryAdd(range);
        Console.WriteLine($"Wheel range: {degrees} deg");
    }

    private static HidStream? TryAcquireStream()
    {
        foreach (var device in DeviceList.Local.GetHidDevices(LogitechVendorId, G25NativeProductId))
        {
            if (device.GetMaxOutputReportLength() != 8) continue;
            if (device.TryOpen(out var stream))
            {
                stream.WriteTimeout = 250;
                return stream;
            }
        }
        return null;
    }

    public static G25ForceFeedbackRelay Open(Libg25.FfbMode ffbMode)
    {
        var stream = TryAcquireStream();
        if (stream == null)
            throw new InvalidOperationException(
                "The physical G25 HID force-feedback output was not found (expected 046D:C299 with an 8-byte output report).");

        string name;
        try { name = stream.Device.GetProductName(); }
        catch { name = "Logitech G25"; }
        return new G25ForceFeedbackRelay(stream, name, ffbMode);
    }

    private bool Reconnect()
    {
        Console.Error.WriteLine("Force feedback output write failed (USB re-enumeration?). Trying to reconnect...");
        var deadline = DateTime.UtcNow + ReconnectWindow;
        while (!_stopping && DateTime.UtcNow < deadline)
        {
            var stream = TryAcquireStream();
            if (stream != null)
            {
                try { _stream.Dispose(); } catch { }
                _stream = stream;
                Console.Error.WriteLine("Force feedback output reconnected.");
                return true;
            }
            Thread.Sleep(500);
        }
        return false;
    }

    public void Enqueue(ReadOnlySpan<byte> rawG29Report)
    {
        if (_failure != null || rawG29Report.Length < 1) return;

        Interlocked.Increment(ref _received);
        foreach (var report in Libg25.FfbTranslate(rawG29Report, _ffbMode))
            if (!_commands.TryAdd(report)) Interlocked.Increment(ref _dropped);
    }

    private void WriteLoop()
    {
        foreach (var report in _commands.GetConsumingEnumerable())
        {
            try
            {
                _stream.Write(report);
            }
            catch (Exception ex) when (!_stopping)
            {
                if (Reconnect()) continue;
                _failure = ex;
                Console.Error.WriteLine($"Force feedback relay stopped: {ex.Message}");
                return;
            }
        }
    }

    public void Dispose()
    {
        _stopping = true;
        _commands.CompleteAdding();
        _writer.Join(1000);

        try
        {
            _stream.Write(Libg25.StopAll());
            _stream.Write(Libg25.DisableAutocenter());
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"Could not stop G25 effects during shutdown: {ex.Message}");
        }

        _stream.Dispose();
        _commands.Dispose();
        if (_dropped != 0) Console.Error.WriteLine($"Dropped force feedback reports: {_dropped}");
        if (_failure != null) Console.Error.WriteLine("Force feedback relay ended with an I/O error.");
    }
}
