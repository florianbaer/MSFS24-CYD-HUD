using MsfsHudSender.SimConnect;

namespace MsfsHudSender;

/// <summary>
/// Reads flight data from MSFS 2020/2024 with the built-in SimConnect client:
/// no MSFS SDK and no Microsoft DLLs needed.
/// </summary>
public sealed class MsfsSource : IFlightSource
{
    /// <summary>Data definitions; each is requested with the same id.</summary>
    internal enum Def : uint
    {
        Attitude = 1, Engine1, Engine2, Engine3, Engine4,
        FlightData, GForce, Alerts, NavData, Config, Autopilot, AircraftInfo,
        EcamEngine1, EcamEngine2, EcamStatus,
    }

    private static (string Var, string Units)[] Engine(int n) =>
    [
        ($"GENERAL ENG RPM:{n}", "rpm"),
        ($"GENERAL ENG THROTTLE LEVER POSITION:{n}", "percent"),
        ($"ENG FUEL FLOW GPH:{n}", "gallons per hour"),
        ($"GENERAL ENG OIL TEMPERATURE:{n}", "rankine"),
        ($"GENERAL ENG OIL PRESSURE:{n}", "psf"),
    ];

    private static (string Var, string Units)[] EcamEngine(int n) =>
    [
        ($"TURB ENG N1:{n}", "percent"),
        ($"TURB ENG N2:{n}", "percent"),
        ($"ENG EXHAUST GAS TEMPERATURE:{n}", "celsius"),
        ($"TURB ENG FUEL FLOW PPH:{n}", "pounds per hour"),
    ];

    /// <summary>Simulation variables per definition, in the order the values arrive.</summary>
    internal static readonly IReadOnlyDictionary<Def, (string Var, string Units)[]> Definitions =
        new Dictionary<Def, (string, string)[]>
        {
            [Def.Attitude] =
            [
                ("PLANE PITCH DEGREES", "radians"),
                ("PLANE BANK DEGREES", "radians"),
                ("PLANE HEADING DEGREES MAGNETIC", "radians"),
            ],
            [Def.Engine1] = Engine(1),
            [Def.Engine2] = Engine(2),
            [Def.Engine3] = Engine(3),
            [Def.Engine4] = Engine(4),
            [Def.FlightData] =
            [
                ("AIRSPEED INDICATED", "knots"),
                ("INDICATED ALTITUDE", "feet"),
                ("VERTICAL SPEED", "feet per minute"),
                ("GROUND VELOCITY", "knots"),
            ],
            // Load factor for the vertical axis (1.0 in level flight), body
            // accelerations for the other two (body X = lateral, body Z = longitudinal)
            [Def.GForce] =
            [
                ("G FORCE", "GForce"),
                ("ACCELERATION BODY X", "feet per second squared"),
                ("ACCELERATION BODY Z", "feet per second squared"),
            ],
            [Def.Alerts] =
            [
                ("STALL WARNING", "bool"),
                ("OVERSPEED WARNING", "bool"),
                ("ENG ON FIRE:1", "bool"),
            ],
            [Def.NavData] =
            [
                ("PLANE LATITUDE", "radians"),
                ("PLANE LONGITUDE", "radians"),
                ("AUTOPILOT HEADING LOCK DIR", "degrees"),
                ("GPS WP DISTANCE", "meters"),
                ("GPS WP BEARING", "degrees"),
            ],
            [Def.Config] =
            [
                ("FLAPS HANDLE PERCENT", "percent"),
                ("GEAR HANDLE POSITION", "bool"),
                ("GEAR TOTAL PCT EXTENDED", "percent over 100"),
                ("ELEVATOR TRIM POSITION", "radians"),
                ("RUDDER TRIM PCT", "percent"),
            ],
            [Def.Autopilot] =
            [
                ("AUTOPILOT MASTER", "bool"),
                ("AUTOPILOT HEADING LOCK", "bool"),
                ("AUTOPILOT ALTITUDE LOCK", "bool"),
                ("AUTOPILOT VERTICAL HOLD", "bool"),
                ("AUTOPILOT NAV1 LOCK", "bool"),
                ("AUTOPILOT APPROACH HOLD", "bool"),
                ("AUTOPILOT ALTITUDE LOCK VAR", "feet"),
                ("AUTOPILOT HEADING LOCK DIR", "degrees"),
                ("AUTOPILOT AVAILABLE", "bool"),
            ],
            [Def.AircraftInfo] = [("NUMBER OF ENGINES", "number")],
            // ECAM: kept in their own definitions so an aircraft that lacks one
            // of these variables does not disturb the others
            [Def.EcamEngine1] = EcamEngine(1),
            [Def.EcamEngine2] = EcamEngine(2),
            [Def.EcamStatus] =
            [
                ("FUEL TOTAL QUANTITY WEIGHT", "pounds"),
                ("FLAPS HANDLE INDEX", "number"),
                ("LEADING EDGE FLAPS LEFT PERCENT", "percent"),
                ("TRAILING EDGE FLAPS LEFT PERCENT", "percent"),
                ("BRAKE PARKING POSITION", "bool"),
                ("SPOILERS HANDLE POSITION", "percent"),
                ("SPOILERS ARMED", "bool"),
                ("CABIN SEATBELTS ALERT SWITCH", "bool"),
                ("APU PCT RPM", "percent"),
                ("ENG ANTI ICE:1", "bool"),
                ("LIGHT LANDING", "bool"),
            ],
        };

