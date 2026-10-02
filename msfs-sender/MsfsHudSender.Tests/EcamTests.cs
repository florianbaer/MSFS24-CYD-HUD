using MsfsHudSender.Protocol;

namespace MsfsHudSender.Tests;

/// <summary>ECAM messages (tests/proto/decoder_test.cpp decodes the same bytes).</summary>
public class EcamTests
{
    [Fact]
    public void EngineFrameMatchesTheFirmware() =>
        Assert.Equal(Convert.FromHexString("000c0a013803a70367028004a100"),
            FrameBuilder.FrameEcamEngine(1, 824, 935, 615, 1152));

    [Fact]
    public void StatusFrameMatchesTheFirmware() =>
        Assert.Equal(Convert.FromHexString("00040b60180105024b1e4902b300"),
            FrameBuilder.FrameEcamStatus(6240, 2, 75, 30,
                Messages.MemoParkBrake | Messages.MemoSeatBelts | Messages.MemoLandingLights));

    [Fact]
    public void EngineValuesAreConverted()
    {
        // 82.44 % N1, 93.46 % N2, 614.6 °C, 2540 lb/h
        var (n1, n2, egt, ff) = Conversions.ConvertEcamEngine(82.44, 93.46, 614.6, 2540);
        Assert.Equal((ushort)824, n1);
        Assert.Equal((ushort)935, n2);
        Assert.Equal((short)615, egt);
        Assert.Equal((ushort)1152, ff);   // kg/h
    }

    [Fact]
    public void EngineValuesAreClamped()
    {
        var (n1, _, egt, ff) = Conversions.ConvertEcamEngine(-3, 0, -500, -10);
        Assert.Equal((0, (short)-100, 0), (n1, egt, ff));
    }

    [Fact]
    public void StatusValuesAreConverted()
    {
        var (fob, idx, slats, flaps) = Conversions.ConvertEcamStatus(13757, 2.0, 74.6, 130);
        Assert.Equal(6240u, fob);          // kg
        Assert.Equal((byte)2, idx);
        Assert.Equal((byte)75, slats);
        Assert.Equal((byte)100, flaps);    // clamped
    }

    [Fact]
    public void MemoFlags() =>
        Assert.Equal(Messages.MemoSpeedBrake | Messages.MemoApuAvail,
            Conversions.BuildMemoFlags(false, true, false, false, true, false, false));
}
