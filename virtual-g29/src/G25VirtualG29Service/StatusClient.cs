// SPDX-License-Identifier: GPL-2.0-only
using System.IO.Pipes;

namespace G25VirtualG29Service;

/// <summary>`g25vg29 status` - connect to the pipe, print one JSON line.</summary>
static class StatusClient
{
    public static async Task<int> PrintOnce()
    {
        try
        {
            using var pipe = new NamedPipeClientStream(".", StatusServer.PipeName, PipeDirection.In);
            await pipe.ConnectAsync(2000);
            using var reader = new StreamReader(pipe);
            var line = await reader.ReadLineAsync();
            Console.WriteLine(line ?? "{\"state\":\"unknown\"}");
            return 0;
        }
        catch (TimeoutException)
        {
            Console.WriteLine("{\"state\":\"stopped\"}");   // service not running
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine(ex.Message);
            return 1;
        }
    }
}
