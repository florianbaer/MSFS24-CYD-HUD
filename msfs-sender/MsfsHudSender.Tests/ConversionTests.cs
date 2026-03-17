namespace MsfsHudSender.Tests;

public class ConversionTests
{
    // --- Attitude ---

    [Fact]
    public void Attitude_ZeroInputs()
    {
        var (p, r, h) = Conversions.ConvertAttitude(0, 0, 0);
        Assert.Equal(0, p); Assert.Equal(0, r); Assert.Equal(0, h);
    }

    [Fact]
    public void Attitude_90DegPitch()
    {
        var (p, _, _) = Conversions.ConvertAttitude(Math.PI / 2, 0, 0);
        Assert.Equal(900, p);
    }

    [Fact]
    public void Attitude_NegativePitch()
    {
        var (p, _, _) = Conversions.ConvertAttitude(-Math.PI / 4, 0, 0);
        Assert.Equal(-450, p);
    }

    [Fact]
    public void Attitude_PitchClampedAt180()
    {
        var (p, _, _) = Conversions.ConvertAttitude(Math.PI * 200 / 180, 0, 0);
        Assert.Equal(1800, p);
    }

    [Fact]
    public void Attitude_HeadingWraps()
    {
        var (_, _, h) = Conversions.ConvertAttitude(0, 0, 2 * Math.PI);
        Assert.Equal(0, h);
    }

    [Fact]
    public void Attitude_Heading270()
    {
        var (_, _, h) = Conversions.ConvertAttitude(0, 0, Math.PI * 3 / 2);
        Assert.Equal(2700, h);
    }

    [Fact]
    public void Attitude_NegativeHeadingWraps()
    {
        var (_, _, h) = Conversions.ConvertAttitude(0, 0, -Math.PI / 2);
        Assert.Equal(2700, h);
    }

    // --- Engine ---

    [Fact]
    public void Engine_ZeroInputs()
    {
        var (rpm, thr, ff, ot, op) = Conversions.ConvertEngine(0, 0, 0, 0, 0);
        Assert.Equal((ushort)0, rpm);
        Assert.Equal((byte)0, thr);
        Assert.Equal((byte)0, ff);
    }

    [Fact]
    public void Engine_RpmClamped()
    {
        var (rpm, _, _, _, _) = Conversions.ConvertEngine(70000, 0, 0, 0, 0);
        Assert.Equal((ushort)65535, rpm);
    }

    [Fact]
    public void Engine_ThrottleClamped()
    {
        var (_, thr, _, _, _) = Conversions.ConvertEngine(0, 150, 0, 0, 0);
        Assert.Equal((byte)100, thr);
    }

    [Fact]
    public void Engine_FuelFlowMapping()
    {
        // 10 GPH → 10*255/50 = 51
        var (_, _, ff, _, _) = Conversions.ConvertEngine(0, 0, 10, 0, 0);
        Assert.Equal((byte)51, ff);
    }

    // --- FlightData ---

    [Fact]
    public void FlightData_ZeroInputs()
    {
        var (ias, alt, vs, gs) = Conversions.ConvertFlightData(0, 0, 0, 0);
        Assert.Equal((ushort)0, ias);
        Assert.Equal(0, alt);
        Assert.Equal((short)0, vs);
    }

    [Fact]
    public void FlightData_TypicalCruise()
    {
        var (ias, alt, vs, gs) = Conversions.ConvertFlightData(125, 5000, 500, 130);
        Assert.Equal((ushort)1250, ias);
        Assert.Equal(5000, alt);
        Assert.Equal((short)500, vs);
        Assert.Equal((ushort)1300, gs);
    }

    [Fact]
    public void FlightData_AirspeedClamped()
    {
        var (ias, _, _, _) = Conversions.ConvertFlightData(7000, 0, 0, 0);
        Assert.Equal((ushort)65535, ias);
    }

    [Fact]
    public void FlightData_VspeedClamped()
    {
        var (_, _, vs, _) = Conversions.ConvertFlightData(0, 0, 40000, 0);
        Assert.Equal(short.MaxValue, vs);
    }

    // --- GForce ---

    [Fact]
    public void GForce_ZeroInputs()
    {
        var (gx, gy, gz) = Conversions.ConvertGForce(0, 0, 0);
        Assert.Equal((short)0, gx); Assert.Equal((short)0, gy); Assert.Equal((short)0, gz);
    }

    [Fact]
    public void GForce_1G()
    {
        var (_, gy, _) = Conversions.ConvertGForce(0, 32.174, 0);
        Assert.Equal((short)100, gy);
    }

    [Fact]
    public void GForce_2G()
    {
        var (_, gy, _) = Conversions.ConvertGForce(0, 64.348, 0);
        Assert.Equal((short)200, gy);
    }

    [Fact]
    public void GForce_NegativeG()
    {
        var (_, gy, _) = Conversions.ConvertGForce(0, -32.174, 0);
        Assert.Equal((short)-100, gy);
    }

    // --- Alert/AP flags ---

    [Fact]
    public void AlertFlags_StallOnly()
    {
        var flags = Conversions.BuildAlertFlags(stall: true, overspeed: false,
            gearUnsafe: false, lowFuel: false, engineFire: false, apDisconnect: false);
        Assert.Equal((ushort)1, flags);
    }

    [Fact]
    public void AlertFlags_Multiple()
    {
        var flags = Conversions.BuildAlertFlags(stall: true, overspeed: true,
            gearUnsafe: false, lowFuel: false, engineFire: false, apDisconnect: false);
        Assert.Equal((ushort)3, flags);
    }

    [Fact]
    public void ApFlags_MasterAndAlt()
    {
        var flags = Conversions.BuildApFlags(master: true, hdg: false, alt: true,
            vs: false, nav: false, apr: false);
        Assert.Equal((ushort)5, flags); // bit 0 + bit 2
    }

    // --- Config ---

    [Fact]
    public void Config_GearDown()
    {
        var (_, gear, _, _) = Conversions.ConvertConfig(0, 1, 1.0, 0, 0);
        Assert.Equal((byte)2, gear); // down
    }

    [Fact]
    public void Config_GearUp()
    {
        var (_, gear, _, _) = Conversions.ConvertConfig(0, 0, 0.0, 0, 0);
        Assert.Equal((byte)0, gear); // up
    }

    [Fact]
    public void Config_GearTransit()
    {
        var (_, gear, _, _) = Conversions.ConvertConfig(0, 1, 0.5, 0, 0);
        Assert.Equal((byte)1, gear); // transit
    }
}
