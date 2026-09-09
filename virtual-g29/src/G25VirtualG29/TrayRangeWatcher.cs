// SPDX-License-Identifier: GPL-2.0-only
using System.Runtime.InteropServices;
using Microsoft.Win32;
using Microsoft.Win32.SafeHandles;

namespace G25VirtualG29;

/// <summary>
/// Watches <c>HKEY_USERS\{sid}\Software\g25-driver</c> for the tray's
/// <c>Rotation</c> value and calls back when it changes, so the running bridge
/// can apply a new steering range without a restart.
/// </summary>
/// <remarks>
/// The worker runs as SYSTEM, so it cannot use <c>HKEY_CURRENT_USER</c>; the
/// supervising service passes the interactive user's SID via <c>--user-sid</c>.
/// A single background thread blocks in <c>RegNotifyChangeKeyValue</c>; there is
/// no polling.
/// </remarks>
sealed class TrayRangeWatcher : IDisposable
{
    private const int REG_NOTIFY_CHANGE_LAST_SET = 0x00000004;

    [DllImport("advapi32.dll", SetLastError = true)]
    private static extern int RegNotifyChangeKeyValue(
        SafeRegistryHandle hKey, bool bWatchSubtree, int dwNotifyFilter,
        SafeWaitHandle hEvent, bool fAsynchronous);

    private readonly Thread _thread;

    private TrayRangeWatcher(string sid, int initialDegrees, Action<int> onChange, CancellationToken token)
    {
        _thread = new Thread(() => Run(sid, initialDegrees, onChange, token))
        {
            IsBackground = true,
            Name = "tray wheel-range watcher",
        };
        _thread.Start();
    }

    public static TrayRangeWatcher Start(string sid, int initialDegrees, Action<int> onChange, CancellationToken token)
        => new(sid, initialDegrees, onChange, token);

    private static void Run(string sid, int lastApplied, Action<int> onChange, CancellationToken token)
    {
        var subKey = $@"{sid}\Software\g25-driver";
        using var signal = new ManualResetEventSlim(false);
        using var reg = token.Register(signal.Set);

        while (!token.IsCancellationRequested)
        {
            RegistryKey? key = null;
            try
            {
                using var users = RegistryKey.OpenBaseKey(RegistryHive.Users, RegistryView.Default);
                key = users.OpenSubKey(subKey);
                if (key == null)
                {
                    // Tray has not written the key yet; retry unless we are stopping.
                    if (token.WaitHandle.WaitOne(2000)) return;
                    continue;
                }

                using var changed = new ManualResetEvent(false);
                while (!token.IsCancellationRequested)
                {
                    changed.Reset();
                    var rc = RegNotifyChangeKeyValue(
                        key.Handle, false, REG_NOTIFY_CHANGE_LAST_SET, changed.SafeWaitHandle, true);
                    if (rc != 0)
                    {
                        Console.Error.WriteLine($"tray range watcher: RegNotifyChangeKeyValue failed ({rc}); reopening.");
                        break;
                    }

                    if (WaitHandle.WaitAny([changed, token.WaitHandle]) == 1) return;

                    if (ReadRotation(key) is int deg && deg != lastApplied)
                    {
                        lastApplied = deg;
                        try { onChange(deg); }
                        catch (Exception ex) { Console.Error.WriteLine($"tray range watcher: apply failed: {ex.Message}"); }
                    }
                }
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"tray range watcher: {ex.Message}");
                if (token.WaitHandle.WaitOne(5000)) return;
            }
            finally
            {
                key?.Dispose();
            }
        }
    }

    private static int? ReadRotation(RegistryKey key)
        => key.GetValue("Rotation") is int r && r is 180 or 360 or 540 or 900 ? r : null;

    public void Dispose() => _thread.Join(1000);
}
