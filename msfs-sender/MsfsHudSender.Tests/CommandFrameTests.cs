using System.Text;
using MsfsHudSender.Protocol;

namespace MsfsHudSender.Tests;

/// <summary>Command frames from the display (tests/proto/decoder_test.cpp pins the same bytes).</summary>
public class CommandFrameTests
{
    [Theory]
    [InlineData(HudCommand.ApMaster, "00042001ed00")]
    [InlineData(HudCommand.ApAltitudeHold, "000420038f00")]
    [InlineData(HudCommand.AltitudeDec, "0004200a0700")]
    public void FramesMatchTheFirmware(HudCommand command, string hex) =>
        Assert.Equal(Convert.FromHexString(hex), FrameBuilder.FrameCommand(command));

    [Fact]
    public void ReaderSkipsTheDisplaysTextLog()
    {
        // The display prints status lines on the same serial port as its command frames
        var stream = new List<byte>();
        stream.AddRange(Encoding.ASCII.GetBytes("HUD ready (firmware 1.1.0)\r\nTelemetry: 1600 frames in 10 s\r\n"));
        stream.AddRange(FrameBuilder.FrameCommand(HudCommand.ApMaster));
        stream.AddRange(Encoding.ASCII.GetBytes("HUDCFG OK\r\n"));
        stream.AddRange(FrameBuilder.FrameCommand(HudCommand.HeadingBugInc));

        var reader = new FrameReader();
        // Arrives in arbitrary chunks, like serial reads do
        var frames = new List<Frame>();
        for (int i = 0; i < stream.Count; i += 7)
            frames.AddRange(reader.Feed(stream.Skip(i).Take(7).ToArray()));

        Assert.Equal(2, frames.Count);
        Assert.All(frames, f => Assert.Equal(Messages.MsgCommand, f.Type));
        Assert.Equal((byte)HudCommand.ApMaster, frames[0].Payload.Single());
        Assert.Equal((byte)HudCommand.HeadingBugInc, frames[1].Payload.Single());
    }

    [Fact]
    public void CorruptFramesAreDropped()
    {
        var frame = FrameBuilder.FrameCommand(HudCommand.ApMaster);
        frame[3] ^= 0x40;  // flip a bit in the command: CRC no longer matches
        Assert.Empty(new FrameReader().Feed(frame));
    }
}
