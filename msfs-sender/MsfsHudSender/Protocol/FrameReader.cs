namespace MsfsHudSender.Protocol;

/// <summary>A decoded frame: message type and payload.</summary>
public readonly record struct Frame(byte Type, byte[] Payload);

/// <summary>
/// Splits a byte stream into frames ([0x00][COBS(type|payload|crc)][0x00]).
/// Anything between frames that does not decode with a valid CRC is dropped,
/// so the display's text log on the same serial port is simply skipped.
/// </summary>
public sealed class FrameReader
{
    private const int MaxFrame = 256;
    private readonly List<byte> _buffer = new(MaxFrame);
    private bool _overflow;

    public IEnumerable<Frame> Feed(ReadOnlySpan<byte> bytes)
    {
        var frames = new List<Frame>();
        foreach (var b in bytes)
        {
            if (b != 0x00)
            {
                if (_buffer.Count < MaxFrame) _buffer.Add(b);
                else _overflow = true;
                continue;
            }
            if (_buffer.Count > 0 && !_overflow && TryDecode(_buffer.ToArray(), out var frame))
                frames.Add(frame);
            _buffer.Clear();
            _overflow = false;
        }
        return frames;
    }

    private static bool TryDecode(byte[] encoded, out Frame frame)
    {
        frame = default;
        byte[] raw;
        try
        {
            raw = Cobs.Decode(encoded);
        }
        catch (InvalidOperationException)
        {
            return false;
        }
        if (raw.Length < 2 || Crc8.Compute(raw.AsSpan(0, raw.Length - 1)) != raw[^1]) return false;
        frame = new Frame(raw[0], raw[1..^1]);
        return true;
    }
}
