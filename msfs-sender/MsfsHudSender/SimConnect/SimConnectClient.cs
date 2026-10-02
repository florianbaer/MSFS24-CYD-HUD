using System.Buffers.Binary;
using System.IO.Pipes;
using System.Net.Sockets;
using Microsoft.Win32;

namespace MsfsHudSender.SimConnect;

/// <summary>The simulator is not running (or not accepting SimConnect clients) yet.</summary>
public sealed class SimConnectUnavailableException(string message, Exception? inner = null)
    : Exception(message, inner);

/// <summary>
/// Minimal SimConnect client over the simulator's named pipe (or TCP), with no
/// dependency on the MSFS SDK. Requests are sent from the caller's thread;
/// replies are read on a background thread and raised as events.
/// </summary>
public sealed class SimConnectClient : IDisposable
{
    /// <summary>The pipe MSFS 2020 and 2024 listen on for local SimConnect clients.</summary>
    public const string PipeName = @"Microsoft Flight Simulator\SimConnect";

    private readonly Stream _stream;
    private readonly IDisposable? _owner;
    private readonly object _writeLock = new();
    private uint _sendId;
    private Thread? _reader;
    private volatile bool _disposed;

    public event Action<SimObjectData>? DataReceived;
    public event Action<ServerException>? ExceptionReceived;
    /// <summary>The simulator is shutting down, or the connection was lost.</summary>
    public event Action? Closed;

    public OpenInfo? Server { get; private set; }

    internal SimConnectClient(Stream stream, IDisposable? owner = null)
    {
        _stream = stream;
        _owner = owner;
    }

    /// <summary>
    /// Connects to a local simulator (named pipe, then the TCP port MSFS
    /// registers) or to <paramref name="host"/>:<paramref name="port"/>, and
    /// performs the Open handshake.
    /// </summary>
    /// <exception cref="SimConnectUnavailableException">No simulator answered.</exception>
    public static SimConnectClient Connect(string appName, string? host = null, int port = 0,
        TimeSpan? timeout = null)
    {
        var wait = timeout ?? TimeSpan.FromSeconds(5);
        SimConnectClient client = host is not null ? ConnectTcp(host, port) : ConnectLocal();
        try
        {
            client.Open(appName, wait);
            client.StartReceiving();
            return client;
        }
        catch
        {
            client.Dispose();
            throw;
        }
    }

    private static SimConnectClient ConnectLocal()
    {
        if (!OperatingSystem.IsWindows())
            throw new SimConnectUnavailableException("A local simulator needs Windows; use --simconnect host:port.");

        var pipe = new NamedPipeClientStream(".", PipeName, PipeDirection.InOut, PipeOptions.Asynchronous);
        try
        {
            pipe.Connect(500);
            return new SimConnectClient(pipe);
        }
        catch (Exception ex) when (ex is TimeoutException or IOException)
        {
            pipe.Dispose();
        }

        // Older setups only listen on TCP; MSFS writes the port to the registry
        if (Registry.GetValue(@"HKEY_CURRENT_USER\Software\Microsoft\Microsoft Games\Flight Simulator",
                "SimConnect_Port_IPv4", null) is string portText && int.TryParse(portText, out var port))
            return ConnectTcp("127.0.0.1", port);

        throw new SimConnectUnavailableException("MSFS is not running.");
    }

    private static SimConnectClient ConnectTcp(string host, int port)
    {
        var tcp = new TcpClient { NoDelay = true };
        try
        {
            tcp.Connect(host, port);
            return new SimConnectClient(tcp.GetStream(), tcp);
        }
        catch (SocketException ex)
        {
            tcp.Dispose();
            throw new SimConnectUnavailableException($"Nothing answers on {host}:{port}.", ex);
        }
    }

