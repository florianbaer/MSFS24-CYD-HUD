using System.IO.Ports;
using System.Runtime.InteropServices;
using MsfsHudSender;
using MsfsHudSender.Protocol;

// Parse CLI args
bool udpMode = false;
string target = "";
int baud = 115200;
int hz = 20;
int udpPort = 4242;

static int ParseOption(string name, string value, int min, int max)
{
    if (!int.TryParse(value, out var n) || n < min || n > max)
    {
        Console.Error.WriteLine($"Error: {name} must be {min}-{max}, got '{value}'");
        Environment.Exit(1);
    }
    return n;
}

for (int i = 0; i < args.Length; i++)
{
    switch (args[i])
    {
        case "--udp": udpMode = true; break;
        case "--baud" when i + 1 < args.Length:
            baud = ParseOption("baud rate", args[++i], 9600, 921600);
            break;
        case "--hz" when i + 1 < args.Length:
            hz = ParseOption("hz", args[++i], 1, 60);
            break;
        case "--port" when i + 1 < args.Length:
            udpPort = ParseOption("port", args[++i], 1, 65535);
            break;
        case "--help" or "-h":
            Console.WriteLine("Usage: msfs-hud-sender <COM_PORT|HOST> [options]");
            Console.WriteLine("  --udp          Use UDP transport instead of serial");
            Console.WriteLine("  --baud <rate>  Serial baud rate (default: 115200)");
            Console.WriteLine("  --hz <rate>    Send rate in Hz (default: 20)");
            Console.WriteLine("  --port <port>  UDP port (default: 4242)");
            return 0;
        default:
            if (args[i].StartsWith('-'))
            {
                Console.Error.WriteLine($"Error: unknown or incomplete option '{args[i]}'");
                Console.Error.WriteLine("Run with --help for usage.");
                return 1;
            }
            target = args[i];
            break;
    }
}

if (string.IsNullOrEmpty(target))
{
    Console.Error.WriteLine("Error: specify serial port (e.g. COM6) or host IP (with --udp)");
    Console.Error.WriteLine("Run with --help for usage.");
    return 1;
}

if (!MsfsSource.IsSupported)
{
    Console.Error.WriteLine("This build was compiled without the MSFS SDK and cannot talk to the simulator.");
    Console.Error.WriteLine("Rebuild on Windows with the MSFS_SDK environment variable set (see docs/MSFS_PLUGIN.md).");
    return 1;
}

// Set up transport
Action<byte[]> write;
IDisposable transport;

try
{
    if (udpMode)
    {
        var udp = new UdpTransport(target, udpPort);
        write = udp.Write;
        transport = udp;
        Console.WriteLine($"Sending via UDP to {target}:{udpPort} at {hz}Hz");
    }
    else
    {
        var serial = new SerialTransport(target, baud);
        write = serial.Write;
        transport = serial;
        Console.WriteLine($"Sending via serial {target} at {baud} baud, {hz}Hz");
    }
}
catch (Exception ex)
{
    Console.Error.WriteLine($"Could not open {(udpMode ? "UDP target" : "serial port")} '{target}': {ex.Message}");
    if (!udpMode)
        Console.Error.WriteLine($"Available ports: {string.Join(", ", SerialPort.GetPortNames())}");
    return 1;
}

using var cts = new CancellationTokenSource();
Console.CancelKeyPress += (_, e) => { e.Cancel = true; cts.Cancel(); };

// Connect to MSFS. The sender may be launched before the sim is ready
// (e.g. from exe.xml), so keep trying until it answers.
var source = new MsfsSource();
bool announcedWaiting = false;
while (!cts.IsCancellationRequested)
{
    try
    {
        source.Connect();
        break;
    }
    catch (COMException)
    {
        if (!announcedWaiting)
        {
            Console.WriteLine("Waiting for MSFS 2024... (Ctrl+C to stop)");
            announcedWaiting = true;
        }
        cts.Token.WaitHandle.WaitOne(TimeSpan.FromSeconds(2));
    }
    catch (Exception ex)
    {
        Console.Error.WriteLine($"Failed to connect to MSFS SimConnect: {ex.Message}");
        transport.Dispose();
        return 1;
    }
}

if (cts.IsCancellationRequested)
{
    transport.Dispose();
    return 0;
}

Console.WriteLine("Connected to MSFS.");
Console.WriteLine("Sending: attitude, engine, flight, g-force, alerts, nav, config, autopilot");
Console.WriteLine("Press Ctrl+C to stop");

var interval = TimeSpan.FromMilliseconds(1000.0 / hz);
int exitCode = 0;

try
{
    while (!cts.Token.IsCancellationRequested)
    {
        var start = DateTime.UtcNow;

        source.RequestData();
        if (source.SimQuit)
        {
            Console.WriteLine("MSFS has quit.");
            break;
        }

        var (pitch, roll, heading) = source.ReadAttitude();
        write(FrameBuilder.FrameAttitude(pitch, roll, heading));

        for (int i = 0; i < source.EngineCount; i++)
        {
            var (rpm, thr, ff, ot, op) = source.ReadEngine(i);
            write(FrameBuilder.FrameEngine((byte)i, rpm, thr, ff, ot, op));
        }

        var (ias, alt, vs, gs) = source.ReadFlightData();
        write(FrameBuilder.FrameFlightData(ias, alt, vs, gs));

        var (gx, gy, gz) = source.ReadGForce();
        write(FrameBuilder.FrameGForce(gx, gy, gz));

        write(FrameBuilder.FrameAlerts(source.ReadAlerts()));

        var (lat, lon, bug, dist, brg) = source.ReadNavData();
        write(FrameBuilder.FrameNavData(lat, lon, bug, dist, brg));

        var (flaps, gear, eTrim, rTrim) = source.ReadConfig();
        write(FrameBuilder.FrameConfig(flaps, gear, eTrim, rTrim));

        if (source.AutopilotAvailable)
        {
            var (apFlags, apAlt, apHdg) = source.ReadAutopilot();
            write(FrameBuilder.FrameAutopilot(apFlags, apAlt, apHdg));
        }

        var delay = interval - (DateTime.UtcNow - start);
        if (delay > TimeSpan.Zero)
            cts.Token.WaitHandle.WaitOne(delay);
    }
}
catch (Exception ex)
{
    // A failing link (sim crashed, cable unplugged) does not recover on its own;
    // stop instead of logging the same error 20 times a second.
    Console.Error.WriteLine($"Error: {ex.Message}");
    exitCode = 1;
}
finally
{
    Console.WriteLine("\nStopping...");
    source.Dispose();
    transport.Dispose();
}

return exitCode;
