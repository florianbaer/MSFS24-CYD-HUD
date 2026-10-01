namespace MsfsHudSender.Protocol;

/// <summary>CRC-8 checksum: polynomial 0x31, init 0x00, MSB first (no reflection, no final XOR).</summary>
public static class Crc8
{
    private static readonly byte[] Table = BuildTable();

    private static byte[] BuildTable()
    {
        var table = new byte[256];
        for (int i = 0; i < 256; i++)
        {
            int crc = i;
            for (int j = 0; j < 8; j++)
            {
                crc = (crc & 0x80) != 0
                    ? ((crc << 1) ^ 0x31) & 0xFF
                    : (crc << 1) & 0xFF;
            }
            table[i] = (byte)crc;
        }
        return table;
    }

    public static byte Compute(ReadOnlySpan<byte> data)
    {
        byte crc = 0x00;
        foreach (byte b in data)
            crc = Table[crc ^ b];
        return crc;
    }
}
