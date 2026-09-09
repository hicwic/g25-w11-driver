// SPDX-License-Identifier: GPL-2.0-only
using System.IO.Pipes;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Text;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;

namespace G25VirtualG29Service;

/// <summary>
/// Serves the current <see cref="BridgeStatus"/> as one JSON line on
/// <c>\\.\pipe\g25vg29</c>, then hangs up. Readable by any authenticated user so
/// the unprivileged tray can consume it.
/// </summary>
/// <remarks>
/// Both consumers poll one line and disconnect (the tray's <c>vg29::status()</c>,
/// and <c>g25vg29 status</c>). An earlier version kept the connection open to
/// stream every change, which meant it could not tell a client had hung up until
/// the next status change - up to a second later - while that dead connection
/// held the only pipe instance. Polls landing in that window silently got
/// nothing. Serving a snapshot per connection removes the failure mode; several
/// accept loops run in parallel so an instance is always ready.
/// </remarks>
sealed class StatusServer(BridgeStatus status, ILogger<StatusServer> log) : BackgroundService
{
    public const string PipeName = "g25vg29";
    private const int AcceptLoops = 3;   // must stay <= the pipe's instance limit

    protected override Task ExecuteAsync(CancellationToken stopping) =>
        Task.WhenAll(Enumerable.Range(0, AcceptLoops).Select(_ => AcceptLoop(stopping)));

    private async Task AcceptLoop(CancellationToken stopping)
    {
        while (!stopping.IsCancellationRequested)
        {
            try
            {
                // Disposing the stream (rather than Disconnect(), which would
                // discard unread bytes) lets the client drain the line it asked
                // for and then see end-of-file.
                using var server = CreatePipe();
                await server.WaitForConnectionAsync(stopping);
                await Write(server, status.Json, stopping);
            }
            catch (OperationCanceledException) { }
            catch (IOException) { /* client hung up before reading */ }
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
            PipeName, PipeDirection.Out, AcceptLoops,
            PipeTransmissionMode.Byte, PipeOptions.Asynchronous,
            0, 0, security);
    }

    private static async Task Write(NamedPipeServerStream server, string json, CancellationToken ct)
    {
        var bytes = Encoding.UTF8.GetBytes(json + "\n");
        await server.WriteAsync(bytes, ct);
        await server.FlushAsync(ct);
    }
}
