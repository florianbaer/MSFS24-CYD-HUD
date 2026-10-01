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

    // SimConnect reports PLANE PITCH DEGREES positive nose-DOWN; the wire format is positive nose-UP.

    [Fact]
    public void Attitude_NoseUpIsPositive()
    {
        var (p, _, _) = Conversions.ConvertAttitude(-Math.PI / 2, 0, 0);
        Assert.Equal(900, p);
    }

    [Fact]
    public void Attitude_NoseDownIsNegative()
    {
        var (p, _, _) = Conversions.ConvertAttitude(Math.PI / 4, 0, 0);
        Assert.Equal(-450, p);
    }

    [Fact]
    public void Attitude_PitchClampedAt180()
    {
        var (p, _, _) = Conversions.ConvertAttitude(-Math.PI * 200 / 180, 0, 0);
        Assert.Equal(1800, p);
    }

    [Fact]
    public void Attitude_RollPassesThrough()
    {
        var (_, r, _) = Conversions.ConvertAttitude(0, Math.PI / 6, 0);
        Assert.Equal(300, r);
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

    [Fact]
    public void Engine_OilTempRankineToFahrenheit()
    {
        // 180 °F = 639.67 °R → 180*255/250 = 183
        var (_, _, _, ot, _) = Conversions.ConvertEngine(0, 0, 0, 639.67, 0);
        Assert.Equal((byte)183, ot);
    }

    [Fact]
    public void Engine_OilTempBelowZeroFahrenheitClamped()
    {
        var (_, _, _, ot, _) = Conversions.ConvertEngine(0, 0, 0, 400, 0);
        Assert.Equal((byte)0, ot);
    }

    [Fact]
    public void Engine_OilPressurePsfToPsi()
    {
        // 62 PSI = 8928 psf → 62*255/100 = 158
        var (_, _, _, _, op) = Conversions.ConvertEngine(0, 0, 0, 0, 8928);
        Assert.Equal((byte)158, op);
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
    // Inputs: G FORCE (load factor), ACCELERATION BODY X (lateral), ACCELERATION BODY Z (longitudinal).

    [Fact]
    public void GForce_LevelFlightIsOneG()
    {
        var (gx, gy, gz) = Conversions.ConvertGForce(1.0, 0, 0);
        Assert.Equal((short)0, gx); Assert.Equal((short)100, gy); Assert.Equal((short)0, gz);
    }

    [Fact]
    public void GForce_2G()
    {
        var (_, gy, _) = Conversions.ConvertGForce(2.0, 0, 0);
        Assert.Equal((short)200, gy);
    }

    [Fact]
    public void GForce_NegativeG()
    {
        var (_, gy, _) = Conversions.ConvertGForce(-1.0, 0, 0);
        Assert.Equal((short)-100, gy);
    }

    [Fact]
    public void GForce_BodyZIsLongitudinal()
    {
        var (gx, _, gz) = Conversions.ConvertGForce(1.0, 0, 32.174);
        Assert.Equal((short)100, gx);
        Assert.Equal((short)0, gz);
    }

    [Fact]
    public void GForce_BodyXIsLateral()
    {
        var (gx, _, gz) = Conversions.ConvertGForce(1.0, -16.087, 0);
        Assert.Equal((short)0, gx);
        Assert.Equal((short)-50, gz);
    }

    // --- Autopilot targets ---

    [Fact]
    public void ApTargets_Typical()
    {
        var (alt, hdg) = Conversions.ConvertAutopilotTargets(6000, 270);
        Assert.Equal(6000, alt);
        Assert.Equal((short)2700, hdg);
    }

    [Fact]
    public void ApTargets_Heading360WrapsToZero()
    {
        var (_, hdg) = Conversions.ConvertAutopilotTargets(0, 360);
        Assert.Equal((short)0, hdg);
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
