using System.Buffers.Binary;
using MsfsHudSender.Protocol;

namespace MsfsHudSender.Tests;

public class FrameTests
{
    private static byte[] DecodeFrame(byte[] frame)
    {
        Assert.Equal(0x00, frame[0]);
        Assert.Equal(0x00, frame[^1]);
        Assert.DoesNotContain((byte)0x00, frame[1..^1]);
        return Cobs.Decode(frame[1..^1]);
    }

    private static void VerifyCrc(byte[] decoded)
    {
        var payload = decoded[..^1];
        Assert.Equal(decoded[^1], Crc8.Compute(payload));
    }

    // --- Attitude ---

    [Fact]
    public void Attitude_FrameStructure()
    {
        var frame = FrameBuilder.FrameAttitude(0, 0, 0);
        DecodeFrame(frame);
    }

    [Fact]
    public void Attitude_Decodable()
    {
        var decoded = DecodeFrame(FrameBuilder.FrameAttitude(450, -300, 2700));
        VerifyCrc(decoded);
        Assert.Equal(Messages.MsgAttitude, decoded[0]);
        Assert.Equal(450, BinaryPrimitives.ReadInt16LittleEndian(decoded.AsSpan(1)));
        Assert.Equal(-300, BinaryPrimitives.ReadInt16LittleEndian(decoded.AsSpan(3)));
        Assert.Equal(2700, BinaryPrimitives.ReadInt16LittleEndian(decoded.AsSpan(5)));
    }

    [Fact]
    public void Attitude_ByteIdenticalToRust()
    {
        var expected = Convert.FromHexString("000902c201d4fe8c0a6200");
        Assert.Equal(expected, FrameBuilder.FrameAttitude(450, -300, 2700));
    }

    // --- Engine (new format with engine_idx) ---

    [Fact]
    public void Engine_FrameStructure()
    {
        var frame = FrameBuilder.FrameEngine(0, 2450, 75, 128, 200, 180);
        DecodeFrame(frame);
    }

    [Fact]
    public void Engine_Decodable()
    {
        var decoded = DecodeFrame(FrameBuilder.FrameEngine(0, 2450, 75, 128, 200, 180));
        VerifyCrc(decoded);
        Assert.Equal(Messages.MsgEngine, decoded[0]);
        Assert.Equal(0, decoded[1]); // engine_idx
        Assert.Equal(2450, BinaryPrimitives.ReadUInt16LittleEndian(decoded.AsSpan(2)));
        Assert.Equal(75, decoded[4]); // throttle
        Assert.Equal(128, decoded[5]); // fuel_flow
        Assert.Equal(200, decoded[6]); // oil_temp
        Assert.Equal(180, decoded[7]); // oil_press
    }

    // --- FlightData ---

    [Fact]
    public void FlightData_Decodable()
    {
        var decoded = DecodeFrame(FrameBuilder.FrameFlightData(1250, -50, -1500, 1400));
        VerifyCrc(decoded);
        Assert.Equal(Messages.MsgFlightData, decoded[0]);
        Assert.Equal(1250, BinaryPrimitives.ReadUInt16LittleEndian(decoded.AsSpan(1)));
        Assert.Equal(-50, BinaryPrimitives.ReadInt32LittleEndian(decoded.AsSpan(3)));
        Assert.Equal(-1500, BinaryPrimitives.ReadInt16LittleEndian(decoded.AsSpan(7)));
        Assert.Equal(1400, BinaryPrimitives.ReadUInt16LittleEndian(decoded.AsSpan(9)));
    }

    [Fact]
    public void FlightData_ByteIdenticalToRust()
    {
        var expected = Convert.FromHexString("000d04e204ceffffff24fa7805c700");
        Assert.Equal(expected, FrameBuilder.FrameFlightData(1250, -50, -1500, 1400));
    }

    // --- GForce ---

    [Fact]
    public void GForce_Decodable()
    {
        var decoded = DecodeFrame(FrameBuilder.FrameGForce(-15, 120, 5));
        VerifyCrc(decoded);
        Assert.Equal(Messages.MsgGForce, decoded[0]);
        Assert.Equal(-15, BinaryPrimitives.ReadInt16LittleEndian(decoded.AsSpan(1)));
        Assert.Equal(120, BinaryPrimitives.ReadInt16LittleEndian(decoded.AsSpan(3)));
        Assert.Equal(5, BinaryPrimitives.ReadInt16LittleEndian(decoded.AsSpan(5)));
    }

    [Fact]
    public void GForce_ByteIdenticalToRust()
    {
        var expected = Convert.FromHexString("000505f1ff780205023d00");
        Assert.Equal(expected, FrameBuilder.FrameGForce(-15, 120, 5));
    }

    // --- Alerts ---

    [Fact]
    public void Alerts_Roundtrip()
    {
        ushort flags = Messages.AlertStall | Messages.AlertOverspeed;
        var decoded = DecodeFrame(FrameBuilder.FrameAlerts(flags));
        VerifyCrc(decoded);
        Assert.Equal(Messages.MsgAlerts, decoded[0]);
        Assert.Equal(flags, BinaryPrimitives.ReadUInt16LittleEndian(decoded.AsSpan(1)));
    }

    // --- NavData ---

    [Fact]
    public void NavData_Roundtrip()
    {
        var decoded = DecodeFrame(FrameBuilder.FrameNavData(
            lat: 523456789, lon: -12345678, hdgBug: 2700, wpDist: 150, wpBearing: 900));
        VerifyCrc(decoded);
        Assert.Equal(Messages.MsgNavData, decoded[0]);
        Assert.Equal(523456789, BinaryPrimitives.ReadInt32LittleEndian(decoded.AsSpan(1)));
        Assert.Equal(-12345678, BinaryPrimitives.ReadInt32LittleEndian(decoded.AsSpan(5)));
        Assert.Equal(2700, BinaryPrimitives.ReadInt16LittleEndian(decoded.AsSpan(9)));
        Assert.Equal(150, BinaryPrimitives.ReadUInt16LittleEndian(decoded.AsSpan(11)));
        Assert.Equal(900, BinaryPrimitives.ReadInt16LittleEndian(decoded.AsSpan(13)));
    }

    // --- Config ---

    [Fact]
    public void Config_Roundtrip()
    {
        var decoded = DecodeFrame(FrameBuilder.FrameConfig(
            flapsPct: 50, gearState: 2, elevTrim: -30, rudderTrim: 15));
        VerifyCrc(decoded);
        Assert.Equal(Messages.MsgConfig, decoded[0]);
        Assert.Equal(50, decoded[1]);
        Assert.Equal(2, decoded[2]);
        Assert.Equal(-30, (sbyte)decoded[3]);
        Assert.Equal(15, (sbyte)decoded[4]);
    }

    // --- Autopilot ---

    [Fact]
    public void Autopilot_Roundtrip()
    {
        ushort flags = Messages.ApMaster | Messages.ApAltitudeLock;
        var decoded = DecodeFrame(FrameBuilder.FrameAutopilot(
            modeFlags: flags, targetAlt: 5000, targetHdg: 2700));
        VerifyCrc(decoded);
        Assert.Equal(Messages.MsgAutopilot, decoded[0]);
        Assert.Equal(flags, BinaryPrimitives.ReadUInt16LittleEndian(decoded.AsSpan(1)));
        Assert.Equal(5000, BinaryPrimitives.ReadInt32LittleEndian(decoded.AsSpan(3)));
        Assert.Equal(2700, BinaryPrimitives.ReadInt16LittleEndian(decoded.AsSpan(7)));
    }
}
