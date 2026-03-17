using MsfsHudSender.Protocol;

namespace MsfsHudSender.Tests;

public class CobsTests
{
    [Fact]
    public void Roundtrip_NoZeros()
    {
        byte[] data = [0x01, 0x02, 0x03];
        Assert.Equal(data, Cobs.Decode(Cobs.Encode(data)));
    }

    [Fact]
    public void Roundtrip_WithZeros()
    {
        byte[] data = [0x00, 0x01, 0x00, 0x02];
        Assert.Equal(data, Cobs.Decode(Cobs.Encode(data)));
    }

    [Fact]
    public void Roundtrip_AllZeros()
    {
        byte[] data = [0x00, 0x00, 0x00];
        Assert.Equal(data, Cobs.Decode(Cobs.Encode(data)));
    }

    [Fact]
    public void Roundtrip_Empty()
    {
        byte[] data = [];
        Assert.Equal(data, Cobs.Decode(Cobs.Encode(data)));
    }

    [Fact]
    public void Roundtrip_SingleByte() =>
        Assert.Equal(new byte[] { 0x42 }, Cobs.Decode(Cobs.Encode([0x42])));

    [Fact]
    public void Roundtrip_SingleZero() =>
        Assert.Equal(new byte[] { 0x00 }, Cobs.Decode(Cobs.Encode([0x00])));

    [Fact]
    public void NoZerosInEncoded()
    {
        byte[] data = [0x00, 0x01, 0x00, 0x02, 0x00];
        var encoded = Cobs.Encode(data);
        Assert.DoesNotContain((byte)0x00, encoded);
    }

    [Fact]
    public void Roundtrip_254ByteBoundary()
    {
        // 254 non-zero bytes: COBS block boundary
        var data = new byte[254];
        for (int i = 0; i < 254; i++) data[i] = (byte)(i + 1);
        var encoded = Cobs.Encode(data);
        Assert.DoesNotContain((byte)0x00, encoded);
        Assert.Equal(data, Cobs.Decode(encoded));
    }

    [Fact]
    public void Roundtrip_255PlusBytes()
    {
        // Crosses COBS block boundary
        var data = new byte[258];
        for (int i = 0; i < 258; i++) data[i] = (byte)((i % 254) + 1);
        var encoded = Cobs.Encode(data);
        Assert.DoesNotContain((byte)0x00, encoded);
        Assert.Equal(data, Cobs.Decode(encoded));
    }

    [Fact]
    public void Decode_Truncated_Throws() =>
        Assert.Throws<InvalidOperationException>(() => Cobs.Decode([0x05, 0x01, 0x02]));

    [Fact]
    public void Decode_ZeroInData_Throws() =>
        Assert.Throws<InvalidOperationException>(() => Cobs.Decode([0x00]));
}
