// SPDX-License-Identifier: GPL-2.0-only
using G25GfnBridgeService;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;

// g25gfnbridge  -  on-demand Windows service that supervises the GeForce NOW
// bridge worker. The unprivileged tray drives it through the SCM (start/stop/
// query) and reads richer status from the \\.\pipe\g25gfnbridge pipe.

var verb = args.Length > 0 ? args[0].ToLowerInvariant() : "run";

switch (verb)
{
    case "install":
        return ServiceInstall.Install();
    case "uninstall":
        return ServiceInstall.Uninstall();
    case "status":
        return await StatusClient.PrintOnce();
    case "run":
        break;
    default:
        Console.Error.WriteLine("usage: g25gfnbridge [install|uninstall|status|run]");
        return 2;
}

var builder = Host.CreateApplicationBuilder(args);
builder.Services.AddWindowsService(o => o.ServiceName = ServiceInstall.ServiceName);
builder.Services.AddSingleton<BridgeStatus>();
builder.Services.AddHostedService<StatusServer>();
builder.Services.AddHostedService<BridgeWorker>();
builder.Logging.AddEventLog(o => o.SourceName = ServiceInstall.ServiceName);

await builder.Build().RunAsync();
return 0;
