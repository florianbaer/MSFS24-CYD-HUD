using MsfsHudSender.Protocol;

namespace MsfsHudSender.Tests;

public class Crc8Tests
{
    [Fact]
    public void Empty_ReturnsZero() =>
        Assert.Equal(0x00, Crc8.Compute([]));

    [Fact]
    public void ZeroByte_ReturnsZero() =>
        Assert.Equal(0x00, Crc8.Compute([0x00]));

    [Fact]
    public void KnownValue_0x01() =>
        Assert.Equal(0x31, Crc8.Compute([0x01]));

    [Fact]
    public void KnownValue_0xFF() =>
        Assert.Equal(0xAC, Crc8.Compute([0xFF]));

    [Fact]
    public void DifferentData_DifferentCrc() =>
        Assert.NotEqual(Crc8.Compute([0x01]), Crc8.Compute([0x02]));

    [Fact]
    public void Deterministic()
    {
        byte[] data = [0x01, 0x02, 0x03, 0xFF, 0x80];
        Assert.Equal(Crc8.Compute(data), Crc8.Compute(data));
    }
}
