using Android.App;
using Android.Nfc;
using Android.OS;
using Android.Views;
using Android.Widget;
using CredBench.Core.Services;
using CredBench.Core.Services.CardDetectors;
using CredBench.Core.Services.GlobalPlatform;
using CredBench.Core.Services.Pkoc;

namespace CredBench.Droid;

/// <summary>
/// Single-screen PKOC tool. Tapping a card with no action selected only scans it (no
/// authentication). "Analyze" and "Install" each run exactly once per button press, so a
/// card left on the phone never receives repeated authentication attempts.
/// </summary>
[Activity(Label = "@string/app_name", MainLauncher = true,
    ScreenOrientation = Android.Content.PM.ScreenOrientation.Portrait)]
public class MainActivity : Activity, NfcAdapter.IReaderCallback
{
    private enum PendingAction { None, Analyze, Install }

    // Generous: INSTALL [for install] and LOAD of the last block can take a few seconds on JCOP,
    // and some cards deliberately delay INITIALIZE UPDATE
    private const int TransceiveTimeoutMs = 30_000;

    private const int SettleDelayMs = 1_000;

    private static readonly byte[] SelectIsdCommand =
        Convert.FromHexString("00A4040008A00000015100000000");
    private static readonly byte[] GetCplcCommand = Convert.FromHexString("80CA9F7F00");

    private readonly PkocCardProgrammer _programmer = new();
    private readonly PKOCDetector _pkocDetector = new();

    private const string LogTag = "CredBench";

    private NfcAdapter? _nfcAdapter;
    private PendingAction _pendingAction;
    private int _busy;
    private string? _cardUid;
    private IsoDepConnection? _heldConnection;

    private TextView _statusText = null!;
    private ProgressBar _progressBar = null!;
    private RadioGroup _keyModeGroup = null!;
    private EditText _encKey = null!;
    private EditText _macKey = null!;
    private EditText _dekKey = null!;
    private TextView _keyError = null!;
    private CheckBox _replaceExisting = null!;
    private TextView _replaceWarning = null!;
    private Button _analyzeButton = null!;
    private Button _installButton = null!;
    private TextView _logText = null!;

    protected override void OnCreate(Bundle? savedInstanceState)
    {
        base.OnCreate(savedInstanceState);
        SetContentView(Resource.Layout.activity_main);

        // Keep typed keys out of screenshots and the recent-apps thumbnail
        Window?.SetFlags(WindowManagerFlags.Secure, WindowManagerFlags.Secure);

        _statusText = FindViewById<TextView>(Resource.Id.statusText)!;
        _progressBar = FindViewById<ProgressBar>(Resource.Id.progressBar)!;
        _keyModeGroup = FindViewById<RadioGroup>(Resource.Id.keyModeGroup)!;
        _encKey = FindViewById<EditText>(Resource.Id.encKey)!;
        _macKey = FindViewById<EditText>(Resource.Id.macKey)!;
        _dekKey = FindViewById<EditText>(Resource.Id.dekKey)!;
        _keyError = FindViewById<TextView>(Resource.Id.keyError)!;
        _replaceExisting = FindViewById<CheckBox>(Resource.Id.replaceExisting)!;
        _replaceWarning = FindViewById<TextView>(Resource.Id.replaceWarning)!;
        _analyzeButton = FindViewById<Button>(Resource.Id.analyzeButton)!;
        _installButton = FindViewById<Button>(Resource.Id.installButton)!;
        _logText = FindViewById<TextView>(Resource.Id.logText)!;

        _keyModeGroup.CheckedChange += (_, _) => UpdateKeyFields();
        _replaceExisting.CheckedChange += (_, e) =>
            _replaceWarning.Visibility = e.IsChecked ? ViewStates.Visible : ViewStates.Gone;
        _analyzeButton.Click += (_, _) => Arm(PendingAction.Analyze);
        _installButton.Click += (_, _) => OnInstallClicked();

        UpdateKeyFields();

        _nfcAdapter = NfcAdapter.GetDefaultAdapter(this);
        if (_nfcAdapter == null)
        {
            SetStatus(GetString(Resource.String.nfc_missing));
            _analyzeButton.Enabled = false;
            _installButton.Enabled = false;
        }
    }

