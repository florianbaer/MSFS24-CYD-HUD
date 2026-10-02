using System.Buffers.Binary;

namespace MsfsHudSender.Protocol;

/// <summary>Builds COBS-framed wire messages: [0x00][COBS(type|payload|crc)][0x00]</summary>
public static class FrameBuilder
{
    private static byte[] Frame(byte msgType, ReadOnlySpan<byte> payload)
    {
        // raw = [msgType] [payload...] [crc]
        Span<byte> raw = stackalloc byte[1 + payload.Length + 1];
        raw[0] = msgType;
        payload.CopyTo(raw[1..]);
        raw[^1] = Crc8.Compute(raw[..^1]);

        var encoded = Cobs.Encode(raw);
        var frame = new byte[encoded.Length + 2];
        frame[0] = 0x00;
        encoded.CopyTo(frame, 1);
        frame[^1] = 0x00;
        return frame;
    }

    /// <summary>Command from the display (the display builds these; used by tests and tools).</summary>
    public static byte[] FrameCommand(HudCommand command) => Frame(Messages.MsgCommand, [(byte)command]);

    /// <summary>Attitude: pitch/roll/heading in tenths of degrees (int16 LE).</summary>
    public static byte[] FrameAttitude(short pitch, short roll, short heading)
    {
        Span<byte> payload = stackalloc byte[6];
        BinaryPrimitives.WriteInt16LittleEndian(payload[0..], pitch);
        BinaryPrimitives.WriteInt16LittleEndian(payload[2..], roll);
        BinaryPrimitives.WriteInt16LittleEndian(payload[4..], heading);
        return Frame(Messages.MsgAttitude, payload);
    }

    /// <summary>Engine: idx + rpm/throttle/fuel_flow/oil_temp/oil_press.</summary>
    public static byte[] FrameEngine(byte engineIdx, ushort rpm, byte throttle,
                                      byte fuelFlow, byte oilTemp, byte oilPress)
    {
        Span<byte> payload = stackalloc byte[7];
        payload[0] = engineIdx;
        BinaryPrimitives.WriteUInt16LittleEndian(payload[1..], rpm);
        payload[3] = throttle;
        payload[4] = fuelFlow;
        payload[5] = oilTemp;
        payload[6] = oilPress;
        return Frame(Messages.MsgEngine, payload);
    }

    /// <summary>Flight data: airspeed(u16), altitude(i32), vspeed(i16), gs(u16).</summary>
    public static byte[] FrameFlightData(ushort airspeed, int altitude,
                                          short vspeed, ushort groundSpeed)
    {
        Span<byte> payload = stackalloc byte[10];
        BinaryPrimitives.WriteUInt16LittleEndian(payload[0..], airspeed);
        BinaryPrimitives.WriteInt32LittleEndian(payload[2..], altitude);
        BinaryPrimitives.WriteInt16LittleEndian(payload[6..], vspeed);
        BinaryPrimitives.WriteUInt16LittleEndian(payload[8..], groundSpeed);
        return Frame(Messages.MsgFlightData, payload);
    }

    /// <summary>G-force: gx/gy/gz in hundredths of G (int16 LE).</summary>
    public static byte[] FrameGForce(short gx, short gy, short gz)
    {
        Span<byte> payload = stackalloc byte[6];
        BinaryPrimitives.WriteInt16LittleEndian(payload[0..], gx);
        BinaryPrimitives.WriteInt16LittleEndian(payload[2..], gy);
        BinaryPrimitives.WriteInt16LittleEndian(payload[4..], gz);
        return Frame(Messages.MsgGForce, payload);
    }

    /// <summary>Alerts: uint16 bitfield of active alerts.</summary>
    public static byte[] FrameAlerts(ushort flags)
    {
        Span<byte> payload = stackalloc byte[2];
        BinaryPrimitives.WriteUInt16LittleEndian(payload, flags);
        return Frame(Messages.MsgAlerts, payload);
    }

    /// <summary>Nav data: lat/lon (i32 deg*1e7), hdg_bug(i16), wp_dist(u16), wp_bearing(i16).</summary>
    public static byte[] FrameNavData(int lat, int lon, short hdgBug,
                                       ushort wpDist, short wpBearing)
    {
        Span<byte> payload = stackalloc byte[14];
        BinaryPrimitives.WriteInt32LittleEndian(payload[0..], lat);
        BinaryPrimitives.WriteInt32LittleEndian(payload[4..], lon);
        BinaryPrimitives.WriteInt16LittleEndian(payload[8..], hdgBug);
        BinaryPrimitives.WriteUInt16LittleEndian(payload[10..], wpDist);
        BinaryPrimitives.WriteInt16LittleEndian(payload[12..], wpBearing);
        return Frame(Messages.MsgNavData, payload);
    }

    /// <summary>Config: flaps_pct(u8), gear_state(u8), elev_trim(i8), rudder_trim(i8).</summary>
    public static byte[] FrameConfig(byte flapsPct, byte gearState,
                                      sbyte elevTrim, sbyte rudderTrim)
    {
        Span<byte> payload = stackalloc byte[4];
        payload[0] = flapsPct;
        payload[1] = gearState;
        payload[2] = (byte)elevTrim;
        payload[3] = (byte)rudderTrim;
        return Frame(Messages.MsgConfig, payload);
    }

    /// <summary>Autopilot: mode_flags(u16), target_alt(i32), target_hdg(i16).</summary>
    /// <summary>ECAM engine: N1/N2 in tenths of %, EGT in °C, fuel flow in kg/h.</summary>
    public static byte[] FrameEcamEngine(byte engineIdx, ushort n1, ushort n2, short egt, ushort fuelFlow)
    {
        Span<byte> payload = stackalloc byte[9];
        payload[0] = engineIdx;
        BinaryPrimitives.WriteUInt16LittleEndian(payload[1..], n1);
        BinaryPrimitives.WriteUInt16LittleEndian(payload[3..], n2);
        BinaryPrimitives.WriteInt16LittleEndian(payload[5..], egt);
        BinaryPrimitives.WriteUInt16LittleEndian(payload[7..], fuelFlow);
        return Frame(Messages.MsgEcamEngine, payload);
    }

    /// <summary>ECAM status: fuel on board (kg), flaps detent and positions, memo flags.</summary>
    public static byte[] FrameEcamStatus(uint fobKg, byte flapsIndex, byte slatsPct, byte flapsPct, ushort memoFlags)
    {
        Span<byte> payload = stackalloc byte[9];
        BinaryPrimitives.WriteUInt32LittleEndian(payload[0..], fobKg);
        payload[4] = flapsIndex;
        payload[5] = slatsPct;
        payload[6] = flapsPct;
        BinaryPrimitives.WriteUInt16LittleEndian(payload[7..], memoFlags);
        return Frame(Messages.MsgEcamStatus, payload);
    }

    public static byte[] FrameAutopilot(ushort modeFlags, int targetAlt, short targetHdg)
    {
        Span<byte> payload = stackalloc byte[8];
        BinaryPrimitives.WriteUInt16LittleEndian(payload[0..], modeFlags);
        BinaryPrimitives.WriteInt32LittleEndian(payload[2..], targetAlt);
        BinaryPrimitives.WriteInt16LittleEndian(payload[6..], targetHdg);
        return Frame(Messages.MsgAutopilot, payload);
    }
}
