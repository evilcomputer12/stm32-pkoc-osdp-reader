using System.Collections;
using System.IO.Ports;
using OSDP.Net;
using OSDP.Net.Connections;
using OSDP.Net.Tracing;
using OSDP.Net.Model.CommandData;

if (args.Length < 2) {
    Console.Error.WriteLine("Usage: Interop <RS485-port> <plain|SCBK-file|wrong:SCBK-file> [USB-diagnostics-port|-] [seconds] [--require-card] [--feedback-port=USB-port] [--feedback-buzzer] [--feedback-reject=index|color|buzzer]");
    return 2;
}
string port = args[0];
bool secure = args[1] != "plain";
bool wrongKey = args[1].StartsWith("wrong:");
byte[]? key = secure ? Convert.FromHexString(File.ReadAllText(wrongKey ? args[1][6..] : args[1]).Trim()) : null;
if (key is not null && key.Length != 16) throw new ArgumentException("SCBK must be 16 bytes");
if (wrongKey) key![0] ^= 1;
int seconds = args.Length > 3 ? int.Parse(args[3]) : 15;
bool requireCard = args.Skip(4).Contains("--require-card");
string? feedbackPort = args.Skip(4).FirstOrDefault(x => x.StartsWith("--feedback-port="))?[16..];
string? feedbackReject = args.Skip(4).FirstOrDefault(x => x.StartsWith("--feedback-reject="))?[18..];
bool feedbackBuzzer = args.Skip(4).Contains("--feedback-buzzer");
if (feedbackBuzzer && (feedbackPort is null || feedbackReject is not null))
    throw new ArgumentException("--feedback-buzzer requires --feedback-port and cannot be combined with --feedback-reject");
if (feedbackReject is not null && (feedbackPort is null || !new[] { "index", "color", "buzzer" }.Contains(feedbackReject)))
    throw new ArgumentException("--feedback-reject=index|color|buzzer requires --feedback-port");
if (feedbackPort is not null && (!secure || wrongKey)) throw new ArgumentException("Feedback test requires the correct Secure Channel key");
if (requireCard && (wrongKey || !secure || args.Length > 2 && args[2] != "-"))
    throw new ArgumentException("--require-card requires Secure Channel and '-' instead of a synthetic USB trigger");
