using System.Buffers.Binary;
using System.Text;

namespace MsfsHudSender.SimConnect;

/// <summary>
/// The parts of the SimConnect wire protocol the sender uses, built without
/// Microsoft's SimConnect.dll. The layout matches what the official client
/// sends; tests pin it to bytes captured from node-simconnect
/// (github.com/EvenAR/node-simconnect, LGPL-3.0), an independent client known
/// to work with MSFS 2020 and 2024.
///
/// Every client packet starts with a 16-byte header:
///   size (u32) | protocol (u32) | 0xF0000000 | function id (u32) | send id (u32)
/// Every server message starts with a 12-byte header:
///   size (u32) | protocol (u32) | message id (u32)
/// All values are little-endian; strings are fixed-length, zero-padded.
/// </summary>
public static class SimConnectProtocol
{
    /// <summary>FSX SP2 protocol: understood by FSX, MSFS 2020 and MSFS 2024.</summary>
    public const uint ProtocolFsxSp2 = 4;

    public const uint Unused = 0xFFFFFFFF;
    public const uint ObjectIdUser = 0;

    public const int ClientHeaderSize = 16;
    public const int ServerHeaderSize = 12;
    public const int MaxMessageSize = 1 << 20;

    // Client functions
    private const uint FnOpen = 0x01;
    private const uint FnMapClientEventToSimEvent = 0x04;
    private const uint FnTransmitClientEvent = 0x05;
    private const uint FnAddToDataDefinition = 0x0C;
    private const uint FnRequestDataOnSimObject = 0x0E;

    // Server messages
    public const uint RecvNull = 0;
    public const uint RecvException = 1;
    public const uint RecvOpen = 2;
    public const uint RecvQuit = 3;
    public const uint RecvSimObjectData = 8;

    /// <summary>TransmitClientEvent flag: the group id is a priority.</summary>
    public const uint EventFlagGroupIdIsPriority = 0x10;
    public const uint PriorityHighest = 1;

    public enum DataType : uint { Int32 = 1, Int64 = 2, Float32 = 3, Float64 = 4 }

    public enum Period : uint { Never = 0, Once = 1, VisualFrame = 2, SimFrame = 3, Second = 4 }

    /// <summary>Open: application name plus the client version the protocol implies.</summary>
    public static byte[] Open(string appName, uint sendId)
    {
        var p = new PacketWriter(FnOpen, ProtocolFsxSp2);
        p.String(appName, 256);
        p.U32(0);
        p.Byte(0);
        p.String("XSF", 3);          // FSX SP2 client: 10.0.61259.0
        p.U32(10);
        p.U32(0);
        p.U32(61259);
        p.U32(0);
        return p.Finish(sendId);
    }

    public static byte[] AddToDataDefinition(uint defineId, string datumName, string? unitsName,
        DataType type, uint sendId, float epsilon = 0, uint datumId = Unused)
    {
        var p = new PacketWriter(FnAddToDataDefinition, ProtocolFsxSp2);
        p.U32(defineId);
        p.String(datumName, 256);
        p.String(unitsName ?? "", 256);
        p.U32((uint)type);
        p.F32(epsilon);
        p.U32(datumId);
        return p.Finish(sendId);
    }

    public static byte[] RequestDataOnSimObject(uint requestId, uint defineId, uint objectId,
        Period period, uint sendId, uint flags = 0, uint origin = 0, uint interval = 0, uint limit = 0)
    {
        var p = new PacketWriter(FnRequestDataOnSimObject, ProtocolFsxSp2);
        p.U32(requestId);
        p.U32(defineId);
        p.U32(objectId);
        p.U32((uint)period);
        p.U32(flags);
        p.U32(origin);
        p.U32(interval);
        p.U32(limit);
        return p.Finish(sendId);
    }

    /// <summary>Binds a client event id to a simulator event such as "AP_MASTER".</summary>
    public static byte[] MapClientEventToSimEvent(uint eventId, string eventName, uint sendId)
    {
        var p = new PacketWriter(FnMapClientEventToSimEvent, ProtocolFsxSp2);
        p.U32(eventId);
        p.String(eventName, 256);
        return p.Finish(sendId);
    }