    /// <summary>Sends Open and waits for the simulator's reply.</summary>
    internal void Open(string appName, TimeSpan timeout)
    {
        Send(id => SimConnectProtocol.Open(appName, id));
        var read = Task.Run(() =>
        {
            while (true)
            {
                var message = ReadMessage() ?? throw new SimConnectUnavailableException("The simulator closed the connection.");
                if (SimConnectProtocol.MessageId(message) == SimConnectProtocol.RecvOpen)
                    return SimConnectProtocol.ParseOpen(message);
            }
        });
        try
        {
            if (!read.Wait(timeout))
            {
                _stream.Dispose();  // unblocks the read
                throw new SimConnectUnavailableException("The simulator did not answer the SimConnect handshake.");
            }
        }
        catch (AggregateException ex) when (ex.InnerException is SimConnectUnavailableException inner)
        {
            throw inner;
        }
        catch (AggregateException ex) when (ex.InnerException is IOException or ObjectDisposedException)
        {
            throw new SimConnectUnavailableException("The SimConnect connection failed.", ex.InnerException);
        }
        Server = read.Result;
    }

    internal void StartReceiving()
    {
        _reader = new Thread(ReceiveLoop) { IsBackground = true, Name = "SimConnect receive" };
        _reader.Start();
    }

    public void AddToDataDefinition(uint defineId, string datumName, string? unitsName,
        SimConnectProtocol.DataType type = SimConnectProtocol.DataType.Float64) =>
        Send(id => SimConnectProtocol.AddToDataDefinition(defineId, datumName, unitsName, type, id));

    public void RequestDataOnSimObject(uint requestId, uint defineId, SimConnectProtocol.Period period,
        uint objectId = SimConnectProtocol.ObjectIdUser) =>
        Send(id => SimConnectProtocol.RequestDataOnSimObject(requestId, defineId, objectId, period, id));

    public void MapClientEventToSimEvent(uint eventId, string eventName) =>
        Send(id => SimConnectProtocol.MapClientEventToSimEvent(eventId, eventName, id));

    public void TransmitClientEvent(uint eventId, uint data = 0, uint objectId = SimConnectProtocol.ObjectIdUser) =>
        Send(id => SimConnectProtocol.TransmitClientEvent(objectId, eventId, data, id));

    private void Send(Func<uint, byte[]> build)
    {
        lock (_writeLock)
        {
            var packet = build(++_sendId);
            _stream.Write(packet);
            _stream.Flush();
        }
    }

    /// <summary>One complete server message, or null at the end of the stream.</summary>
    private byte[]? ReadMessage()
    {
        var header = new byte[4];
        if (!ReadExactly(header)) return null;
        int size = (int)BinaryPrimitives.ReadUInt32LittleEndian(header);
        if (size < SimConnectProtocol.ServerHeaderSize || size > SimConnectProtocol.MaxMessageSize)
            throw new InvalidDataException($"Bad SimConnect message size {size}");
        var message = new byte[size];
        header.CopyTo(message, 0);
        return ReadExactly(message.AsSpan(4)) ? message : null;
    }

    private bool ReadExactly(Span<byte> buffer)
    {
        int done = 0;
        while (done < buffer.Length)
        {
            int n = _stream.Read(buffer[done..]);
            if (n == 0) return false;
            done += n;
        }
        return true;
    }

    private void ReceiveLoop()
    {
        try
        {
            while (!_disposed)
            {
                var message = ReadMessage();
                if (message is null) break;
                switch (SimConnectProtocol.MessageId(message))
                {
                    case SimConnectProtocol.RecvSimObjectData:
                        DataReceived?.Invoke(SimConnectProtocol.ParseSimObjectData(message));
                        break;
                    case SimConnectProtocol.RecvException:
                        ExceptionReceived?.Invoke(SimConnectProtocol.ParseException(message));
                        break;
                    case SimConnectProtocol.RecvQuit:
                        Closed?.Invoke();
                        return;
                }
            }
        }
        catch (Exception) when (_disposed)
        {
            return;  // closing the stream ends the blocking read
        }
        catch (Exception ex) when (ex is IOException or InvalidDataException or ObjectDisposedException)
        {
            // connection lost: reported as Closed below
        }
        if (!_disposed) Closed?.Invoke();
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _stream.Dispose();
        _owner?.Dispose();
    }
}