    protected override void OnResume()
    {
        base.OnResume();
        if (_nfcAdapter == null)
            return;

        if (!_nfcAdapter.IsEnabled)
        {
            SetStatus(GetString(Resource.String.nfc_disabled));
            return;
        }

        // Long presence-check delay: Android's background presence checks must not interleave
        // with a multi-second LOAD sequence.
        var options = new Bundle();
        options.PutInt(NfcAdapter.ExtraReaderPresenceCheckDelay, 5000);
        _nfcAdapter.EnableReaderMode(this, this,
            NfcReaderFlags.NfcA | NfcReaderFlags.NfcB | NfcReaderFlags.SkipNdefCheck,
            options);
    }

    protected override void OnPause()
    {
        base.OnPause();
        _nfcAdapter?.DisableReaderMode(this);
        Interlocked.Exchange(ref _heldConnection, null)?.Dispose();
    }

    // ── Key input ──────────────────────────────────────────────────────

    private void UpdateKeyFields()
    {
        var mode = _keyModeGroup.CheckedRadioButtonId;
        _encKey.Visibility = mode == Resource.Id.keyDefault ? ViewStates.Gone : ViewStates.Visible;
        _encKey.Hint = GetString(mode == Resource.Id.keySeparate
            ? Resource.String.key_enc_hint
            : Resource.String.key_single_hint);
        var separate = mode == Resource.Id.keySeparate ? ViewStates.Visible : ViewStates.Gone;
        _macKey.Visibility = separate;
        _dekKey.Visibility = separate;
        _keyError.Visibility = ViewStates.Gone;
    }

    /// <summary>Returns null for "use default key discovery"; throws on invalid input.</summary>
    private GpKeySet? ReadKeys()
    {
        var mode = _keyModeGroup.CheckedRadioButtonId;
        if (mode == Resource.Id.keyDefault)
            return null;

        var prefix = mode == Resource.Id.keySeparate ? "ENC: " : "";
        var enc = ParseKey(_encKey.Text, prefix);
        if (mode == Resource.Id.keySingle)
            return GpKeySet.Single(enc);

        return new GpKeySet(enc, ParseKey(_macKey.Text, "MAC: "), ParseKey(_dekKey.Text, "DEK: "));
    }

    private static byte[] ParseKey(string? hex, string label) =>
        PkocCardProgrammer.TryParseGpKey(hex, out var key, out var error)
            ? key
            : throw new ArgumentException(label + error);

    // ── Actions ────────────────────────────────────────────────────────

    private void OnInstallClicked()
    {
        if (!_replaceExisting.Checked)
        {
            Arm(PendingAction.Install);
            return;
        }

        new AlertDialog.Builder(this)
            .SetTitle(Resource.String.replace_confirm_title)!
            .SetMessage(Resource.String.replace_confirm_text)!
            .SetPositiveButton(Resource.String.replace_confirm_yes, (_, _) => Arm(PendingAction.Install))!
            .SetNegativeButton(Resource.String.cancel, (_, _) => { })!
            .Show();
    }

    private void Arm(PendingAction action)
    {
        try
        {
            ReadKeys(); // validate now so the user sees mistakes before tapping the card
            _keyError.Visibility = ViewStates.Gone;
        }
        catch (ArgumentException ex)
        {
            _keyError.Text = ex.Message;
            _keyError.Visibility = ViewStates.Visible;
            return;
        }

        _pendingAction = action;
        SetStatus(GetString(action == PendingAction.Install
            ? Resource.String.status_waiting_install
            : Resource.String.status_waiting_analyze));

        // A card already lying on the phone is not reported again by Android, so run on the
        // connection kept open since it was scanned. If it has gone, the run fails before
        // anything is sent and the action stays armed for the next tap.
        if (_heldConnection is { } held)
            Task.Run(() => Run(held, newTap: false));
    }

    public void OnTagDiscovered(Tag? tag)
    {
        if (tag == null || Volatile.Read(ref _busy) == 1)
            return;

        IsoDepConnection connection;
        try
        {
            connection = new IsoDepConnection(tag, TransceiveTimeoutMs);
        }
        catch (Exception ex) when (ex is Java.IO.IOException or Java.Lang.SecurityException)
        {
            Log($"Could not connect to card: {ex.Message}");
            return;
        }

        // Keep the connection open while the card stays on the phone, so a later button
        // press can use it without lifting the card.
        Interlocked.Exchange(ref _heldConnection, connection)?.Dispose();
        Run(connection, newTap: true);
    }

