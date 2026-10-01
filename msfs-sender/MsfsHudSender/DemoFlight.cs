using MsfsHudSender.Protocol;

namespace MsfsHudSender;

/// <summary>
/// Synthetic telemetry for checking the display without the simulator: a light
/// single flying gentle S-turns while climbing and descending. Every screen gets
/// moving values, and a short stall warning fires once a minute.
/// </summary>
public static class DemoFlight
{
    /// <summary>All frames for one send cycle at <paramref name="t"/> seconds into the demo.</summary>
    public static IReadOnlyList<byte[]> Frames(double t)
    {
        // Bank swings ±25° over 24 s; pitch follows a slower climb/descent cycle
        double bankDeg = 25 * Math.Sin(t * 2 * Math.PI / 24);
        double pitchDeg = 4 + 6 * Math.Sin(t * 2 * Math.PI / 40);
        // Coordinated turn: rate ≈ g·tan(bank)/V, integrated in closed form is messy;
        // a heading that drifts with the bank is convincing enough here.
        double headingDeg = Mod(270 + 40 * (1 - Math.Cos(t * 2 * Math.PI / 24)) + t * 0.5, 360);

        double vsFpm = pitchDeg * 110;
        double altFt = 4500 + 600 * (1 - Math.Cos(t * 2 * Math.PI / 40));
        double iasKt = 110 - pitchDeg * 2;
        double loadG = 1 / Math.Cos(bankDeg * Math.PI / 180) + 0.05 * Math.Sin(t * 3);

        bool stall = Mod(t, 60) is > 50 and < 53;
        ushort rpm = (ushort)(2350 + 150 * Math.Sin(t * 2 * Math.PI / 40));

        var (pitch, roll, heading) = (Tenths(pitchDeg), Tenths(bankDeg), Tenths(headingDeg));
        return
        [
            FrameBuilder.FrameAttitude(pitch, roll, heading),
            FrameBuilder.FrameEngine(0, rpm, (byte)(80 + 10 * Math.Sin(t / 7)), 47, 187, 158),
            FrameBuilder.FrameFlightData((ushort)(iasKt * 10), (int)altFt, (short)vsFpm, (ushort)((iasKt + 8) * 10)),
            FrameBuilder.FrameGForce((short)(5 * Math.Sin(t)), (short)Math.Round(loadG * 100), (short)(8 * Math.Sin(t / 2))),
            FrameBuilder.FrameAlerts(stall ? Messages.AlertStall : (ushort)0),
            FrameBuilder.FrameNavData(474502000 + (int)(t * 2000), 85618000 + (int)(t * 3000),
                2700, (ushort)Math.Max(0, 240 - t * 0.5), Tenths(Mod(headingDeg - 5, 360))),
            FrameBuilder.FrameConfig(10, 2, (sbyte)(pitchDeg * 2), 0),
            FrameBuilder.FrameAutopilot(Messages.ApMaster | Messages.ApHeadingLock | Messages.ApAltitudeLock, 6000, 2700),
        ];
    }

    private static short Tenths(double deg) => (short)Math.Round(deg * 10);

    private static double Mod(double x, double m) => ((x % m) + m) % m;
}
