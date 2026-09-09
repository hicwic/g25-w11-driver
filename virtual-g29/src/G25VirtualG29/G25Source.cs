// SPDX-License-Identifier: GPL-2.0-only
using HidSharp;

namespace G25VirtualG29;

/// <summary>One decoded input frame from the physical G25 in native mode.</summary>
sealed record G25Frame(float Wheel, float Accelerator, float Brake, float Clutch, uint Buttons, int Pov);

/// <summary>
/// Reads the physical G25 (native USB id <c>046D:C299</c>) directly through its HID
/// input report on a background thread. A dedicated thread keeps the latest frame
/// available without blocking the submit loop.
/// </summary>
/// <remarks>
/// The report layout is derived from the G25 native descriptor and mirrors the
/// decoder in the g25-driver project. Keep the two in sync; the long term plan is
/// to share a single protocol library (see docs/plugin-integration.md).
/// </remarks>
sealed class G25Source : IDisposable
{
    private const int LogitechVendorId = 0x046D;
    // Native-mode product ids: G25 = C299, G27 = C29B. Same 12/8-byte report
    // shape; the button field differs, so the decoder needs the model.
    private static readonly (int pid, Libg25.Model model)[] NativeWheels =
        { (0xC299, Libg25.Model.G25), (0xC29B, Libg25.Model.G27) };

    // How long ReadLoop keeps trying to re-acquire the G25 after an I/O error
    // before giving up. Startup USB re-enumeration is handled separately by
    // Open(); a mid-run gap this long means the wheel was really unplugged.
    private static readonly TimeSpan ReconnectWindow = TimeSpan.FromSeconds(15);

    private HidStream _stream;
    private readonly Libg25.Model _model;
    private readonly byte[] _report = new byte[12];
    private readonly ManualResetEventSlim _firstFrame = new(false);
    // Pulsed on every decoded frame so the submit loop runs at the wheel's own
    // report rate (~200 Hz) instead of polling a cached value on a fixed timer.
    private readonly AutoResetEvent _frameReady = new(false);
    private readonly Thread _reader;
    private G25Frame? _latest;
    private Exception? _failure;
    private volatile bool _stopping;

    private G25Source(HidStream stream, string name, Libg25.Model model)
    {
        _stream = stream;
        _model = model;
        Name = name;
        _reader = new Thread(ReadLoop) { IsBackground = true, Name = "G25 HID input reader" };
        _reader.Start();
    }

    public string Name { get; }
    public Libg25.Model Model => _model;

    private static (HidStream stream, Libg25.Model model)? TryAcquireStream()
    {
        foreach (var (pid, model) in NativeWheels)
        {
            foreach (var device in DeviceList.Local.GetHidDevices(LogitechVendorId, pid))
            {
                if (device.GetMaxInputReportLength() != 12 || device.GetMaxOutputReportLength() != 8)
                    continue;
                if (device.TryOpen(out var stream))
                {
                    stream.ReadTimeout = 100;
                    return (stream, model);
                }
            }
        }
        return null;
    }

    public static G25Source Open(TimeSpan? waitForDevice = null)
    {
        var timeout = waitForDevice ?? TimeSpan.Zero;
        var deadline = DateTime.UtcNow + timeout;
        do
        {
            if (TryAcquireStream() is { } got)
            {
                string name;
                try { name = got.stream.Device.GetProductName(); }
                catch { name = got.model == Libg25.Model.G27 ? "Logitech G27" : "Logitech G25"; }
                return new G25Source(got.stream, name, got.model);
            }
            if (DateTime.UtcNow < deadline) Thread.Sleep(250);
        } while (DateTime.UtcNow < deadline);

        throw new InvalidOperationException(
            "No native physical G25/G27 HID input found (expected 046D:C299 or 046D:C29B with 12-byte input "
            + "and 8-byte output reports). If Logitech G HUB is running it may have taken the wheel into "
            + "compatibility mode; switch it back to native mode (g25tool native, or replug).");
    }

    // Re-acquire the G25 after a USB re-enumeration. Returns false if the device
    // does not come back on native 046D:C299 within ReconnectWindow.
    private bool Reconnect()
    {
        // Parsed by the service to tell "hidden by HidHide" from "unplugged".
        Console.WriteLine("WHEEL: lost");
        var deadline = DateTime.UtcNow + ReconnectWindow;
        while (!_stopping && DateTime.UtcNow < deadline)
        {
            if (TryAcquireStream() is { } got)
            {
                try { _stream.Dispose(); } catch { }
                _stream = got.stream;
                Console.WriteLine("WHEEL: reconnected");
                return true;
            }
            Thread.Sleep(500);
        }
        Console.WriteLine("WHEEL: gone");
        return false;
    }

    /// <summary>
    /// Blocks until the reader thread decodes a fresh frame, or <paramref name="timeoutMs"/>
    /// elapses (then it returns the last frame again, so the submit loop keeps the
    /// virtual wheel fed during a brief stall). Throws once the reader has given up.
    /// </summary>
    public G25Frame WaitFrame(int timeoutMs)
    {
        if (!_firstFrame.IsSet && !_firstFrame.Wait(TimeSpan.FromSeconds(2)))
            throw new TimeoutException("No input report received from the physical G25 within two seconds.");
        _frameReady.WaitOne(timeoutMs);
        if (_failure != null) throw new IOException("Physical G25 input reader stopped.", _failure);
        return Volatile.Read(ref _latest)!;
    }

    private void ReadLoop()
    {
        while (!_stopping)
        {
            try
            {
                var count = _stream.Read(_report, 0, _report.Length);
                if (count != _report.Length) continue;

                var decoded = Libg25.DecodeInput(_report, _model);
                if (decoded is not { } s) continue;
                Volatile.Write(ref _latest, new G25Frame(
                    s.Wheel / 16383f,
                    s.Throttle / 255f,
                    s.Brake / 255f,
                    s.Clutch / 255f,
                    s.Buttons,
                    s.Hat <= 7 ? s.Hat * 4500 : -1));
                _firstFrame.Set();
                _frameReady.Set();
            }
            catch (TimeoutException)
            {
                // A short timeout lets Dispose stop this thread promptly.
            }
            catch (Exception ex) when (!_stopping)
            {
                if (Reconnect()) continue;
                _failure = ex;
                _firstFrame.Set();
                _frameReady.Set();
                return;
            }
            catch
            {
                // Stopping: Dispose is tearing the stream and events down under
                // us. Letting this escape would kill the process, because the
                // filter above deliberately does not catch it.
                return;
            }
        }
    }

    public void Dispose()
    {
        _stopping = true;
        _frameReady.Set();   // wake a waiting submit loop so it can see _stopping
        var stopped = _reader.Join(500);
        _stream.Dispose();
        // The reader signals these; only release them once it has really left.
        if (stopped) { _firstFrame.Dispose(); _frameReady.Dispose(); }
    }
}
