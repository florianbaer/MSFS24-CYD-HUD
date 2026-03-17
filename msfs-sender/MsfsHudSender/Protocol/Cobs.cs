namespace MsfsHudSender.Protocol;

/// <summary>Consistent Overhead Byte Stuffing (COBS) encoder/decoder.</summary>
public static class Cobs
{
    public static byte[] Encode(ReadOnlySpan<byte> data)
    {
        var output = new byte[data.Length + data.Length / 254 + 2];
        int codeIdx = 0;
        int writeIdx = 1;
        byte code = 1;

        foreach (byte b in data)
        {
            if (b == 0x00)
            {
                output[codeIdx] = code;
                codeIdx = writeIdx;
                writeIdx++;
                code = 1;
            }
            else
            {
                output[writeIdx] = b;
                writeIdx++;
                code++;
                if (code == 0xFF)
                {
                    output[codeIdx] = code;
                    codeIdx = writeIdx;
                    writeIdx++;
                    code = 1;
                }
            }
        }

        output[codeIdx] = code;
        return output[..writeIdx];
    }

    public static byte[] Decode(ReadOnlySpan<byte> data)
    {
        var output = new List<byte>();
        int idx = 0;

        while (idx < data.Length)
        {
            byte code = data[idx];
            if (code == 0)
                throw new InvalidOperationException("Zero byte in COBS data");
            idx++;

            for (int i = 1; i < code; i++)
            {
                if (idx >= data.Length)
                    throw new InvalidOperationException("Truncated COBS data");
                output.Add(data[idx]);
                idx++;
            }

            if (code < 0xFF && idx < data.Length)
                output.Add(0x00);
        }

        return output.ToArray();
    }
}
