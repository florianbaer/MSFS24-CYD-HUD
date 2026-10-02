using System.Buffers.Binary;
using System.IO.Pipes;
using System.Net;
using System.Net.Sockets;
using MsfsHudSender.Protocol;
using MsfsHudSender.SimConnect;
using static MsfsHudSender.SimConnect.SimConnectProtocol;

namespace MsfsHudSender.Tests;

/// <summary>The built-in client produces and understands the same bytes as node-simconnect.</summary>
public class SimConnectProtocolTests
{
    private static byte[] Hex(string hex) => Convert.FromHexString(hex);

    [Fact]
    public void OpenMatchesNodeSimConnect() =>
        Assert.Equal(Hex(SimConnectGolden.Open), Open("MsfsHudSender", sendId: 0));

    [Fact]
    public void AddToDataDefinitionMatchesNodeSimConnect()
    {
        Assert.Equal(Hex(SimConnectGolden.AddPitch),
            AddToDataDefinition(1, "PLANE PITCH DEGREES", "radians", DataType.Float64, sendId: 1));
        Assert.Equal(Hex(SimConnectGolden.AddRpm),
            AddToDataDefinition(1, "GENERAL ENG RPM:1", "rpm", DataType.Float64, sendId: 2));
    }

    [Fact]
    public void RequestDataOnSimObjectMatchesNodeSimConnect() =>
        Assert.Equal(Hex(SimConnectGolden.Request),
            RequestDataOnSimObject(7, 1, ObjectIdUser, Period.Once, sendId: 3));

    [Fact]
    public void EventPacketsMatchNodeSimConnect()
    {
        Assert.Equal(Hex(SimConnectGolden.MapApMaster), MapClientEventToSimEvent(3, "AP_MASTER", sendId: 4));
        Assert.Equal(Hex(SimConnectGolden.TransmitApMaster), TransmitClientEvent(ObjectIdUser, 3, 0, sendId: 5));
    }

    [Fact]
    public void ParsesOpenLikeNodeSimConnect()
    {
        var msg = Hex(SimConnectGolden.RecvOpen);
        Assert.Equal(RecvOpen, MessageId(msg));
        var open = ParseOpen(msg);
        Assert.Equal("KittyHawk", open.ApplicationName);
        Assert.Equal(11u, open.SimConnectVersionMajor);
    }

    [Fact]
    public void ParsesSimObjectDataLikeNodeSimConnect()
    {
        var msg = Hex(SimConnectGolden.RecvSimObjectData);
        Assert.Equal(RecvSimObjectData, MessageId(msg));
        var data = ParseSimObjectData(msg);
        Assert.Equal(7u, data.RequestId);
        Assert.Equal(1u, data.DefineId);
        Assert.Equal([0.1, -0.25], data.Values);
    }

    [Fact]
    public void ParsesExceptionLikeNodeSimConnect()
    {
        var msg = Hex(SimConnectGolden.RecvException);
        Assert.Equal(RecvException, MessageId(msg));
        Assert.Equal(new ServerException(7, 3, 2), ParseException(msg));
    }

    [Fact]
    public void QuitIsRecognised() => Assert.Equal(RecvQuit, MessageId(Hex(SimConnectGolden.RecvQuit)));

    [Fact]
    public void OverlongNamesAreCutAndStayTerminated()
    {
        var packet = AddToDataDefinition(1, new string('A', 300), "x", DataType.Float64, 0);
        Assert.Equal(544, packet.Length);
        Assert.Equal(0, packet[16 + 4 + 255]);  // last byte of the 256-byte name field
    }

    [Fact]
    public void EverySimVarFitsItsField() =>
        Assert.All(MsfsSource.Definitions.Values.SelectMany(v => v),
            d => Assert.True(d.Var.Length < 256 && d.Units.Length < 256));
}

