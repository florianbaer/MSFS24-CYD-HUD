#if SIMCONNECT
using Microsoft.FlightSimulator.SimConnect;
using System.Runtime.InteropServices;

namespace MsfsHudSender;

/// <summary>
/// Reads flight data through Microsoft's managed SimConnect API from the MSFS SDK.
/// Only built with -p:UseSdkSimConnect=true; the default is the SDK-free
/// <see cref="MsfsSource"/>. Run with --sdk-simconnect to use this one.
/// </summary>
public sealed class SdkMsfsSource : IFlightSource
{
    private enum DataDef
    {
        Attitude, Engine1, Engine2, Engine3, Engine4,
        FlightData, GForce, Alerts, NavData, Config, Autopilot, AircraftInfo
    }

    private enum RequestId
    {
        Attitude, Engine1, Engine2, Engine3, Engine4,
        FlightData, GForce, Alerts, NavData, Config, Autopilot, AircraftInfo
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi, Pack = 1)]
    private struct AttitudeData { public double pitch, roll, heading; }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi, Pack = 1)]
    private struct EngineData { public double rpm, throttle, fuelFlow, oilTemp, oilPress; }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi, Pack = 1)]
    private struct FlightDataStruct { public double ias, altitude, vspeed, gs; }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi, Pack = 1)]
    private struct GForceData { public double gForce, accelBodyX, accelBodyZ; }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi, Pack = 1)]
    private struct AlertData { public double stallWarning, overspeedWarning, engOnFire; }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi, Pack = 1)]
    private struct NavDataStruct { public double lat, lon, hdgBug, wpDist, wpBearing; }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi, Pack = 1)]
    private struct ConfigData { public double flaps, gearHandle, gearExtended, elevTrim, rudderTrim; }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi, Pack = 1)]
    private struct AutopilotData
    {
        public double master, hdgLock, altLock, vsLock, navLock, aprLock;
        public double altVar, hdgDir, available;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi, Pack = 1)]
    private struct AircraftInfoData { public double numEngines; }

    private SimConnect? _sc;
    private AttitudeData _attitude;
    private EngineData[] _engines = new EngineData[4];
    private FlightDataStruct _flight;
    private GForceData _gforce;
    private AlertData _alerts;
    private NavDataStruct _nav;
    private ConfigData _config;
    private AutopilotData _ap;
    private int _engineCount = 1;
    private bool _apAvailable;
    private bool _simQuit;

    public int EngineCount => _engineCount;
    public bool AutopilotAvailable => _apAvailable;
    /// <summary>True once MSFS has told us it is shutting down.</summary>
    public bool SimQuit => _simQuit;

    public void Connect()
    {
        try
        {
            _sc = new SimConnect("MsfsHudSender", IntPtr.Zero, 0, null, 0);
        }
        catch (COMException ex)
        {
            throw new SimConnect.SimConnectUnavailableException("MSFS is not running.", ex);
        }
        RegisterDataDefinitions();
        _sc.OnRecvSimobjectData += OnRecvData;
        _sc.OnRecvQuit += (_, _) => _simQuit = true;
    }

    private void RegisterDataDefinitions()
    {
        if (_sc == null) return;

        // Attitude
        _sc.AddToDataDefinition(DataDef.Attitude, "PLANE PITCH DEGREES", "radians",
            SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
        _sc.AddToDataDefinition(DataDef.Attitude, "PLANE BANK DEGREES", "radians",
            SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
        _sc.AddToDataDefinition(DataDef.Attitude, "PLANE HEADING DEGREES MAGNETIC", "radians",
            SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
        _sc.RegisterDataDefineStruct<AttitudeData>(DataDef.Attitude);

        // Engine (register for each engine index)
        for (int i = 0; i < 4; i++)
        {
            var def = (DataDef)((int)DataDef.Engine1 + i);
            var idx = i + 1;
            _sc.AddToDataDefinition(def, $"GENERAL ENG RPM:{idx}", "rpm",
                SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
            _sc.AddToDataDefinition(def, $"GENERAL ENG THROTTLE LEVER POSITION:{idx}", "percent",
                SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
            _sc.AddToDataDefinition(def, $"ENG FUEL FLOW GPH:{idx}", "gallons per hour",
                SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
            _sc.AddToDataDefinition(def, $"GENERAL ENG OIL TEMPERATURE:{idx}", "rankine",
                SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
            _sc.AddToDataDefinition(def, $"GENERAL ENG OIL PRESSURE:{idx}", "psf",
                SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
            _sc.RegisterDataDefineStruct<EngineData>(def);
        }

        // Flight data
        _sc.AddToDataDefinition(DataDef.FlightData, "AIRSPEED INDICATED", "knots",
            SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
        _sc.AddToDataDefinition(DataDef.FlightData, "INDICATED ALTITUDE", "feet",
            SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
        _sc.AddToDataDefinition(DataDef.FlightData, "VERTICAL SPEED", "feet per minute",
            SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
        _sc.AddToDataDefinition(DataDef.FlightData, "GROUND VELOCITY", "knots",
            SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
        _sc.RegisterDataDefineStruct<FlightDataStruct>(DataDef.FlightData);

        // G-force: load factor for the vertical axis (1.0 in level flight), body
        // accelerations for the other two (body X = lateral, body Z = longitudinal)
        _sc.AddToDataDefinition(DataDef.GForce, "G FORCE", "GForce",
            SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
        _sc.AddToDataDefinition(DataDef.GForce, "ACCELERATION BODY X", "feet per second squared",
            SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
        _sc.AddToDataDefinition(DataDef.GForce, "ACCELERATION BODY Z", "feet per second squared",
            SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
        _sc.RegisterDataDefineStruct<GForceData>(DataDef.GForce);

        // Alerts
        _sc.AddToDataDefinition(DataDef.Alerts, "STALL WARNING", "bool",
            SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
        _sc.AddToDataDefinition(DataDef.Alerts, "OVERSPEED WARNING", "bool",
            SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
        _sc.AddToDataDefinition(DataDef.Alerts, "ENG ON FIRE:1", "bool",
            SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
        _sc.RegisterDataDefineStruct<AlertData>(DataDef.Alerts);

        // Nav data
        _sc.AddToDataDefinition(DataDef.NavData, "PLANE LATITUDE", "radians",
            SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
        _sc.AddToDataDefinition(DataDef.NavData, "PLANE LONGITUDE", "radians",
            SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
        _sc.AddToDataDefinition(DataDef.NavData, "AUTOPILOT HEADING LOCK DIR", "degrees",
            SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
        _sc.AddToDataDefinition(DataDef.NavData, "GPS WP DISTANCE", "meters",
            SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
        _sc.AddToDataDefinition(DataDef.NavData, "GPS WP BEARING", "degrees",
            SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
        _sc.RegisterDataDefineStruct<NavDataStruct>(DataDef.NavData);

        // Config
        _sc.AddToDataDefinition(DataDef.Config, "FLAPS HANDLE PERCENT", "percent",
            SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
        _sc.AddToDataDefinition(DataDef.Config, "GEAR HANDLE POSITION", "bool",
            SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
        _sc.AddToDataDefinition(DataDef.Config, "GEAR TOTAL PCT EXTENDED", "percent over 100",
            SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
        _sc.AddToDataDefinition(DataDef.Config, "ELEVATOR TRIM POSITION", "radians",
            SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
        _sc.AddToDataDefinition(DataDef.Config, "RUDDER TRIM PCT", "percent",
            SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
        _sc.RegisterDataDefineStruct<ConfigData>(DataDef.Config);

        // Autopilot
        _sc.AddToDataDefinition(DataDef.Autopilot, "AUTOPILOT MASTER", "bool",
            SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
        _sc.AddToDataDefinition(DataDef.Autopilot, "AUTOPILOT HEADING LOCK", "bool",
            SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
        _sc.AddToDataDefinition(DataDef.Autopilot, "AUTOPILOT ALTITUDE LOCK", "bool",
            SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
        _sc.AddToDataDefinition(DataDef.Autopilot, "AUTOPILOT VERTICAL HOLD", "bool",
            SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
        _sc.AddToDataDefinition(DataDef.Autopilot, "AUTOPILOT NAV1 LOCK", "bool",
            SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
        _sc.AddToDataDefinition(DataDef.Autopilot, "AUTOPILOT APPROACH HOLD", "bool",
            SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
        _sc.AddToDataDefinition(DataDef.Autopilot, "AUTOPILOT ALTITUDE LOCK VAR", "feet",
            SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
        _sc.AddToDataDefinition(DataDef.Autopilot, "AUTOPILOT HEADING LOCK DIR", "degrees",
            SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
        _sc.AddToDataDefinition(DataDef.Autopilot, "AUTOPILOT AVAILABLE", "bool",
            SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
        _sc.RegisterDataDefineStruct<AutopilotData>(DataDef.Autopilot);

        // Aircraft info (engine count)
        _sc.AddToDataDefinition(DataDef.AircraftInfo, "NUMBER OF ENGINES", "number",
            SIMCONNECT_DATATYPE.FLOAT64, 0, SimConnect.SIMCONNECT_UNUSED);
        _sc.RegisterDataDefineStruct<AircraftInfoData>(DataDef.AircraftInfo);
    }

    public void RequestData()
    {
        if (_sc == null) return;
        _sc.RequestDataOnSimObject(RequestId.Attitude, DataDef.Attitude,
            SimConnect.SIMCONNECT_OBJECT_ID_USER, SIMCONNECT_PERIOD.ONCE, 0, 0, 0, 0);
        for (int i = 0; i < _engineCount; i++)
        {
            var req = (RequestId)((int)RequestId.Engine1 + i);
            var def = (DataDef)((int)DataDef.Engine1 + i);
            _sc.RequestDataOnSimObject(req, def,
                SimConnect.SIMCONNECT_OBJECT_ID_USER, SIMCONNECT_PERIOD.ONCE, 0, 0, 0, 0);
        }
        _sc.RequestDataOnSimObject(RequestId.FlightData, DataDef.FlightData,
            SimConnect.SIMCONNECT_OBJECT_ID_USER, SIMCONNECT_PERIOD.ONCE, 0, 0, 0, 0);
        _sc.RequestDataOnSimObject(RequestId.GForce, DataDef.GForce,
            SimConnect.SIMCONNECT_OBJECT_ID_USER, SIMCONNECT_PERIOD.ONCE, 0, 0, 0, 0);
        _sc.RequestDataOnSimObject(RequestId.Alerts, DataDef.Alerts,
            SimConnect.SIMCONNECT_OBJECT_ID_USER, SIMCONNECT_PERIOD.ONCE, 0, 0, 0, 0);
        _sc.RequestDataOnSimObject(RequestId.NavData, DataDef.NavData,
            SimConnect.SIMCONNECT_OBJECT_ID_USER, SIMCONNECT_PERIOD.ONCE, 0, 0, 0, 0);
        _sc.RequestDataOnSimObject(RequestId.Config, DataDef.Config,
            SimConnect.SIMCONNECT_OBJECT_ID_USER, SIMCONNECT_PERIOD.ONCE, 0, 0, 0, 0);
        _sc.RequestDataOnSimObject(RequestId.Autopilot, DataDef.Autopilot,
            SimConnect.SIMCONNECT_OBJECT_ID_USER, SIMCONNECT_PERIOD.ONCE, 0, 0, 0, 0);
        _sc.RequestDataOnSimObject(RequestId.AircraftInfo, DataDef.AircraftInfo,
            SimConnect.SIMCONNECT_OBJECT_ID_USER, SIMCONNECT_PERIOD.ONCE, 0, 0, 0, 0);
        _sc.ReceiveMessage();
    }

    private void OnRecvData(SimConnect sender, SIMCONNECT_RECV_SIMOBJECT_DATA data)
    {
        switch ((RequestId)data.dwRequestID)
        {
            case RequestId.Attitude:
                _attitude = (AttitudeData)data.dwData[0]; break;
            case RequestId.Engine1:
                _engines[0] = (EngineData)data.dwData[0]; break;
            case RequestId.Engine2:
                _engines[1] = (EngineData)data.dwData[0]; break;
            case RequestId.Engine3:
                _engines[2] = (EngineData)data.dwData[0]; break;
            case RequestId.Engine4:
                _engines[3] = (EngineData)data.dwData[0]; break;
            case RequestId.FlightData:
                _flight = (FlightDataStruct)data.dwData[0]; break;
            case RequestId.GForce:
                _gforce = (GForceData)data.dwData[0]; break;
            case RequestId.Alerts:
                _alerts = (AlertData)data.dwData[0]; break;
            case RequestId.NavData:
                _nav = (NavDataStruct)data.dwData[0]; break;
            case RequestId.Config:
                _config = (ConfigData)data.dwData[0]; break;
            case RequestId.Autopilot:
                _ap = (AutopilotData)data.dwData[0];
                _apAvailable = _ap.available > 0.5;
                break;
            case RequestId.AircraftInfo:
                var info = (AircraftInfoData)data.dwData[0];
                _engineCount = Math.Clamp((int)info.numEngines, 1, 4);
                break;
        }
    }

    public (short pitch, short roll, short heading) ReadAttitude() =>
        Conversions.ConvertAttitude(_attitude.pitch, _attitude.roll, _attitude.heading);

    public (ushort rpm, byte throttle, byte ff, byte ot, byte op) ReadEngine(int idx) =>
        Conversions.ConvertEngine(
            _engines[idx].rpm, _engines[idx].throttle, _engines[idx].fuelFlow,
            _engines[idx].oilTemp, _engines[idx].oilPress);

    public (ushort ias, int alt, short vs, ushort gs) ReadFlightData() =>
        Conversions.ConvertFlightData(_flight.ias, _flight.altitude, _flight.vspeed, _flight.gs);

    public (short gx, short gy, short gz) ReadGForce() =>
        Conversions.ConvertGForce(_gforce.gForce, _gforce.accelBodyX, _gforce.accelBodyZ);

    public ushort ReadAlerts()
    {
        bool gearUnsafe = _config.gearHandle > 0.5 && _config.gearExtended < 0.99;
        return Conversions.BuildAlertFlags(
            _alerts.stallWarning > 0.5, _alerts.overspeedWarning > 0.5,
            gearUnsafe, false, _alerts.engOnFire > 0.5, false);
    }

    public (int lat, int lon, short bug, ushort dist, short brg) ReadNavData() =>
        Conversions.ConvertNavData(_nav.lat, _nav.lon, _nav.hdgBug, _nav.wpDist, _nav.wpBearing);

    public (byte flaps, byte gear, sbyte eTrim, sbyte rTrim) ReadConfig() =>
        Conversions.ConvertConfig(
            _config.flaps, _config.gearHandle, _config.gearExtended,
            _config.elevTrim, _config.rudderTrim);

    public (ushort flags, int alt, short hdg) ReadAutopilot()
    {
        var flags = Conversions.BuildApFlags(
            _ap.master > 0.5, _ap.hdgLock > 0.5, _ap.altLock > 0.5,
            _ap.vsLock > 0.5, _ap.navLock > 0.5, _ap.aprLock > 0.5);
        var (alt, hdg) = Conversions.ConvertAutopilotTargets(_ap.altVar, _ap.hdgDir);
        return (flags, alt, hdg);
    }

    private bool _warnedCommands;

    /// <summary>Display controls are only wired up for the built-in SimConnect client.</summary>
    public void SendCommand(Protocol.HudCommand command)
    {
        if (_warnedCommands) return;
        _warnedCommands = true;
        Console.Error.WriteLine("Display controls are not supported with --sdk-simconnect; ignoring them.");
    }

    public void Dispose() => _sc?.Dispose();
}
#endif
