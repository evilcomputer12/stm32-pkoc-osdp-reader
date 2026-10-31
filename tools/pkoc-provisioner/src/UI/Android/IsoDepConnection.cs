using Android.Nfc;
using Android.Nfc.Tech;
using CredBench.Core.Services;

namespace CredBench.Droid;

/// <summary>
/// <see cref="ICardConnection"/> over Android NFC (ISO14443-4 / IsoDep), so the Core
/// GlobalPlatform and PKOC code runs unchanged on a phone.
/// </summary>
public sealed class IsoDepConnection : ICardConnection
{
    private readonly Tag _tag;
    private readonly IsoDep _isoDep;

    public IsoDepConnection(Tag tag, int timeoutMs)
    {
        _tag = tag;
        _isoDep = IsoDep.Get(tag)
            ?? throw new InvalidOperationException("Card does not support ISO14443-4 (IsoDep).");
        _isoDep.Connect();
        _isoDep.Timeout = timeoutMs;
    }

    public string ReaderName => "Android NFC";

    public int MaxTransceiveLength => _isoDep.MaxTransceiveLength;

    public bool ExtendedLengthSupported => _isoDep.IsExtendedLengthApduSupported;

    /// <summary>
    /// True once INITIALIZE UPDATE has been sent, i.e. a GlobalPlatform authentication has
    /// started. Used to decide whether an interrupted action may safely be retried.
    /// </summary>
    public bool AuthenticationStarted { get; private set; }

    /// <summary>The connection is reused across actions; reset per-action state.</summary>
    public void BeginRun() => AuthenticationStarted = false;

    /// <summary>ATS historical bytes (NFC-A) or ATTRIB hi-layer response (NFC-B).</summary>
    public string? GetATR()
    {
        var bytes = _isoDep.GetHistoricalBytes() ?? _isoDep.GetHiLayerResponse();
        return bytes is { Length: > 0 } ? Convert.ToHexString(bytes) : null;
    }

    public string? GetUID()
    {
        var id = _tag.GetId();
        return id is { Length: > 0 } ? Convert.ToHexString(id) : null;
    }

    public string? GetProtocol() => "ISO14443-4 (NFC)";

    public byte[] Transmit(byte[] command)
    {
        if (command.Length > 1 && command[1] == 0x50)
            AuthenticationStarted = true;

        var started = System.Diagnostics.Stopwatch.StartNew();
        try
        {
            return _isoDep.Transceive(command) ?? [];
        }
        catch (TagLostException ex) when (started.ElapsedMilliseconds >= _isoDep.Timeout - 500)
        {
            // Android reports a timeout as a lost tag; the card is usually still in place
            throw new InvalidOperationException(
                $"The card did not answer {CommandName(command)} within {_isoDep.Timeout / 1000} s. " +
                "This is the card not responding, not the card moving.", ex);
        }
        catch (TagLostException ex)
        {
            throw new InvalidOperationException(
                "Card connection lost — the card moved away from the phone. Hold it still and try again.", ex);
        }
        catch (Java.IO.IOException ex)
        {
            throw new InvalidOperationException(
                $"NFC transmit failed ({ex.Message}). Hold the card still against the phone and try again.", ex);
        }
    }

    private static string CommandName(byte[] command) => command.Length < 2 ? "the command" : command[1] switch
    {
        0xA4 => "SELECT",
        0x50 => "INITIALIZE UPDATE",
        0x82 => "EXTERNAL AUTHENTICATE",
        0xE4 => "DELETE",
        0xE6 => "INSTALL",
        0xE8 => "LOAD",
        0xF2 => "GET STATUS",
        _ => $"INS {command[1]:X2}",
    };

    public void Dispose()
    {
        try
        {
            _isoDep.Close();
        }
        catch (Java.Lang.Exception)
        {
            // Card already gone or reset (IOException, or SecurityException "tag out of date")
        }
    }
}
