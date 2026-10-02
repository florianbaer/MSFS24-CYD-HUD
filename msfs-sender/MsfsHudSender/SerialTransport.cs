using System.IO.Ports;

namespace MsfsHudSender;

public sealed class SerialTransport : ITransport
{
    private readonly SerialPort _port;

    public SerialTransport(string portName, int baudRate = 115200)
    {
        _port = new SerialPort(portName, baudRate);
        _port.Open();
    }

    public void Write(byte[] data) => _port.Write(data, 0, data.Length);

    public byte[] ReadAvailable()
    {
        int n = _port.BytesToRead;
        if (n <= 0) return [];
        var buffer = new byte[n];
        int read = _port.Read(buffer, 0, n);
        return read == n ? buffer : buffer[..read];
    }

    public void Dispose() => _port.Dispose();
}
