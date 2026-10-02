using MsfsHudSender.Protocol;

namespace MsfsHudSender;

/// <summary>Simulator events the display's controls trigger (MSFS key events).</summary>
public static class SimEvents
{
    /// <summary>
    /// The panel ("AP_PANEL_*") variants behave like pressing the button on the
    /// autopilot panel, which is what the default MSFS aircraft expect.
    /// </summary>
    public static readonly IReadOnlyDictionary<HudCommand, string> ForCommand = new Dictionary<HudCommand, string>
    {
        [HudCommand.ApMaster] = "AP_MASTER",
        [HudCommand.ApHeadingHold] = "AP_PANEL_HEADING_HOLD",
        [HudCommand.ApAltitudeHold] = "AP_PANEL_ALTITUDE_HOLD",
        [HudCommand.ApVerticalSpeedHold] = "AP_PANEL_VS_HOLD",
        [HudCommand.ApNavHold] = "AP_NAV1_HOLD",
        [HudCommand.ApApproachHold] = "AP_APR_HOLD",
        [HudCommand.HeadingBugInc] = "HEADING_BUG_INC",
        [HudCommand.HeadingBugDec] = "HEADING_BUG_DEC",
        [HudCommand.AltitudeInc] = "AP_ALT_VAR_INC",
        [HudCommand.AltitudeDec] = "AP_ALT_VAR_DEC",
    };

    /// <summary>Parses a command frame payload; null if it is not a known command.</summary>
    public static HudCommand? Parse(Frame frame) =>
        frame.Type == Messages.MsgCommand && frame.Payload.Length == 1
            && Enum.IsDefined(typeof(HudCommand), frame.Payload[0])
            ? (HudCommand)frame.Payload[0]
            : null;
}
