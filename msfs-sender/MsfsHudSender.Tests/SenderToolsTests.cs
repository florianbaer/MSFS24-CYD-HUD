using MsfsHudSender.Protocol;

namespace MsfsHudSender.Tests;

public class PortFinderTests
{
    [Theory]
    [InlineData("VID_1A86&PID_7523", 0x1A86, 0x7523)]
    [InlineData("vid_10c4&pid_ea60", 0x10C4, 0xEA60)]
    [InlineData("VID_1A86&PID_55D4&MI_00", 0x1A86, 0x55D4)]
    public void ParsesHardwareIds(string key, int vid, int pid)
    {
        Assert.True(PortFinder.TryParseHardwareId(key, out var v, out var p));
        Assert.Equal(vid, v);
        Assert.Equal(pid, p);
    }

    [Theory]
    [InlineData("ROOT_HUB30")]
    [InlineData("VID_XYZ&PID_7523")]
    [InlineData("VID_1A86")]
    public void RejectsOtherKeys(string key) =>
        Assert.False(PortFinder.TryParseHardwareId(key, out _, out _));

    [Fact]
    public void KnowsTheCydBridge() =>
        Assert.Contains(PortFinder.KnownBridges, b => b.Vid == 0x1A86 && b.Pid == 0x7523);
}

public class DemoFlightTests
{
    [Theory]
    [InlineData(0.0)]
    [InlineData(12.3)]
    [InlineData(51.0)]
    public void EmitsOneFramePerMessageType(double t)
    {
        var frames = DemoFlight.Frames(t);
        var types = frames.Select(f => Cobs.Decode(f[1..^1])[0]).ToArray();
        Assert.Equal(
            new[] { Messages.MsgAttitude, Messages.MsgEngine, Messages.MsgFlightData, Messages.MsgGForce,
                    Messages.MsgAlerts, Messages.MsgNavData, Messages.MsgConfig, Messages.MsgAutopilot },
            types);
    }

    [Fact]
    public void StallWarningFiresOncePerMinute()
    {
        static ushort Alerts(double t)
        {
            var frame = DemoFlight.Frames(t)[4];
            var raw = Cobs.Decode(frame[1..^1]);
            return BitConverter.ToUInt16(raw, 1);
        }
        Assert.Equal(0, Alerts(10));
        Assert.Equal(Messages.AlertStall, Alerts(51));
    }
}
