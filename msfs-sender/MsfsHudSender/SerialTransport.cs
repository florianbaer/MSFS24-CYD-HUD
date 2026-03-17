using System.IO.Ports;

namespace MsfsHudSender;

public sealed class SerialTransport : IDisposable
{
    private readonly SerialPort _port;

    public SerialTransport(string portName, int baudRate = 115200)
    {
        _port = new SerialPort(portName, baudRate);
        _port.Open();
    }

    public void Write(byte[] data) => _port.Write(data, 0, data.Length);

    public void Dispose() => _port.Dispose();
}