    /// <summary>Fires a mapped event at an object (the user aircraft), as if a cockpit control was used.</summary>
    public static byte[] TransmitClientEvent(uint objectId, uint eventId, uint data, uint sendId,
        uint groupId = PriorityHighest, uint flags = EventFlagGroupIdIsPriority)
    {
        var p = new PacketWriter(FnTransmitClientEvent, ProtocolFsxSp2);
        p.U32(objectId);
        p.U32(eventId);
        p.U32(data);
        p.U32(groupId);
        p.U32(flags);
        return p.Finish(sendId);
    }

    /// <summary>Message id of a complete server message (header included).</summary>
    public static uint MessageId(ReadOnlySpan<byte> message) =>
        BinaryPrimitives.ReadUInt32LittleEndian(message[8..]);

    public static OpenInfo ParseOpen(ReadOnlySpan<byte> message)
    {
        var b = message[ServerHeaderSize..];
        return new OpenInfo(
            ReadString(b[..256]),
            BinaryPrimitives.ReadUInt32LittleEndian(b[256..]),
            BinaryPrimitives.ReadUInt32LittleEndian(b[260..]),
            BinaryPrimitives.ReadUInt32LittleEndian(b[272..]),   // after app version major/minor/build/build
            BinaryPrimitives.ReadUInt32LittleEndian(b[276..]));
    }

    public static SimObjectData ParseSimObjectData(ReadOnlySpan<byte> message)
    {
        // requestID, objectID, defineID, flags, entryNumber, outOf, defineCount, then the values
        var b = message[ServerHeaderSize..];
        var data = b[28..];
        var values = new double[data.Length / 8];
        for (int i = 0; i < values.Length; i++)
            values[i] = BinaryPrimitives.ReadDoubleLittleEndian(data[(8 * i)..]);
        return new SimObjectData(
            RequestId: BinaryPrimitives.ReadUInt32LittleEndian(b),
            ObjectId: BinaryPrimitives.ReadUInt32LittleEndian(b[4..]),
            DefineId: BinaryPrimitives.ReadUInt32LittleEndian(b[8..]),
            Values: values);
    }

    public static ServerException ParseException(ReadOnlySpan<byte> message)
    {
        var b = message[ServerHeaderSize..];
        return new ServerException(
            BinaryPrimitives.ReadUInt32LittleEndian(b),
            BinaryPrimitives.ReadUInt32LittleEndian(b[4..]),
            BinaryPrimitives.ReadUInt32LittleEndian(b[8..]));
    }

    private static string ReadString(ReadOnlySpan<byte> fixedField)
    {
        int end = fixedField.IndexOf((byte)0);
        return Encoding.Latin1.GetString(end < 0 ? fixedField : fixedField[..end]);
    }

    private sealed class PacketWriter
    {
        private readonly List<byte> _bytes = new(256);

        public PacketWriter(uint function, uint protocol)
        {
            U32(0);                          // size, set in Finish
            U32(protocol);
            U32(0xF0000000 | function);
            U32(0);                          // send id, set in Finish
        }

        public void Byte(byte v) => _bytes.Add(v);

        public void U32(uint v)
        {
            Span<byte> s = stackalloc byte[4];
            BinaryPrimitives.WriteUInt32LittleEndian(s, v);
            _bytes.AddRange(s.ToArray());
        }

        public void F32(float v)
        {
            Span<byte> s = stackalloc byte[4];
            BinaryPrimitives.WriteSingleLittleEndian(s, v);
            _bytes.AddRange(s.ToArray());
        }

        /// <summary>Fixed-length Latin-1 field; always keeps a terminating zero.</summary>
        public void String(string value, int length)
        {
            var text = Encoding.Latin1.GetBytes(value);
            int n = Math.Min(text.Length, length - (length > 3 ? 1 : 0));
            _bytes.AddRange(text.AsSpan(0, n).ToArray());
            _bytes.AddRange(new byte[length - n]);
        }

        public byte[] Finish(uint sendId)
        {
            var packet = _bytes.ToArray();
            BinaryPrimitives.WriteUInt32LittleEndian(packet, (uint)packet.Length);
            BinaryPrimitives.WriteUInt32LittleEndian(packet.AsSpan(12), sendId);
            return packet;
        }
    }
}

public sealed record OpenInfo(string ApplicationName, uint VersionMajor, uint VersionMinor,
    uint SimConnectVersionMajor, uint SimConnectVersionMinor);

public sealed record SimObjectData(uint RequestId, uint ObjectId, uint DefineId, double[] Values);

/// <summary>A request SimConnect rejected (e.g. an unknown simulation variable).</summary>
public sealed record ServerException(uint Exception, uint SendId, uint Index);