    private readonly string? _host;
    private readonly int _port;
    private SimConnectClient? _sc;
    // Latest values per definition; the receive thread swaps whole arrays
    private readonly double[]?[] _latest = new double[]?[(int)Def.EcamStatus + 1];
    private volatile bool _simQuit;

    /// <param name="host">Remote simulator (SimConnect over TCP); null for the local one.</param>
    public MsfsSource(string? host = null, int port = 0)
    {
        _host = host;
        _port = port;
    }

    public int EngineCount => Math.Clamp((int)Value(Def.AircraftInfo, 0, 1), 1, 4);
    public bool AutopilotAvailable => Value(Def.Autopilot, 8) > 0.5;
    public bool SimQuit => _simQuit;
    /// <summary>Simulator version reported in the handshake, e.g. "KittyHawk".</summary>
    public string? SimulatorName => _sc?.Server?.ApplicationName;

    public void Connect()
    {
        var sc = SimConnectClient.Connect("MsfsHudSender", _host, _port);
        sc.DataReceived += d =>
        {
            if (d.RequestId >= 1 && d.RequestId < _latest.Length) _latest[d.RequestId] = d.Values;
        };
        sc.ExceptionReceived += e =>
            Console.Error.WriteLine($"SimConnect rejected request {e.SendId} (exception {e.Exception}, parameter {e.Index})");
        sc.Closed += () => _simQuit = true;
        foreach (var (def, vars) in Definitions)
            foreach (var (name, units) in vars)
                sc.AddToDataDefinition((uint)def, name, units);
        // Client event id = command id
        foreach (var (command, simEvent) in SimEvents.ForCommand)
            sc.MapClientEventToSimEvent((uint)command, simEvent);
        _sc = sc;
    }

    public void RequestData()
    {
        if (_sc is null || _simQuit) return;
        try
        {
            foreach (var def in Definitions.Keys)
            {
                bool unusedEngine = (def is >= Def.Engine1 and <= Def.Engine4 && def - Def.Engine1 >= EngineCount)
                                || (def == Def.EcamEngine2 && EngineCount < 2);
                if (!unusedEngine)
                    _sc.RequestDataOnSimObject((uint)def, (uint)def, SimConnectProtocol.Period.Once);
            }
        }
        catch (Exception ex) when (ex is IOException or ObjectDisposedException)
        {
            // The simulator closed the connection (it may not have sent Quit first)
            _simQuit = true;
        }
    }