    private void Run(IsoDepConnection connection, bool newTap)
    {
        if (Interlocked.Exchange(ref _busy, 1) == 1)
            return;

        // Consume the action up front: it runs once even if the card is tapped again.
        var action = _pendingAction;
        _pendingAction = PendingAction.None;

        GpKeySet? keys = null;
        var replace = false;
        Exception? keyError = null;
        using var latch = new ManualResetEventSlim();
        RunOnUiThread(() =>
        {
            try
            {
                if (action != PendingAction.None)
                    keys = ReadKeys();
                replace = _replaceExisting.Checked;
                SetBusyUi(true);
            }
            catch (Exception ex)
            {
                keyError = ex;
            }
            finally
            {
                latch.Set();
            }
        });
        latch.Wait();

        connection.BeginRun();
        try
        {
            if (keyError != null)
                throw keyError;

            // Discovery fires on first contact, while the card is still being moved into place
            if (newTap && action != PendingAction.None)
            {
                SetStatus("Card detected — keep it still…");
                Thread.Sleep(SettleDelayMs);
            }

            _cardUid = connection.GetUID() ?? "?";
            Log("");
            Log($"──── {DateTime.Now:HH:mm:ss}  Card {_cardUid}  {(action == PendingAction.None ? "Scan" : action)} ────");
            Log($"Historical bytes: {connection.GetATR() ?? "(none)"}");
            Log($"Max APDU: {connection.MaxTransceiveLength} bytes" +
                (connection.ExtendedLengthSupported ? ", extended length" : ""));

            var pkocInstalled = ScanPkoc(connection);
            ReadCplc(connection);

            switch (action)
            {
                case PendingAction.Analyze:
                    Analyze(connection, keys);
                    break;
                case PendingAction.Install:
                    Install(connection, keys, replace, pkocInstalled);
                    break;
                default:
                    Finish(pkocInstalled
                        ? "PKOC applet installed."
                        : "PKOC candidate — applet not installed. Choose Analyze or Install.");
                    break;
            }
        }
        catch (GlobalPlatformException ex)
        {
            Log($"ERROR: {ex.Message}");
            if (ex.DiagnosticDetails != null)
                Log($"Details: {ex.DiagnosticDetails}");

            var wrongKey = ex.Message.Contains("wrong GP key", StringComparison.OrdinalIgnoreCase)
                || ex.Message.StartsWith("Key discovery failed", StringComparison.Ordinal);
            Finish(wrongKey
                ? "these keys do not match this card. Nothing was changed and no authentication " +
                  "attempt was used. Each card can have its own keys — ask the supplier which " +
                  "keys belong to this UID."
                : $"failed: {ex.Message}");
        }
        catch (Exception ex) when (IsLinkFailure(ex))
        {
            // The connection is unusable now; a fresh tap opens a new one
            if (Interlocked.CompareExchange(ref _heldConnection, null, connection) == connection)
                connection.Dispose();

            Log($"ERROR: {ex.Message}");
            if (action != PendingAction.None && !connection.AuthenticationStarted)
            {
                // Nothing reached the card's authentication, so retrying is safe
                _pendingAction = action;
                Finish("lost contact with the card before anything was sent. Put it on the phone again.");
            }
            else
            {
                Finish($"failed: {ex.Message}");
            }
        }
        catch (Exception ex)
        {
            Log($"ERROR: {ex.Message}");
            Finish($"failed: {ex.Message}");
        }
        finally
        {
            RunOnUiThread(() => SetBusyUi(false));
            Interlocked.Exchange(ref _busy, 0);
        }
    }

    private static bool IsLinkFailure(Exception ex) =>
        ex is Java.IO.IOException or Java.Lang.SecurityException
        || ex.InnerException is Java.IO.IOException or Java.Lang.SecurityException;

    /// <summary>Strict PKOC detection — same detector as the desktop app.</summary>
    private bool ScanPkoc(ICardConnection connection)
    {
        var (detected, details, _) = _pkocDetector.Detect(connection);
        Log(detected ? $"PKOC: {details}" : $"PKOC: not detected — {details}");
        return detected;
    }

