// SPDX-License-Identifier: GPL-2.0-only
using System.Collections.Concurrent;
using HidSharp;

namespace G25GfnWheelBridge;

/// <summary>
/// Forwards force-feedback output reports received by the virtual G29 to the
/// physical G25 output endpoint.
/// </summary>
/// <remarks>
/// KNOWN LIMITATION: this is currently a near-raw passthrough. It copies the
/// first seven bytes of every output report straight to the G25 8-byte output
/// report. It assumes the bytes the virtual G29 receives are already in the
/// Logitech "classic" wheel command format that the G25 understands.
///
/// That holds only partly:
///  - the G29 in native mode uses an extended command set that differs from the
///    G25/G27 classic set (constant-force scaling, spring/damper parameters,
///    autocenter);
///  - no effect-slot management, scaling or rate limiting is done here.
///
/// The real translation table is tracked in docs/ffb-protocol.md. The Linux
/// kernel driver hid-lg4ff.c is the reference for the per-model differences.
/// </remarks>
sealed class G25ForceFeedbackRelay : IDisposable
{
    private const int LogitechVendorId = 0x046D;
    private const int G25NativeProductId = 0xC299;
    private static readonly byte[] StopAll = [0, 0xF3, 0, 0, 0, 0, 0, 0];
    private static readonly byte[] DisableAutocenter = [0, 0xF5, 0, 0, 0, 0, 0, 0];

    private static readonly TimeSpan ReconnectWindow = TimeSpan.FromSeconds(30);

    private HidStream _stream;
    private readonly BlockingCollection<byte[]> _commands = new(512);
    private readonly Thread _writer;
    private int _dropped;
    private volatile bool _stopping;
    private Exception? _failure;

    private G25ForceFeedbackRelay(HidStream stream, string productName)
    {
        _stream = stream;
        _writer = new Thread(WriteLoop) { IsBackground = true, Name = "G25 force-feedback relay" };
        _writer.Start();
        Console.WriteLine($"Force feedback output opened: {productName}");
    }

    // G25 SET_RANGE (classic), same encoding lg4ff uses for G25/G27/G29:
    // f8 81 <range lo> <range hi>. 900 deg -> 0x0384.
    private static byte[] SetRange(int degrees)
    {
        var r = Math.Clamp(degrees, 40, 900);
        return [0, 0xF8, 0x81, (byte)(r & 0xFF), (byte)((r >> 8) & 0xFF), 0, 0, 0];
    }

    /// <summary>
    /// Bring the physical G25 to a known state: stop forces, disable autocenter,
    /// set rotation range. Without G HUB running, nothing else does this.
    /// </summary>
    public void SendWheelInit(int rangeDegrees)
    {
        _commands.TryAdd((byte[])StopAll.Clone());
        _commands.TryAdd((byte[])DisableAutocenter.Clone());
        if (rangeDegrees > 0)
        {
            _commands.TryAdd(SetRange(rangeDegrees));
            Console.WriteLine($"Wheel init: stop forces, autocenter off, range {Math.Clamp(rangeDegrees, 40, 900)} deg.");
        }
        else
        {
            Console.WriteLine("Wheel init: stop forces, autocenter off.");
        }
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

    public static G25ForceFeedbackRelay Open()
    {
        var stream = TryAcquireStream();
        if (stream == null)
            throw new InvalidOperationException(
                "The physical G25 HID force-feedback output was not found (expected 046D:C299 with an 8-byte output report).");

        string name;
        try { name = stream.Device.GetProductName(); }
        catch { name = "Logitech G25"; }
        return new G25ForceFeedbackRelay(stream, name);
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
        if (_failure != null || rawG29Report.Length < 7) return;

        var report = new byte[8];
        rawG29Report[..7].CopyTo(report.AsSpan(1));
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
            _stream.Write(StopAll);
            _stream.Write(DisableAutocenter);
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
