namespace MsfsHudSender;

/// <summary>Flight data in wire units, read once per send cycle.</summary>
public interface IFlightSource : IDisposable
{
    int EngineCount { get; }
    bool AutopilotAvailable { get; }
    /// <summary>True once the simulator has quit or the connection is gone.</summary>
    bool SimQuit { get; }

    /// <exception cref="SimConnect.SimConnectUnavailableException">The simulator is not running yet.</exception>
    void Connect();
    /// <summary>Asks for fresh values; they arrive in the background.</summary>
    void RequestData();

    (short pitch, short roll, short heading) ReadAttitude();
    (ushort rpm, byte throttle, byte ff, byte ot, byte op) ReadEngine(int idx);
    (ushort ias, int alt, short vs, ushort gs) ReadFlightData();
    (short gx, short gy, short gz) ReadGForce();
    ushort ReadAlerts();
    (int lat, int lon, short bug, ushort dist, short brg) ReadNavData();
    (byte flaps, byte gear, sbyte eTrim, sbyte rTrim) ReadConfig();
    (ushort flags, int alt, short hdg) ReadAutopilot();
}
