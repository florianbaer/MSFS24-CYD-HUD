using System.IO.Ports;
using MsfsHudSender;
using MsfsHudSender.Protocol;
using MsfsHudSender.SimConnect;

// Parse CLI args
bool udpMode = false;
bool demo = false;
bool sdkSimConnect = false;
string? simHost = null;
int simPort = 0;
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
        case "--demo": demo = true; break;
        case "--sdk-simconnect": sdkSimConnect = true; break;
        case "--simconnect" when i + 1 < args.Length:
        {
            var parts = args[++i].Split(':');
            if (parts.Length != 2 || !int.TryParse(parts[1], out simPort) || simPort is < 1 or > 65535)
            {
                Console.Error.WriteLine($"Error: --simconnect needs host:port, got '{args[i]}'");
                return 1;
            }
            simHost = parts[0];
            break;
        }
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
            Console.WriteLine("Usage: msfs-hud-sender [COM_PORT|auto|HOST] [options]");
            Console.WriteLine("  (no target)    Same as 'auto': find the display on USB by itself");
            Console.WriteLine("  --udp          Use UDP transport instead of serial (target = display IP)");
            Console.WriteLine("  --demo         Send a synthetic flight instead of MSFS data (display test)");
            Console.WriteLine("  --baud <rate>  Serial baud rate (default: 115200)");
            Console.WriteLine("  --hz <rate>    Send rate in Hz (default: 20)");
            Console.WriteLine("  --port <port>  UDP port (default: 4242)");
            Console.WriteLine("  --simconnect <host:port>  MSFS on another PC (SimConnect over TCP, see SimConnect.xml)");
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
    if (udpMode)
    {
        Console.Error.WriteLine("Error: --udp needs the display's IP address or host name");
        Console.Error.WriteLine("Run with --help for usage.");
        return 1;
    }
    target = "auto";
}

#if !SIMCONNECT
if (sdkSimConnect)
{
    Console.Error.WriteLine("Error: --sdk-simconnect needs a build with the MSFS SDK (-p:UseSdkSimConnect=true).");
    return 1;
}
#endif

using var cts = new CancellationTokenSource();
Console.CancelKeyPress += (_, e) => { e.Cancel = true; cts.Cancel(); };

// "auto": wait for a known USB-serial bridge (the display) to show up
if (!udpMode && target.Equals("auto", StringComparison.OrdinalIgnoreCase))
{
    if (!OperatingSystem.IsWindows())
    {
        Console.Error.WriteLine("Error: automatic port detection is Windows-only; pass the port name.");
        return 1;
    }
    string? found;
    bool announced = false;
    while ((found = PortFinder.FindDisplayPort()) is null)
    {
        if (!announced)
        {
            Console.WriteLine("Waiting for the display to be plugged in (USB)... (Ctrl+C to stop)");
            announced = true;
        }
        if (cts.Token.WaitHandle.WaitOne(TimeSpan.FromSeconds(2))) return 0;
    }
    target = found;
    Console.WriteLine($"Found display on {target}");
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

var interval = TimeSpan.FromMilliseconds(1000.0 / hz);

if (demo)
{
    Console.WriteLine("Demo mode: sending a synthetic flight. Press Ctrl+C to stop");
    var clock = System.Diagnostics.Stopwatch.StartNew();
    try
    {
        while (!cts.Token.IsCancellationRequested)
        {
            var start = clock.Elapsed;
            foreach (var frame in DemoFlight.Frames(start.TotalSeconds)) write(frame);
            var delay = interval - (clock.Elapsed - start);
            if (delay > TimeSpan.Zero) cts.Token.WaitHandle.WaitOne(delay);
        }
        return 0;
    }
    catch (Exception ex)
    {
        Console.Error.WriteLine($"Error: {ex.Message}");
        return 1;
    }
    finally
    {
        transport.Dispose();
    }
}

// Connect to MSFS. The sender may be launched before the sim is ready
// (e.g. from exe.xml), so keep trying until it answers.
#if SIMCONNECT
IFlightSource source = sdkSimConnect ? new SdkMsfsSource() : new MsfsSource(simHost, simPort);
#else
IFlightSource source = new MsfsSource(simHost, simPort);
#endif
bool announcedWaiting = false;
while (!cts.IsCancellationRequested)
{
    try
    {
        source.Connect();
        break;
    }
    catch (SimConnectUnavailableException)
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