    public (ushort n1, ushort n2, short egt, ushort ff) ReadEcamEngine(int idx)
    {
        var v = Values(idx == 0 ? Def.EcamEngine1 : Def.EcamEngine2);
        return Conversions.ConvertEcamEngine(v[0], v[1], v[2], v[3]);
    }

    public (uint fobKg, byte flapsIndex, byte slatsPct, byte flapsPct, ushort memo) ReadEcamStatus()
    {
        var v = Values(Def.EcamStatus);
        var (fob, idx, slats, flaps) = Conversions.ConvertEcamStatus(v[0], v[1], v[2], v[3]);
        var memo = Conversions.BuildMemoFlags(
            parkBrake: v[4] > 0.5, speedBrake: v[5] > 1, spoilersArmed: v[6] > 0.5,
            seatBelts: v[7] > 0.5, apuAvail: v[8] > 95, engAntiIce: v[9] > 0.5, landingLights: v[10] > 0.5);
        return (fob, idx, slats, flaps, memo);
    }

    public void SendCommand(Protocol.HudCommand command)
    {
        if (_sc is null || _simQuit) return;
        try
        {
            _sc.TransmitClientEvent((uint)command);
        }
        catch (Exception ex) when (ex is IOException or ObjectDisposedException)
        {
            _simQuit = true;
        }
    }

    private double Value(Def def, int index, double fallback = 0)
    {
        var values = _latest[(int)def];
        return values is not null && index < values.Length ? values[index] : fallback;
    }

    private double[] Values(Def def) => _latest[(int)def] ?? new double[Definitions[def].Length];

    public (short pitch, short roll, short heading) ReadAttitude()
    {
        var v = Values(Def.Attitude);
        return Conversions.ConvertAttitude(v[0], v[1], v[2]);
    }

    public (ushort rpm, byte throttle, byte ff, byte ot, byte op) ReadEngine(int idx)
    {
        var v = Values((Def)((uint)Def.Engine1 + (uint)idx));
        return Conversions.ConvertEngine(v[0], v[1], v[2], v[3], v[4]);
    }

    public (ushort ias, int alt, short vs, ushort gs) ReadFlightData()
    {
        var v = Values(Def.FlightData);
        return Conversions.ConvertFlightData(v[0], v[1], v[2], v[3]);
    }

    public (short gx, short gy, short gz) ReadGForce()
    {
        var v = Values(Def.GForce);
        return Conversions.ConvertGForce(v[0], v[1], v[2]);
    }

    public ushort ReadAlerts()
    {
        var a = Values(Def.Alerts);
        var c = Values(Def.Config);
        bool gearUnsafe = c[1] > 0.5 && c[2] < 0.99;
        return Conversions.BuildAlertFlags(a[0] > 0.5, a[1] > 0.5, gearUnsafe, false, a[2] > 0.5, false);
    }

    public (int lat, int lon, short bug, ushort dist, short brg) ReadNavData()
    {
        var v = Values(Def.NavData);
        return Conversions.ConvertNavData(v[0], v[1], v[2], v[3], v[4]);
    }

    public (byte flaps, byte gear, sbyte eTrim, sbyte rTrim) ReadConfig()
    {
        var v = Values(Def.Config);
        return Conversions.ConvertConfig(v[0], v[1], v[2], v[3], v[4]);
    }

    public (ushort flags, int alt, short hdg) ReadAutopilot()
    {
        var v = Values(Def.Autopilot);
        var flags = Conversions.BuildApFlags(v[0] > 0.5, v[1] > 0.5, v[2] > 0.5, v[3] > 0.5, v[4] > 0.5, v[5] > 0.5);
        var (alt, hdg) = Conversions.ConvertAutopilotTargets(v[6], v[7]);
        return (flags, alt, hdg);
    }

    public void Dispose() => _sc?.Dispose();
}
