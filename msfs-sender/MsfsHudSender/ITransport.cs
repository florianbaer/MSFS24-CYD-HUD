namespace MsfsHudSender;

/// <summary>Link to the display: frames out, command frames back.</summary>
public interface ITransport : IDisposable
{
    void Write(byte[] data);

    /// <summary>Whatever the display sent since the last call (never blocks; may be empty).</summary>
    byte[] ReadAvailable();
}
