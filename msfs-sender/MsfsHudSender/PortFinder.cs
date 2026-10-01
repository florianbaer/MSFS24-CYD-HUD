using System.Globalization;
using System.IO.Ports;
using Microsoft.Win32;

namespace MsfsHudSender;

/// <summary>Finds the serial port of the display by the USB-serial bridge it uses.</summary>
public static class PortFinder
{
    /// <summary>USB-to-serial chips found on ESP32-2432S0xx ("Cheap Yellow Display") boards.</summary>
    public static readonly IReadOnlyList<(ushort Vid, ushort Pid, string Chip)> KnownBridges =
    [
        (0x1A86, 0x7523, "CH340"),
        (0x1A86, 0x55D4, "CH9102"),
        (0x10C4, 0xEA60, "CP210x"),
    ];

    /// <summary>
    /// The COM port of a connected display, or null if none is plugged in.
    /// Windows only: reads the port names the USB drivers registered for the known bridges.
    /// </summary>
    public static string? FindDisplayPort()
    {
        if (!OperatingSystem.IsWindows()) return null;
        var present = new HashSet<string>(SerialPort.GetPortNames(), StringComparer.OrdinalIgnoreCase);
        return Candidates().FirstOrDefault(present.Contains);
    }

    [System.Runtime.Versioning.SupportedOSPlatform("windows")]
    private static IEnumerable<string> Candidates()
    {
        using var usb = Registry.LocalMachine.OpenSubKey(@"SYSTEM\CurrentControlSet\Enum\USB");
        if (usb is null) yield break;
        foreach (var deviceKey in usb.GetSubKeyNames())
        {
            if (!TryParseHardwareId(deviceKey, out var vid, out var pid)) continue;
            if (!KnownBridges.Any(b => b.Vid == vid && b.Pid == pid)) continue;

            using var device = usb.OpenSubKey(deviceKey);
            if (device is null) continue;
            foreach (var instance in device.GetSubKeyNames())
            {
                using var parameters = device.OpenSubKey($@"{instance}\Device Parameters");
                if (parameters?.GetValue("PortName") is string port) yield return port;
            }
        }
    }

    /// <summary>Parses a USB device key such as <c>VID_1A86&amp;PID_7523</c> (case-insensitive).</summary>
    public static bool TryParseHardwareId(string deviceKey, out ushort vid, out ushort pid)
    {
        vid = pid = 0;
        var parts = deviceKey.ToUpperInvariant().Split('&');
        if (parts.Length < 2 || !parts[0].StartsWith("VID_") || !parts[1].StartsWith("PID_")) return false;
        return ushort.TryParse(parts[0].AsSpan(4), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out vid)
            && ushort.TryParse(parts[1].AsSpan(4), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out pid);
    }
}