var panel = new ControlPanel();
bool online = false, channel = false;
int disconnects = 0, events = 0, rxFrames = 0, txFrames = 0, cryptograms = 0;
bool correctEvent = false;
int authenticatedCardEvents = 0;
panel.ConnectionStatusChanged += (_, e) => {
    Console.WriteLine($"LINK online={e.IsConnected} secure={e.IsSecureChannelEstablished}");
    if (online && !e.IsConnected) Interlocked.Increment(ref disconnects);
    online = e.IsConnected; channel = e.IsSecureChannelEstablished;
};
panel.RawCardDataReplyReceived += (_, e) => {
    var data = e.RawCardData;
    byte[] bytes = new byte[(data.BitCount + 7) / 8];
    // OSDP.Net exposes MSB-first bit order; BitArray.CopyTo would reverse bits.
    for (int bit = 0; bit < data.BitCount; ++bit)
        if (data.Data[bit]) bytes[bit / 8] |= (byte)(0x80 >> (bit % 8));
    bool synthetic = data.ReaderNumber == 0 && data.BitCount == 256 &&
        bytes.SequenceEqual(Enumerable.Range(0, 32).Select(x => (byte)x));
    if (synthetic) {
        correctEvent = true; // A later real-card event must not erase this result.
        Console.WriteLine($"CARD reader={data.ReaderNumber} bits={data.BitCount} synthetic=True hex={Convert.ToHexString(bytes)}");
    } else {
        // A short fingerprint correlates presentations without logging the credential.
        string fingerprint = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(bytes))[..16];
        Console.WriteLine($"CARD reader={data.ReaderNumber} bits={data.BitCount} synthetic=False fingerprint={fingerprint} secure={channel}");
        if (channel && data.ReaderNumber == 0 && data.BitCount == 256)
            Interlocked.Increment(ref authenticatedCardEvents);
    }
    Interlocked.Increment(ref events);
};
var connection = new SerialPortOsdpConnection(port, 9600) { ReplyTimeout = TimeSpan.FromMilliseconds(200) };
Guid id = panel.StartConnection(connection, TimeSpan.FromMilliseconds(200), entry => {
    // Bus.cs uses Output for CP->PD and Input for PD->CP (the enum's XML
    // descriptions in this pinned revision are reversed).
    if (entry.Direction == TraceDirection.Output) Interlocked.Increment(ref txFrames);
    if (entry.Direction == TraceDirection.Input) {
        Interlocked.Increment(ref rxFrames);
        var frame = entry.Data;
        // SOM/address/length/control, then security block and reply code.
        if (frame.Length > 9 && (frame[4] & 8) != 0 && frame[6] == 0x12 &&
            5 + frame[5] < frame.Length && frame[5 + frame[5]] == 0x76)
            Interlocked.Increment(ref cryptograms);
    }
});
panel.AddDevice(id, 0, useCrc: true, useSecureChannel: secure, secureChannelKey: key);
try {
    var deadline = DateTime.UtcNow.AddSeconds(10);
    while ((!online || secure && !channel) && DateTime.UtcNow < deadline) await Task.Delay(100);
    if (wrongKey) {
        Console.WriteLine($"WRONG_KEY secure={channel} cryptograms={cryptograms} tx_frames={txFrames} rx_frames={rxFrames}");
        if (!channel && cryptograms > 0) { Console.WriteLine("PASS: peer answered the handshake but wrong-key SC was not established"); return 0; }
        Console.WriteLine(channel ? "FAIL: wrong key accepted" : "INCONCLUSIVE: no peer cryptogram; disconnected hardware is not a rejection test");
        return channel ? 1 : 2;
    }
    if (!online || secure && !channel) { Console.WriteLine($"FAIL: reader did not establish the requested connection; tx_frames={txFrames} rx_frames={rxFrames}"); return 1; }
    Console.WriteLine("IDENTITY " + await panel.IdReport(id, 0));
    Console.WriteLine("CAPABILITIES " + await panel.DeviceCapabilities(id, 0));
    Console.WriteLine("LOCAL_STATUS " + await panel.LocalStatus(id, 0));
    Console.WriteLine("READER_STATUS " + await panel.ReaderStatus(id, 0));
    if (feedbackPort is not null) {
        using var usb = new SerialPort(feedbackPort, 115200) { DtrEnable = true, ReadTimeout = 3000 };
        usb.Open(); await Task.Delay(150);
        string Feedback() { usb.DiscardInBuffer(); usb.Write("f"); return usb.ReadLine().Trim(); }
        void CheckPin(bool on) {
            string state = Feedback(); Console.WriteLine("FEEDBACK " + state);
            if (!state.Contains($"LED_PC13={(on ? 1 : 0)} pin={(on ? 0 : 1)}"))
                throw new Exception("Physical LED state mismatch");
        }
        void CheckBuzzer(bool on) {
            string state = Feedback(); Console.WriteLine("FEEDBACK " + state);
            if (!state.Contains($"buzzer_PB0={(on ? 1 : 0)} buzzer_pin={(on ? 1 : 0)} buzzer_enabled=1"))
                throw new Exception("Physical buzzer state/enabled output mismatch");
        }
        ReaderLedControls Led(byte led, LedColor color, bool temporary = false) => new(new[] {
            new ReaderLedControl(0, led,
                temporary ? TemporaryReaderControlCode.SetTemporaryAndStartTimer : TemporaryReaderControlCode.CancelAnyTemporaryAndDisplayPermanent,
                1, 0, color, LedColor.Black, 50,
                temporary ? PermanentReaderControlCode.Nop : PermanentReaderControlCode.SetPermanentState,
                1, 0, color, LedColor.Black)
        });
        async Task ExpectNak(Func<Task<bool>> operation) {
            try { await operation(); }
            catch (NackReplyException e) when ((byte)e.Reply.ErrorCode == 0x09) {
                Console.WriteLine("FEEDBACK unsupported output correctly NAKed (record invalid 0x09)"); return;
            }
            throw new Exception("Unsupported output did not produce NAK");
        }
        if (feedbackReject is not null) {
            // OSDP.Net resets its DeviceProxy on NAK 0x09. Run each negative
            // case separately, without queueing commands into that old proxy.
            if (feedbackReject == "buzzer")
                await ExpectNak(() => panel.ReaderBuzzerControl(id,0,new ReaderBuzzerControl(0,ToneCode.Default,1,1,1)));
            else
                await ExpectNak(() => panel.ReaderLedControl(id,0,Led(feedbackReject == "index" ? (byte)1 : (byte)0,
                    feedbackReject == "color" ? LedColor.Red : LedColor.Green)));
            CheckPin(false);
            await Task.Delay(2000);
            var reconnectDeadline = DateTime.UtcNow.AddSeconds(10);
            while ((!online || !channel) && DateTime.UtcNow < reconnectDeadline) await Task.Delay(100);
            if (!online || !channel || disconnects > 1) throw new Exception("Secure Channel did not recover after expected NAK reset");
            Console.WriteLine($"FEEDBACK rejection PASS: {feedbackReject}; recovered secure={channel}, expected_resets={disconnects}");
            return 0;
        }
        if (feedbackBuzzer) {
            Console.WriteLine("FEEDBACK buzzer test starts in 5 seconds: two 300 ms beeps");
            await Task.Delay(5000);
            try {
                if (!await panel.ReaderBuzzerControl(id,0,new ReaderBuzzerControl(0,ToneCode.Default,3,3,2)))
                    throw new Exception("Buzzer pattern was not ACKed");
                CheckBuzzer(true);
                await Task.Delay(1350); CheckBuzzer(false);
            } finally {
                if (online && channel && !await panel.ReaderBuzzerControl(id,0,new ReaderBuzzerControl(0,ToneCode.Off,0,0,0)))
                    throw new Exception("Buzzer OFF was not ACKed");
            }
            CheckBuzzer(false);
            Console.WriteLine("FEEDBACK PASS: secure buzzer pattern, timer expiry, explicit OFF and PB0 readback; audible result requires user observation");
        } else {
            Console.WriteLine("FEEDBACK visual sequence starts in 5 seconds: ON 5s, OFF 3s, timed ON 5s, OFF");
            await Task.Delay(5000);
            if (!await panel.ReaderLedControl(id,0,Led(0,LedColor.Green))) throw new Exception("LED ON was not ACKed");
            CheckPin(true); await Task.Delay(5000);
            if (!await panel.ReaderLedControl(id,0,Led(0,LedColor.Black))) throw new Exception("LED OFF was not ACKed");
            CheckPin(false); await Task.Delay(3000);
            if (!await panel.ReaderLedControl(id,0,Led(0,LedColor.Green,true))) throw new Exception("Timed LED was not ACKed");
            CheckPin(true); await Task.Delay(5200); CheckPin(false);
            Console.WriteLine("FEEDBACK PASS: secure LED on/off/5-second timer and GPIO readback");
        }
    }
    if (args.Length > 2 && args[2] != "-") {
        using var usb = new SerialPort(args[2], 115200) { DtrEnable = true, ReadTimeout = 2000 };
        usb.Open(); await Task.Delay(150);
        usb.Write("s");
        Console.WriteLine("USB " + usb.ReadLine().Trim());
    }
    await Task.Delay(TimeSpan.FromSeconds(seconds));
    Console.WriteLine($"RESULT online={online} secure={channel} disconnects={disconnects} events={events} synthetic_match={correctEvent} card_events={authenticatedCardEvents}");
    return online && (!secure || channel) && disconnects == 0 &&
        (!requireCard || authenticatedCardEvents > 0) &&
        (args.Length < 3 || args[2] == "-" || correctEvent) ? 0 : 1;
}
finally { await panel.StopConnection(id); }
