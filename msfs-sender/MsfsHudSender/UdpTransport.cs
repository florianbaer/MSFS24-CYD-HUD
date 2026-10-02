using System.Net;
using System.Net.Sockets;

namespace MsfsHudSender;

public sealed class UdpTransport : ITransport
{
    private readonly UdpClient _client;
    private readonly IPEndPoint _endpoint;

    public UdpTransport(string host, int port = 4242)
    {
        // Accept both IP addresses and host names; the ESP32 only speaks IPv4
        var address = Dns.GetHostAddresses(host)
            .FirstOrDefault(a => a.AddressFamily == AddressFamily.InterNetwork)
            ?? throw new ArgumentException($"'{host}' does not resolve to an IPv4 address");
        _client = new UdpClient(AddressFamily.InterNetwork);
        _endpoint = new IPEndPoint(address, port);
    }

    public void Write(byte[] data) => _client.Send(data, data.Length, _endpoint);

    /// <summary>Command frames the display sends back to the port we send from.</summary>
    public byte[] ReadAvailable()
    {
        var received = new List<byte>();
        try
        {
            while (_client.Available > 0)
            {
                IPEndPoint? from = null;
                var datagram = _client.Receive(ref from);
                if (from.Address.Equals(_endpoint.Address)) received.AddRange(datagram);
            }
        }
        catch (SocketException)
        {
            // e.g. "port unreachable" from an earlier send while the display was offline
        }
        return [.. received];
    }

    public void Dispose() => _client.Dispose();
}
