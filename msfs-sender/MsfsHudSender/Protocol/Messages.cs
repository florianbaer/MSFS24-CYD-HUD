namespace MsfsHudSender.Protocol;

public static class Messages
{
    public const byte MsgAttitude = 0x02;
    public const byte MsgEngine = 0x03;
    public const byte MsgFlightData = 0x04;
    public const byte MsgGForce = 0x05;
    public const byte MsgAlerts = 0x06;
    public const byte MsgNavData = 0x07;
    public const byte MsgConfig = 0x08;
    public const byte MsgAutopilot = 0x09;

    // Alert flag bits
    public const ushort AlertStall = 1 << 0;
    public const ushort AlertOverspeed = 1 << 1;
    public const ushort AlertGearUnsafe = 1 << 2;
    public const ushort AlertLowFuel = 1 << 3;
    public const ushort AlertEngineFire = 1 << 4;
    public const ushort AlertApDisconnect = 1 << 5;

    // Autopilot mode flag bits
    public const ushort ApMaster = 1 << 0;
    public const ushort ApHeadingLock = 1 << 1;
    public const ushort ApAltitudeLock = 1 << 2;
    public const ushort ApVsLock = 1 << 3;
    public const ushort ApNavLock = 1 << 4;
    public const ushort ApAprLock = 1 << 5;
}
