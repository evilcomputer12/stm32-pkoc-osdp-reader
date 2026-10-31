using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using CredBench.Core.Models;
using CredBench.Core.Models.TechnologyDetails;
using CredBench.Core.Services;
using CredBench.Core.Services.GlobalPlatform;
using CredBench.Core.Services.Pkoc;

namespace CredBench.Core.ViewModels;

public partial class PkocViewModel : ObservableObject
{
    private readonly ISmartCardService _smartCardService;
    private readonly PkocCardProgrammer _programmer;
    private readonly ILocalizationService _localization;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasDetectedCredential))]
    [NotifyPropertyChangedFor(nameof(SignatureValidVisible))]
    [NotifyPropertyChangedFor(nameof(SignatureInvalidVisible))]
    [NotifyPropertyChangedFor(nameof(SignatureNotAvailableVisible))]
    private PKOCDetails? _detectedDetails;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(ProgramCardCommand))]
    [NotifyCanExecuteChangedFor(nameof(QueryCardContentCommand))]
    private bool _isProgramming;

    [ObservableProperty]
    private string? _programmingStatus;

    [ObservableProperty]
    private int _programmingProgress;

    [ObservableProperty]
    private int _programmingTotal;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CustomKeyError))]
    [NotifyCanExecuteChangedFor(nameof(ProgramCardCommand))]
    [NotifyCanExecuteChangedFor(nameof(QueryCardContentCommand))]
    private bool _useCustomKey;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CustomKeyError))]
    [NotifyCanExecuteChangedFor(nameof(ProgramCardCommand))]
    [NotifyCanExecuteChangedFor(nameof(QueryCardContentCommand))]
    private string _customKeyHex = "";

    /// <summary>
    /// Supplier gave separate ENC / MAC / DEK (KEK) keys. CustomKeyHex is then the ENC key.
    /// </summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CustomKeyError))]
    [NotifyCanExecuteChangedFor(nameof(ProgramCardCommand))]
    [NotifyCanExecuteChangedFor(nameof(QueryCardContentCommand))]
    private bool _useSeparateKeys;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CustomKeyError))]
    [NotifyCanExecuteChangedFor(nameof(ProgramCardCommand))]
    [NotifyCanExecuteChangedFor(nameof(QueryCardContentCommand))]
    private string _customMacKeyHex = "";

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(CustomKeyError))]
    [NotifyCanExecuteChangedFor(nameof(ProgramCardCommand))]
    [NotifyCanExecuteChangedFor(nameof(QueryCardContentCommand))]
    private string _customDekKeyHex = "";

    /// <summary>
    /// ISO14443 card answered, but SELECT of the PKOC AID did not succeed. The card may be an
    /// unfused JavaCard that can receive the applet; this is not confirmed until GP auth succeeds.
    /// </summary>
    [ObservableProperty]
    private bool _isPkocCandidate;

    /// <summary>Why PKOC was not detected on the last scan (e.g. SELECT status word).</summary>
    [ObservableProperty]
    private string? _pkocDiagnostic;

    /// <summary>True once an SCP02 secure channel has been established with the card.</summary>
    [ObservableProperty]
    private bool _isGpAuthenticated;

    /// <summary>
    /// User explicitly opted in to deleting an existing PKOC applet/package before installing.
    /// </summary>
    [ObservableProperty]
    private bool _replaceExisting;

    [ObservableProperty]
    private string? _errorDetails;

    /// <summary>
    /// Asked before deleting an existing applet. Set by the UI; when unset, replacement is refused.
    /// </summary>
    public Func<bool>? ConfirmReplaceExisting { get; set; }

    public string? CustomKeyError
    {
        get
        {
            if (!UseCustomKey)
                return null;
            TryBuildCustomKeys(out _, out var error);
            return error;
        }
    }

    private bool TryBuildCustomKeys(
        [System.Diagnostics.CodeAnalysis.NotNullWhen(true)] out GpKeySet? keys, out string? error)
    {
        keys = null;
        var prefix = UseSeparateKeys ? "ENC: " : "";
        if (!PkocCardProgrammer.TryParseGpKey(CustomKeyHex, out var enc, out error))
        {
            error = prefix + error;
            return false;
        }

        if (!UseSeparateKeys)
        {
            keys = GpKeySet.Single(enc);
            return true;
        }

        if (!PkocCardProgrammer.TryParseGpKey(CustomMacKeyHex, out var mac, out error))
        {
            error = "MAC: " + error;
            return false;
        }

        if (!PkocCardProgrammer.TryParseGpKey(CustomDekKeyHex, out var dek, out error))
        {
            error = "DEK: " + error;
            return false;
        }

        keys = new GpKeySet(enc, mac, dek);
        return true;
    }

    /// <summary>Use GlobalPlatformPro (gp.jar / gp.exe) instead of the built-in GP engine.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(GlobalPlatformProError))]
    [NotifyCanExecuteChangedFor(nameof(ProgramCardCommand))]
    [NotifyCanExecuteChangedFor(nameof(QueryCardContentCommand))]
    private bool _useGlobalPlatformPro;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(GlobalPlatformProError))]
    [NotifyCanExecuteChangedFor(nameof(ProgramCardCommand))]
    [NotifyCanExecuteChangedFor(nameof(QueryCardContentCommand))]
    private string _globalPlatformProPath = GlobalPlatformProClient.FindGp() ?? "";

    /// <summary>Locates java; replaceable in tests.</summary>
    internal Func<string?> FindJava { get; set; } = GlobalPlatformProClient.FindJava;

    /// <summary>Creates the gp client from (gpPath, javaPath); replaceable in tests.</summary>
    internal Func<string, string, GlobalPlatformProClient> CreateGpClient { get; set; } =
        (gpPath, javaPath) => new GlobalPlatformProClient(gpPath, javaPath);

    public string? GlobalPlatformProError
    {
        get
        {
            if (!UseGlobalPlatformPro)
                return null;
            if (string.IsNullOrWhiteSpace(GlobalPlatformProPath) || !File.Exists(GlobalPlatformProPath))
                return "Select gp.jar or gp.exe (GlobalPlatformPro).";
            if (FindJava() == null)
                return "Java 11+ not found. Set JAVA_HOME or install a JDK.";
            return null;
        }
    }

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(ProgramCardCommand))]
    [NotifyCanExecuteChangedFor(nameof(QueryCardContentCommand))]
    private string? _selectedReader;

    [ObservableProperty]
    private bool _programmingComplete;

    [ObservableProperty]
    private string? _errorMessage;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasCardContent))]
    private CardContent? _cardContent;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(QueryCardContentCommand))]
    private bool _isQuerying;

    [ObservableProperty]
    private string? _queryError;

    [ObservableProperty]
    private string? _queryErrorDetails;

    [ObservableProperty]
    private bool _showQueryErrorDetails;

    public bool HasCardContent => CardContent != null;

    public bool HasDetectedCredential => DetectedDetails != null;
    public bool SignatureValidVisible => DetectedDetails?.SignatureValid == true;
    public bool SignatureInvalidVisible => DetectedDetails?.SignatureValid == false;
    public bool SignatureNotAvailableVisible => DetectedDetails != null && DetectedDetails.SignatureValid is null;

    public PkocViewModel(
        ISmartCardService smartCardService,
        PkocCardProgrammer programmer,
        ILocalizationService localization)
    {
        _smartCardService = smartCardService;
        _programmer = programmer;
        _localization = localization;
    }

    /// <summary>
    /// Updates PKOC state from a scan. An ISO14443 card without the PKOC applet is only
    /// flagged as a candidate; the installer stays available regardless of detection.
    /// </summary>
    public void UpdateFromDetection(DetectionResult? result)
    {
        DetectedDetails = result?.PKOCDetails;
        IsPkocCandidate = result != null
            && result.HasTechnology(CardTechnology.ISO14443)
            && !result.HasTechnology(CardTechnology.PKOC);
        PkocDiagnostic = result?.Diagnostics.GetValueOrDefault(CardTechnology.PKOC);
        IsGpAuthenticated = false;
        CardContent = null;
        ProgrammingComplete = false;
    }

    private bool HasValidKeySelection() =>
        (!UseCustomKey || CustomKeyError == null) && GlobalPlatformProError == null;

    private GlobalPlatformProClient CreateGlobalPlatformProClient() =>
        CreateGpClient(GlobalPlatformProPath, FindJava()
            ?? throw new InvalidOperationException("Java 11+ not found."));

    private bool CanProgramCard() =>
        !IsProgramming && !string.IsNullOrEmpty(SelectedReader) && HasValidKeySelection();

    private bool CanQueryCardContent() =>
        !IsQuerying && !IsProgramming && !string.IsNullOrEmpty(SelectedReader) && HasValidKeySelection();

    [RelayCommand(CanExecute = nameof(CanProgramCard))]
    private async Task ProgramCardAsync()
    {
        if (string.IsNullOrEmpty(SelectedReader))
            return;

        if (ReplaceExisting && ConfirmReplaceExisting?.Invoke() != true)
        {
            ProgrammingStatus = _localization.GetString("PKOC_Programming_ReplaceCancelled");
            return;
        }

        try
        {
            IsProgramming = true;
            ProgrammingComplete = false;
            ErrorMessage = null;
            ErrorDetails = null;
            ProgrammingStatus = _localization.GetString("PKOC_Programming_SecureChannel");
            ProgrammingProgress = 0;
            ProgrammingTotal = 0;

            // The GP key only ever comes from the user's input or the built-in default
            // list — never from PKOC credential data read off the card.
            var gpKeys = UseCustomKey ? ParseCustomKeys() : null;
            var capData = PkocCardProgrammer.LoadEmbeddedCapFile();

            var loadProgress = new Progress<(int Current, int Total)>(p =>
            {
                ProgrammingProgress = p.Current;
                ProgrammingTotal = p.Total;
                ProgrammingStatus = _localization.GetString("PKOC_Programming_Loading", p.Current, p.Total);
            });

            var statusProgress = new Progress<string>(status =>
            {
                ProgrammingStatus = status;
            });

            var deleteExisting = ReplaceExisting;
            if (UseGlobalPlatformPro)
            {
                ProgrammingStatus = "Running GlobalPlatformPro...";
                var capPath = Path.Combine(Path.GetTempPath(), $"pkoc-{Guid.NewGuid():N}.cap");
                await File.WriteAllBytesAsync(capPath, capData);
                try
                {
                    await CreateGlobalPlatformProClient().InstallAsync(
                        SelectedReader, gpKeys, capPath, deleteExisting);
                }
                finally
                {
                    File.Delete(capPath);
                }
            }
            else
            {
                await Task.Run(() =>
                {
                    using var connection = _smartCardService.Connect(SelectedReader);
                    _programmer.ProgramCard(connection, capData, gpKeys,
                        deleteExisting, loadProgress, statusProgress);
                });
            }

            ProgrammingStatus = _localization.GetString("PKOC_Programming_Complete");
            ProgrammingComplete = true;
            IsGpAuthenticated = true;
        }
        catch (GlobalPlatformException gpEx)
        {
            ErrorMessage = gpEx.Message;
            ErrorDetails = gpEx.DiagnosticDetails;
            ProgrammingStatus = _localization.GetString("PKOC_Programming_Failed");
        }
        catch (Exception ex)
        {
            ErrorMessage = ex.Message;
            ProgrammingStatus = _localization.GetString("PKOC_Programming_Failed");
        }
        finally
        {
            IsProgramming = false;
        }
    }

    [RelayCommand]
    private void DismissError()
    {
        ErrorMessage = null;
        ErrorDetails = null;
        ProgrammingStatus = null;
    }

    [RelayCommand(CanExecute = nameof(CanQueryCardContent))]
    private async Task QueryCardContentAsync()
    {
        if (string.IsNullOrEmpty(SelectedReader))
            return;

        try
        {
            IsQuerying = true;
            QueryError = null;
            QueryErrorDetails = null;
            ShowQueryErrorDetails = false;
            CardContent = null;

            var gpKeys = UseCustomKey ? ParseCustomKeys() : null;

            var statusProgress = new Progress<string>(status =>
            {
                ProgrammingStatus = status;
            });

            var content = UseGlobalPlatformPro
                ? await CreateGlobalPlatformProClient().ListAsync(SelectedReader, gpKeys)
                : await Task.Run(() =>
                {
                    using var connection = _smartCardService.Connect(SelectedReader);
                    return _programmer.QueryCardContent(connection, gpKeys, statusProgress);
                });

            CardContent = content;
            IsGpAuthenticated = true;
            ProgrammingStatus = null;
        }
        catch (GlobalPlatformException gpEx)
        {
            QueryError = gpEx.Message;
            QueryErrorDetails = gpEx.DiagnosticDetails;
            ProgrammingStatus = null;
        }
        catch (Exception ex)
        {
            QueryError = ex.Message;
            ProgrammingStatus = null;
        }
        finally
        {
            IsQuerying = false;
        }
    }

    [RelayCommand]
    private void DismissQueryError()
    {
        QueryError = null;
        QueryErrorDetails = null;
        ShowQueryErrorDetails = false;
    }

    [RelayCommand]
    private void ToggleQueryErrorDetails()
    {
        ShowQueryErrorDetails = !ShowQueryErrorDetails;
    }

    private GpKeySet ParseCustomKeys() =>
        TryBuildCustomKeys(out var keys, out var error)
            ? keys
            : throw new ArgumentException(error);
}
