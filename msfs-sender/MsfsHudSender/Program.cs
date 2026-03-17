using MsfsHudSender;
using MsfsHudSender.Protocol;

// Parse CLI args
bool udpMode = false;
string target = "";
int baud = 115200;
int hz = 20;
int udpPort = 4242;

for (int i = 0; i < args.Length; i++)
{
    switch (args[i])
    {
        case "--udp": udpMode = true; break;
        case "--baud" when i + 1 < args.Length:
            baud = int.Parse(args[++i]);
            if (baud < 9600 || baud > 921600)
            {
                Console.Error.WriteLine($"Error: baud rate must be 9600-921600, got {baud}");
                Environment.Exit(1);
            }
            break;
        case "--hz" when i + 1 < args.Length:
            hz = int.Parse(args[++i]);
            if (hz < 1 || hz > 60)
            {
                Console.Error.WriteLine($"Error: hz must be 1-60, got {hz}");
                Environment.Exit(1);
            }
            break;
        case "--port" when i + 1 < args.Length:
            udpPort = int.Parse(args[++i]);
            if (udpPort < 1 || udpPort > 65535)
            {
                Console.Error.WriteLine($"Error: port must be 1-65535, got {udpPort}");
                Environment.Exit(1);
            }
            break;
        case "--help" or "-h":
            Console.WriteLine("Usage: msfs-hud-sender <COM_PORT|HOST> [options]");
            Console.WriteLine("  --udp          Use UDP transport instead of serial");
            Console.WriteLine("  --baud <rate>  Serial baud rate (default: 115200)");
            Console.WriteLine("  --hz <rate>    Send rate in Hz (default: 20)");
            Console.WriteLine("  --port <port>  UDP port (default: 4242)");
            return;
        default:
            if (!args[i].StartsWith('-')) target = args[i];
            break;
    }
}

if (string.IsNullOrEmpty(target))
{
    Console.Error.WriteLine("Error: specify serial port (e.g. COM6) or host IP (with --udp)");
    Console.Error.WriteLine("Run with --help for usage.");
    Environment.Exit(1);
}

// Set up transport
Action<byte[]> write;
IDisposable transport;

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

// Connect to MSFS
var source = new MsfsSource();
try
{
    source.Connect();
}
catch (Exception ex)
{
    Console.Error.WriteLine($"Failed to connect to MSFS SimConnect: {ex.Message}");
    Console.Error.WriteLine("Make sure MSFS 2024 is running and you are in a flight.");
    transport.Dispose();
    Environment.Exit(1);
}

Console.WriteLine($"Connected. Engines: {source.EngineCount}, AP: {(source.AutopilotAvailable ? "yes" : "no")}");
Console.WriteLine("Sending: attitude, engine, flight, g-force, alerts, nav, config, autopilot");
Console.WriteLine("Press Ctrl+C to stop");

var interval = TimeSpan.FromMilliseconds(1000.0 / hz);
var cts = new CancellationTokenSource();
Console.CancelKeyPress += (_, e) => { e.Cancel = true; cts.Cancel(); };

try
{
    while (!cts.Token.IsCancellationRequested)
    {
        var start = DateTime.UtcNow;

        try
        {
            source.RequestData();

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
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"Error: {ex}");
        }

        var elapsed = DateTime.UtcNow - start;
        var delay = interval - elapsed;
        if (delay > TimeSpan.Zero)
            Thread.Sleep(delay);
    }
}
finally
{
    Console.WriteLine("\nStopping...");
    source.Dispose();
    transport.Dispose();
}
