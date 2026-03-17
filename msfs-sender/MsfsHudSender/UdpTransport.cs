using System.Net;
using System.Net.Sockets;

namespace MsfsHudSender;

public sealed class UdpTransport : IDisposable
{
    private readonly UdpClient _client;
    private readonly IPEndPoint _endpoint;

    public UdpTransport(string host, int port = 4242)
    {
        _client = new UdpClient();
        _endpoint = new IPEndPoint(IPAddress.Parse(host), port);
    }

    public void Write(byte[] data) => _client.Send(data, data.Length, _endpoint);

    public void Dispose() => _client.Dispose();
}
