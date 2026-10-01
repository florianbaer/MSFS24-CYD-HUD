using System.Net;
using System.Net.Sockets;

namespace MsfsHudSender;

public sealed class UdpTransport : IDisposable
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

    public void Dispose() => _client.Dispose();
}
