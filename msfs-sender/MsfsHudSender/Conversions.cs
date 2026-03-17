namespace MsfsHudSender;

/// <summary>Pure conversion functions from SimConnect raw values to protocol format.</summary>
public static class Conversions
{
    private const double RadToDeg = 180.0 / Math.PI;
    private const double GConv = 32.174; // 1G in ft/s²

    public static (short pitch, short roll, short heading) ConvertAttitude(
        double pitchRad, double rollRad, double headingRad)
    {
        var pitchDeg = pitchRad * RadToDeg;
        var rollDeg = rollRad * RadToDeg;
        var headingDeg = headingRad * RadToDeg;

        var pitch = (short)Math.Clamp((int)(pitchDeg * 10), -1800, 1800);
        var roll = (short)Math.Clamp((int)(rollDeg * 10), -1800, 1800);
        var heading = (short)(((int)(headingDeg * 10) % 3600 + 3600) % 3600);
        return (pitch, roll, heading);
    }

    public static (ushort rpm, byte throttle, byte fuelFlow, byte oilTemp, byte oilPress) ConvertEngine(
        double rpmRaw, double throttleRaw, double ffGph,
        double oilTempRankine, double oilPressPsf)
    {
        var rpm = (ushort)Math.Clamp((int)rpmRaw, 0, 65535);
        var throttle = (byte)Math.Clamp((int)throttleRaw, 0, 100);
        // Map fuel flow (0-50 GPH typical) to 0-255
        var fuelFlow = (byte)Math.Clamp((int)(ffGph * 255 / 50), 0, 255);
        // Oil temp: Rankine → °F → 0-255 (0-250°F range)
        var oilTempF = oilTempRankine / 1.8 - 459.67 / 1.8; // Rankine to Fahrenheit
        var oilTemp = (byte)Math.Clamp((int)(oilTempF * 255 / 250), 0, 255);
        // Oil pressure: Psf → PSI (÷144) → 0-255 (0-100 PSI range)
        var oilPressPsi = oilPressPsf / 144.0;
        var oilPress = (byte)Math.Clamp((int)(oilPressPsi * 255 / 100), 0, 255);
        return (rpm, throttle, fuelFlow, oilTemp, oilPress);
    }

    public static (ushort airspeed, int altitude, short vspeed, ushort groundSpeed) ConvertFlightData(
        double iasKts, double altFt, double vsFpm, double gsKts)
    {
        var airspeed = (ushort)Math.Clamp((int)(iasKts * 10), 0, 65535);
        var altitude = (int)Math.Clamp(altFt, int.MinValue, int.MaxValue);
        var vspeed = (short)Math.Clamp((int)vsFpm, short.MinValue, short.MaxValue);
        var groundSpeed = (ushort)Math.Clamp((int)(gsKts * 10), 0, 65535);
        return (airspeed, altitude, vspeed, groundSpeed);
    }

    public static (short gx, short gy, short gz) ConvertGForce(
        double axFtS2, double ayFtS2, double azFtS2)
    {
        var gx = (short)Math.Clamp((int)(axFtS2 / GConv * 100), short.MinValue, short.MaxValue);
        var gy = (short)Math.Clamp((int)(ayFtS2 / GConv * 100), short.MinValue, short.MaxValue);
        var gz = (short)Math.Clamp((int)(azFtS2 / GConv * 100), short.MinValue, short.MaxValue);
        return (gx, gy, gz);
    }

    public static (int lat, int lon, short hdgBug, ushort wpDist, short wpBearing) ConvertNavData(
        double latRad, double lonRad, double hdgBugDeg, double wpDistMeters, double wpBearingDeg)
    {
        var lat = (int)Math.Clamp(latRad * RadToDeg * 1e7, int.MinValue, int.MaxValue);
        var lon = (int)Math.Clamp(lonRad * RadToDeg * 1e7, int.MinValue, int.MaxValue);
        var hdgBug = (short)(((int)(hdgBugDeg * 10) % 3600 + 3600) % 3600);
        // Meters → nautical miles × 10
        var wpDist = (ushort)Math.Clamp((int)(wpDistMeters / 1852.0 * 10), 0, 65535);
        var wpBearing = (short)(((int)(wpBearingDeg * 10) % 3600 + 3600) % 3600);
        return (lat, lon, hdgBug, wpDist, wpBearing);
    }

    public static (byte flapsPct, byte gearState, sbyte elevTrim, sbyte rudderTrim) ConvertConfig(
        double flapsPct, double gearHandle, double gearExtended,
        double elevTrimRad, double rudderTrimPct)
    {
        var flaps = (byte)Math.Clamp((int)flapsPct, 0, 100);
        // Gear: handle=0 and extended<0.01 → up(0), extended>=0.99 → down(2), else transit(1)
        byte gear;
        if (gearHandle == 0 && gearExtended < 0.01) gear = 0;
        else if (gearExtended >= 0.99) gear = 2;
        else gear = 1;
        // Elevator trim: radians (typically -0.3..+0.3) → -100..+100
        var eTrim = (sbyte)Math.Clamp((int)(elevTrimRad / 0.3 * 100), -100, 100);
        // Rudder trim: already percent -100..+100
        var rTrim = (sbyte)Math.Clamp((int)rudderTrimPct, -100, 100);
        return (flaps, gear, eTrim, rTrim);
    }

    public static ushort BuildAlertFlags(bool stall, bool overspeed, bool gearUnsafe,
                                          bool lowFuel, bool engineFire, bool apDisconnect)
    {
        ushort flags = 0;
        if (stall) flags |= Protocol.Messages.AlertStall;
        if (overspeed) flags |= Protocol.Messages.AlertOverspeed;
        if (gearUnsafe) flags |= Protocol.Messages.AlertGearUnsafe;
        if (lowFuel) flags |= Protocol.Messages.AlertLowFuel;
        if (engineFire) flags |= Protocol.Messages.AlertEngineFire;
        if (apDisconnect) flags |= Protocol.Messages.AlertApDisconnect;
        return flags;
    }

    public static ushort BuildApFlags(bool master, bool hdg, bool alt,
                                       bool vs, bool nav, bool apr)
    {
        ushort flags = 0;
        if (master) flags |= Protocol.Messages.ApMaster;
        if (hdg) flags |= Protocol.Messages.ApHeadingLock;
        if (alt) flags |= Protocol.Messages.ApAltitudeLock;
        if (vs) flags |= Protocol.Messages.ApVsLock;
        if (nav) flags |= Protocol.Messages.ApNavLock;
        if (apr) flags |= Protocol.Messages.ApAprLock;
        return flags;
    }
}
