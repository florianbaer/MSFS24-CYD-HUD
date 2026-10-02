using MsfsHudSender.Protocol;

namespace MsfsHudSender;

/// <summary>
/// Synthetic telemetry for checking the display without the simulator: a light
/// single flying gentle S-turns while climbing and descending. Every screen gets
/// moving values, and a short stall warning fires once a minute. Its autopilot
/// reacts to the display's controls, so the touch buttons can be tried out too.
/// </summary>
public sealed class DemoFlight
{
    private ushort _apFlags = Messages.ApMaster | Messages.ApHeadingLock | Messages.ApAltitudeLock;
    private int _apAltitude = 6000;
    private int _apHeading = 270;

    /// <summary>What the demo aircraft's autopilot shows right now.</summary>
    public (ushort Flags, int Altitude, int Heading) Autopilot => (_apFlags, _apAltitude, _apHeading);

    /// <summary>Applies a control used on the display, like the simulator would.</summary>
    public void Apply(HudCommand command)
    {
        switch (command)
        {
            case HudCommand.ApMaster: _apFlags ^= Messages.ApMaster; break;
            case HudCommand.ApHeadingHold: _apFlags ^= Messages.ApHeadingLock; break;
            case HudCommand.ApAltitudeHold: _apFlags ^= Messages.ApAltitudeLock; break;
            case HudCommand.ApVerticalSpeedHold: _apFlags ^= Messages.ApVsLock; break;
            case HudCommand.ApNavHold: _apFlags ^= Messages.ApNavLock; break;
            case HudCommand.ApApproachHold: _apFlags ^= Messages.ApAprLock; break;
            case HudCommand.HeadingBugInc: _apHeading = (_apHeading + 1) % 360; break;
            case HudCommand.HeadingBugDec: _apHeading = (_apHeading + 359) % 360; break;
            case HudCommand.AltitudeInc: _apAltitude = Math.Min(_apAltitude + 100, 45000); break;
            case HudCommand.AltitudeDec: _apAltitude = Math.Max(_apAltitude - 100, 0); break;
        }
    }

    /// <summary>All frames for one send cycle at <paramref name="t"/> seconds into the demo.</summary>
    public IReadOnlyList<byte[]> Frames(double t)
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
                (short)(_apHeading * 10), (ushort)Math.Max(0, 240 - t * 0.5), Tenths(Mod(headingDeg - 5, 360))),
            FrameBuilder.FrameConfig(10, 2, (sbyte)(pitchDeg * 2), 0),
            FrameBuilder.FrameAutopilot(_apFlags, _apAltitude, (short)(_apHeading * 10)),
            // ECAM: a twin jet at climb power, slats out, burning fuel
            FrameBuilder.FrameEcamEngine(0, (ushort)Tenths(82 + 4 * Math.Sin(t / 9)), (ushort)Tenths(93.5 + Math.Sin(t / 9)),
                (short)(610 + 25 * Math.Sin(t / 9)), (ushort)(1150 + 60 * Math.Sin(t / 9))),
            FrameBuilder.FrameEcamEngine(1, (ushort)Tenths(81.6 + 4 * Math.Sin(t / 9 + 0.2)), (ushort)Tenths(93.1 + Math.Sin(t / 9 + 0.2)),
                (short)(622 + 25 * Math.Sin(t / 9 + 0.2)), (ushort)(1140 + 60 * Math.Sin(t / 9 + 0.2))),
            FrameBuilder.FrameEcamStatus((uint)Math.Max(0, 6200 - t * 0.6), 1, 50, 0,
                (ushort)(Messages.MemoSeatBelts | Messages.MemoLandingLights
                         | (Mod(t, 60) is > 20 and < 26 ? Messages.MemoSpeedBrake : 0))),
        ];
    }

    private static short Tenths(double deg) => (short)Math.Round(deg * 10);

    private static double Mod(double x, double m) => ((x % m) + m) % m;
}