    /// <summary>Unauthenticated chip info from the card manager (read-only).</summary>
    private void ReadCplc(ICardConnection connection)
    {
        var select = connection.Transmit(SelectIsdCommand);
        if (!IsOk(select))
        {
            Log($"Card manager (ISD) not selectable: SW={Sw(select)}");
            return;
        }

        var cplc = connection.Transmit(GetCplcCommand);
        if (!IsOk(cplc) || cplc.Length < 3 + 10)
        {
            Log($"CPLC not available: SW={Sw(cplc)}");
            return;
        }

        var d = cplc[3..^2]; // skip 9F 7F len
        Log($"CPLC: IC fabricator {d[0]:X2}{d[1]:X2}, IC type {d[2]:X2}{d[3]:X2}, OS {d[4]:X2}{d[5]:X2}");
    }

    private void Analyze(ICardConnection connection, GpKeySet? keys)
    {
        SetStatus("Authenticating with card manager…");
        var content = _programmer.QueryCardContent(connection, keys, new UiProgress<string>(this, SetStatus));

        Log("GlobalPlatform authentication succeeded.");
        if (content.FreePersistentMemory is { } free)
            Log($"Free persistent memory: {free:N0} bytes");
        foreach (var p in content.Packages)
            Log($"Package {p.AidHex} — {p.LifecycleDescription}");
        foreach (var a in content.Applets)
            Log($"Applet  {a.AidHex} — {a.LifecycleDescription}");

        Finish(content.HasPkocApplet
            ? "Authenticated. PKOC applet is installed."
            : "Authenticated. Card is ready for PKOC installation.");
    }

    private void Install(ICardConnection connection, GpKeySet? keys, bool replace, bool pkocInstalled)
    {
        if (pkocInstalled && !replace)
        {
            Finish("PKOC is already installed. Tick 'Replace existing' to reinstall. Nothing was changed.");
            return;
        }

        var capData = PkocCardProgrammer.LoadEmbeddedCapFile();
        Log($"PKOC CAP: {capData.Length:N0} bytes");

        var loadProgress = new UiProgress<(int Current, int Total)>(this, p =>
        {
            _progressBar.Indeterminate = false;
            _progressBar.Max = p.Total;
            _progressBar.Progress = p.Current;
            _statusText.Text = $"Loading applet… block {p.Current}/{p.Total} — keep the card still";
        });

        _programmer.ProgramCard(connection, capData, keys, replace, loadProgress,
            new UiProgress<string>(this, SetStatus));
        Log("Applet loaded and installed.");

        // Confirm the same way the desktop app detects PKOC
        if (ScanPkoc(connection))
            Finish("SUCCESS — PKOC applet installed and responding.");
        else
            Finish("installed, but SELECT of the PKOC applet failed — see log.");
    }

    // ── UI helpers ─────────────────────────────────────────────────────

    private void SetBusyUi(bool busy)
    {
        _analyzeButton.Enabled = !busy;
        _installButton.Enabled = !busy;
        _progressBar.Visibility = busy ? ViewStates.Visible : ViewStates.Gone;
        _progressBar.Indeterminate = true;
        if (busy)
            Window?.AddFlags(WindowManagerFlags.KeepScreenOn);
        else
            Window?.ClearFlags(WindowManagerFlags.KeepScreenOn);
    }

    private void SetStatus(string text) => RunOnUiThread(() => _statusText.Text = text);

    /// <summary>Final outcome of a run, labelled with the card's UID so two cards are never confused.</summary>
    private void Finish(string text)
    {
        var line = $"Card {_cardUid ?? "?"}: {text}";
        Log("RESULT: " + text);
        SetStatus(line);
    }

    /// <summary>Appends to the on-screen log and logcat (tag CredBench). Never receives static keys.</summary>
    private void Log(string line)
    {
        Android.Util.Log.Info(LogTag, line);
        RunOnUiThread(() => _logText.Append(line + "\n"));
    }

    private static bool IsOk(byte[] r) => r.Length >= 2 && r[^2] == 0x90 && r[^1] == 0x00;

    private static string Sw(byte[] r) => r.Length >= 2 ? $"{r[^2]:X2}{r[^1]:X2}" : "(no response)";

    /// <summary>Marshals Core progress callbacks (raised on the NFC thread) to the UI thread.</summary>
    private sealed class UiProgress<T>(Activity activity, Action<T> handler) : IProgress<T>
    {
        public void Report(T value) => activity.RunOnUiThread(() => handler(value));
    }
}