/// <summary>The named pipe MSFS listens on (Windows only; CI runs this on its Windows runner).</summary>
public class SimConnectPipeTests
{
    [Fact]
    public void ConnectsOverTheMsfsNamedPipe()
    {
        if (!OperatingSystem.IsWindows()) return;  // the pipe transport only exists on Windows

        using var server = new NamedPipeServerStream(SimConnectClient.PipeName, PipeDirection.InOut, 1,
            PipeTransmissionMode.Byte, PipeOptions.Asynchronous);
        var serve = Task.Run(() =>
        {
            server.WaitForConnection();
            var head = new byte[4];
            server.ReadExactly(head);
            var rest = new byte[BinaryPrimitives.ReadUInt32LittleEndian(head) - 4];
            server.ReadExactly(rest);
            server.Write(Convert.FromHexString(SimConnectGolden.RecvOpen));
            server.Flush();
            return BinaryPrimitives.ReadUInt32LittleEndian(rest.AsSpan(4)) & 0xFFFF;  // function id
        });

        using var client = SimConnectClient.Connect("MsfsHudSender");
        Assert.Equal(0x01u, serve.Result);                         // Open came over the pipe
        Assert.Equal("KittyHawk", client.Server?.ApplicationName);  // and the reply went back
    }
}

/// <summary>MsfsSource end to end against a fake simulator speaking SimConnect over TCP.</summary>
public sealed class MsfsSourceTests : IDisposable
{
    private readonly FakeSimulator _sim = new();

    public void Dispose() => _sim.Dispose();

    [Fact]
    public void ReadsAndConvertsFlightData()
    {
        _sim.Values[(uint)MsfsSource.Def.Attitude] = [-0.1, 0.2, Math.PI];     // nose up 5.7°, 11.5° left bank, HDG 180
        _sim.Values[(uint)MsfsSource.Def.FlightData] = [112.4, 4520, 650, 118.7];
        _sim.Values[(uint)MsfsSource.Def.AircraftInfo] = [2];
        _sim.Values[(uint)MsfsSource.Def.Autopilot] = [1, 1, 0, 0, 0, 0, 6000, 270, 1];

        using var source = new MsfsSource("127.0.0.1", _sim.Port);
        source.Connect();
        Assert.Equal("MsfsHudSender", _sim.ClientName);
        Assert.Equal("FakeSim", source.SimulatorName);
        Poll(source, () => source.EngineCount == 2 && source.AutopilotAvailable);
        // Every variable was defined (the fake reads them on its own thread)
        Poll(source, () => Volatile.Read(ref _sim.DefinedVariables) == MsfsSource.Definitions.Values.Sum(v => v.Length), request: false);

        var (pitch, roll, heading) = source.ReadAttitude();
        Assert.Equal(57, pitch);
        Assert.Equal(115, roll);
        Assert.Equal(1800, heading);
        Assert.Equal(((ushort)1124, 4520, (short)650, (ushort)1187), source.ReadFlightData());
        var (flags, alt, hdg) = source.ReadAutopilot();
        Assert.Equal(Protocol.Messages.ApMaster | Protocol.Messages.ApHeadingLock, flags);
        Assert.Equal((6000, (short)2700), (alt, hdg));
        // Engines 3 and 4 are not requested on a twin
        Assert.DoesNotContain((uint)MsfsSource.Def.Engine3, _sim.Requested);
    }

    [Fact]
    public void DisplayControlsBecomeSimEvents()
    {
        using var source = new MsfsSource("127.0.0.1", _sim.Port);
        source.Connect();
        source.SendCommand(HudCommand.ApMaster);
        source.SendCommand(HudCommand.HeadingBugInc);
        Poll(source, () => { lock (_sim.Transmitted) return _sim.Transmitted.Count == 2; }, request: false);
        lock (_sim.Transmitted)
            Assert.Equal(["AP_MASTER", "HEADING_BUG_INC"], _sim.Transmitted);
    }

    [Fact]
    public void ReportsWhenTheSimulatorQuits()
    {
        using var source = new MsfsSource("127.0.0.1", _sim.Port);
        source.Connect();
        Assert.False(source.SimQuit);
        _sim.SendQuit();
        Poll(source, () => source.SimQuit, request: false);
    }

    [Fact]
    public void ALostConnectionCountsAsQuit()
    {
        using var source = new MsfsSource("127.0.0.1", _sim.Port);
        source.Connect();
        _sim.Dispose();  // the simulator goes away without saying Quit
        Poll(source, () => source.SimQuit);  // RequestData must not throw meanwhile
    }

    [Fact]
    public void NoSimulatorMeansUnavailable()
    {
        var port = _sim.Port;
        _sim.Dispose();
        using var source = new MsfsSource("127.0.0.1", port);
        Assert.Throws<SimConnectUnavailableException>(source.Connect);
    }

