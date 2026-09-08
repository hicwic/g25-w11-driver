// SPDX-License-Identifier: GPL-2.0-only
using System.IO.Pipes;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Text;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;

namespace G25VirtualG29Service;

/// <summary>
/// Serves the current <see cref="BridgeStatus"/> as newline-delimited JSON on
/// <c>\\.\pipe\g25vg29</c> - one line on connect, then one per change.
/// Readable by any authenticated user so the unprivileged tray can consume it.
/// </summary>
sealed class StatusServer(BridgeStatus status, ILogger<StatusServer> log) : BackgroundService
{
    public const string PipeName = "g25vg29";

    protected override async Task ExecuteAsync(CancellationToken stopping)
    {
        while (!stopping.IsCancellationRequested)
        {
            try
            {
                using var server = CreatePipe();
                await server.WaitForConnectionAsync(stopping);
                await ServeClient(server, stopping);
            }
            catch (OperationCanceledException) { }
            catch (Exception ex)
            {
                log.LogWarning(ex, "status pipe error");
                try { await Task.Delay(1000, stopping); } catch { }
            }
        }
    }

    private static NamedPipeServerStream CreatePipe()
    {
        var security = new PipeSecurity();
        security.AddAccessRule(new PipeAccessRule(
            new SecurityIdentifier(WellKnownSidType.AuthenticatedUserSid, null),
            PipeAccessRights.Read | PipeAccessRights.Synchronize, AccessControlType.Allow));
        security.AddAccessRule(new PipeAccessRule(
            new SecurityIdentifier(WellKnownSidType.LocalSystemSid, null),
            PipeAccessRights.FullControl, AccessControlType.Allow));

        return NamedPipeServerStreamAcl.Create(
            PipeName, PipeDirection.Out, 4,
            PipeTransmissionMode.Byte, PipeOptions.Asynchronous,
            0, 0, security);
    }

    private async Task ServeClient(NamedPipeServerStream server, CancellationToken stopping)
    {
        var pending = new SemaphoreSlim(1);
        var latest = status.Json;

        void OnChange(string json) { latest = json; try { pending.Release(); } catch { } }
        status.Changed += OnChange;
        try
        {
            await Write(server, latest, stopping);
            while (!stopping.IsCancellationRequested && server.IsConnected)
            {
                await pending.WaitAsync(stopping);
                await Write(server, latest, stopping);
            }
        }
        catch (OperationCanceledException) { }
        catch (IOException) { /* client went away */ }
        finally { status.Changed -= OnChange; }
    }

    private static async Task Write(NamedPipeServerStream server, string json, CancellationToken ct)
    {
        var bytes = Encoding.UTF8.GetBytes(json + "\n");
        await server.WriteAsync(bytes, ct);
        await server.FlushAsync(ct);
    }
}