    private static void Poll(MsfsSource source, Func<bool> done, bool request = true)
    {
        var deadline = DateTime.UtcNow.AddSeconds(5);
        while (!done())
        {
            Assert.True(DateTime.UtcNow < deadline, "timed out");
            if (request) source.RequestData();
            Thread.Sleep(20);
        }
    }

    /// <summary>Answers Open, records definitions and answers each data request with Values.</summary>
    private sealed class FakeSimulator : IDisposable
    {
        private readonly TcpListener _listener = new(IPAddress.Loopback, 0);
        private readonly Thread _thread;
        private NetworkStream? _client;
        private readonly object _write = new();

        public readonly Dictionary<uint, double[]> Values = [];
        public readonly HashSet<uint> Requested = [];
        public readonly Dictionary<uint, string> MappedEvents = [];
        /// <summary>Names of the simulator events fired, in order.</summary>
        public readonly List<string> Transmitted = [];
        public string? ClientName;
        public int DefinedVariables;
        public int Port => ((IPEndPoint)_listener.LocalEndpoint).Port;

        public FakeSimulator()
        {
            _listener.Start();
            _thread = new Thread(Serve) { IsBackground = true };
            _thread.Start();
        }

        private static byte[] Message(uint id, byte[] body)
        {
            var m = new byte[12 + body.Length];
            BinaryPrimitives.WriteUInt32LittleEndian(m, (uint)m.Length);
            BinaryPrimitives.WriteUInt32LittleEndian(m.AsSpan(4), 4);
            BinaryPrimitives.WriteUInt32LittleEndian(m.AsSpan(8), id);
            body.CopyTo(m, 12);
            return m;
        }

        private void Send(byte[] m) { lock (_write) _client?.Write(m); }

        public void SendQuit() => Send(Message(RecvQuit, []));

        private void Serve()
        {
            try
            {
                using var tcp = _listener.AcceptTcpClient();
                _client = tcp.GetStream();
                var head = new byte[4];
                while (true)
                {
                    _client.ReadExactly(head);
                    var packet = new byte[BinaryPrimitives.ReadUInt32LittleEndian(head)];
                    head.CopyTo(packet, 0);
                    _client.ReadExactly(packet.AsSpan(4));
                    var fn = BinaryPrimitives.ReadUInt32LittleEndian(packet.AsSpan(8)) & 0xFFFF;
                    var body = packet.AsSpan(16);
                    switch (fn)
                    {
                        case 0x01:
                            ClientName = System.Text.Encoding.Latin1.GetString(body[..256]).TrimEnd('\0');
                            var open = new byte[256 + 40];
                            System.Text.Encoding.Latin1.GetBytes("FakeSim").CopyTo(open, 0);
                            Send(Message(RecvOpen, open));
                            break;
                        case 0x0C:
                            Interlocked.Increment(ref DefinedVariables);
                            break;
                        case 0x04:
                            MappedEvents[BinaryPrimitives.ReadUInt32LittleEndian(body)] =
                                System.Text.Encoding.Latin1.GetString(body.Slice(4, 256)).TrimEnd('\0');
                            break;
                        case 0x05:
                            var eventId = BinaryPrimitives.ReadUInt32LittleEndian(body[4..]);
                            lock (Transmitted) Transmitted.Add(MappedEvents.GetValueOrDefault(eventId, $"unmapped {eventId}"));
                            break;
                        case 0x0E:
                            var req = BinaryPrimitives.ReadUInt32LittleEndian(body);
                            var def = BinaryPrimitives.ReadUInt32LittleEndian(body[4..]);
                            lock (Requested) Requested.Add(req);
                            var values = Values.TryGetValue(def, out var v) ? v : new double[MsfsSource.Definitions[(MsfsSource.Def)def].Length];
                            var data = new byte[28 + 8 * values.Length];
                            BinaryPrimitives.WriteUInt32LittleEndian(data, req);
                            BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(8), def);
                            BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(24), (uint)values.Length);
                            for (int i = 0; i < values.Length; i++)
                                BinaryPrimitives.WriteDoubleLittleEndian(data.AsSpan(28 + 8 * i), values[i]);
                            Send(Message(RecvSimObjectData, data));
                            break;
                    }
                }
            }
            catch (Exception) { /* client went away or the test ended */ }
        }

        public void Dispose()
        {
            _listener.Stop();
            _client?.Dispose();
        }
    }
}
