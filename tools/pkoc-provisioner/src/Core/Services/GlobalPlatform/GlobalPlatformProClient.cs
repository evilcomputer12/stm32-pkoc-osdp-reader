using System.Diagnostics;
using System.Text.RegularExpressions;

namespace CredBench.Core.Services.GlobalPlatform;

/// <summary>
/// Runs GlobalPlatformPro (https://javacard.pro/globalplatform/) as an alternative card-management
/// engine. Both gp.jar and the gp.exe launcher are started with <c>java -jar</c>, because the
/// launcher does not find a JDK that is only set via JAVA_HOME.
/// Keys are passed on the command line, so they are visible to other processes of the same user
/// while gp runs.
/// </summary>
public partial class GlobalPlatformProClient
{
    /// <summary>Runs a process and returns its exit code and combined stdout/stderr.</summary>
    public delegate Task<(int ExitCode, string Output)> ProcessRunner(
        ProcessStartInfo startInfo, CancellationToken cancellationToken);

    private static readonly TimeSpan Timeout = TimeSpan.FromMinutes(3);

    private readonly string _gpPath;
    private readonly string _javaPath;
    private readonly ProcessRunner _runProcess;

    public GlobalPlatformProClient(string gpPath, string javaPath, ProcessRunner? runProcess = null)
    {
        _gpPath = gpPath;
        _javaPath = javaPath;
        _runProcess = runProcess ?? RunProcessAsync;
    }

    /// <summary>
    /// Finds java.exe: JAVA_HOME, then PATH, then the JDK installed for Android builds.
    /// </summary>
    public static string? FindJava()
    {
        var exe = OperatingSystem.IsWindows() ? "java.exe" : "java";
        IEnumerable<string?> candidates =
        [
            Environment.GetEnvironmentVariable("JAVA_HOME") is { } home ? Path.Combine(home, "bin", exe) : null,
            .. (Environment.GetEnvironmentVariable("PATH") ?? "")
                .Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries)
                .Select(dir => Path.Combine(dir, exe)),
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "Android", "jdk", "bin", exe),
        ];
        return candidates.FirstOrDefault(File.Exists);
    }

    /// <summary>Looks for gp.jar / gp.exe in the user's Downloads folder.</summary>
    public static string? FindGp()
    {
        var downloads = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "Downloads");
        string[] candidates =
        [
            Path.Combine(downloads, "gp.jar"),
            Path.Combine(downloads, "gp.exe"),
            Path.Combine(downloads, "GlobalPlatformPro", "gp.jar"),
        ];
        return candidates.FirstOrDefault(File.Exists);
    }

    /// <summary>Authenticates and lists card content (<c>gp --list</c>). Changes nothing.</summary>
    public async Task<CardContent> ListAsync(
        string readerName, GpKeySet? keys, CancellationToken cancellationToken = default)
    {
        var output = await RunGpAsync(BuildArgs(readerName, keys, "--list"), cancellationToken);
        return ParseList(output);
    }

    /// <summary>
    /// Installs the CAP file (<c>gp --install</c>). With <paramref name="replaceExisting"/>,
    /// adds <c>--force</c>, which deletes an existing package and its applets first.
    /// </summary>
    public async Task<string> InstallAsync(
        string readerName, GpKeySet? keys, string capPath, bool replaceExisting,
        CancellationToken cancellationToken = default)
    {
        List<string> action = ["--install", capPath];
        if (replaceExisting)
            action.Add("--force");
        return await RunGpAsync(BuildArgs(readerName, keys, [.. action]), cancellationToken);
    }

    internal static List<string> BuildArgs(string readerName, GpKeySet? keys, params string[] action)
    {
        List<string> args = ["-r", readerName];
        if (keys != null)
        {
            args.AddRange(
            [
                "--key-enc", Convert.ToHexString(keys.Enc),
                "--key-mac", Convert.ToHexString(keys.Mac),
                "--key-dek", Convert.ToHexString(keys.Dek),
            ]);
        }
        args.AddRange(action);
        return args;
    }

    private async Task<string> RunGpAsync(List<string> gpArgs, CancellationToken cancellationToken)
    {
        if (!File.Exists(_gpPath))
            throw new GlobalPlatformException($"GlobalPlatformPro not found at '{_gpPath}'.");

        var startInfo = new ProcessStartInfo(_javaPath)
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
        };
        startInfo.ArgumentList.Add("-jar");
        startInfo.ArgumentList.Add(_gpPath);
        foreach (var arg in gpArgs)
            startInfo.ArgumentList.Add(arg);

        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(Timeout);

        var (exitCode, output) = await _runProcess(startInfo, timeout.Token);
        if (exitCode != 0)
            throw new GlobalPlatformException(DescribeFailure(output), output.Trim());

        return output;
    }

    /// <summary>Turns gp's last error line into a readable message.</summary>
    internal static string DescribeFailure(string output)
    {
        var lastLine = output
            .Split('\n', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .LastOrDefault(l => !l.StartsWith('#')) ?? "(no output)";

        // gp's Java ByteBuffer error when the card returns an empty response
        if (lastLine.Contains("newPosition < 0", StringComparison.Ordinal))
            return "GlobalPlatformPro: the card sent an empty response (no status word). " +
                   "The card did not answer — it may have reset or lost power.";

        return $"GlobalPlatformPro failed: {lastLine}";
    }

    /// <summary>Parses the APP:/PKG: lines of <c>gp --list</c> output.</summary>
    internal static CardContent ParseList(string output)
    {
        List<CardContentEntry> applets = [];
        List<CardContentEntry> packages = [];

        foreach (Match match in ListEntryRegex().Matches(output))
        {
            var entry = new CardContentEntry
            {
                Aid = Convert.FromHexString(match.Groups["aid"].Value),
                LifecycleState = match.Groups["state"].Value switch
                {
                    "LOADED" => 0x01,
                    "INSTALLED" => 0x03,
                    "SELECTABLE" => 0x07,
                    "PERSONALIZED" => 0x0F,
                    "LOCKED" => 0x83,
                    _ => 0x00,
                },
                Privileges = 0,
            };
            (match.Groups["kind"].Value == "APP" ? applets : packages).Add(entry);
        }

        return new CardContent { Applets = applets, Packages = packages };
    }

    [GeneratedRegex(@"^(?<kind>APP|PKG):\s+(?<aid>[0-9A-Fa-f]+)\s+\((?<state>[A-Z_]+)\)", RegexOptions.Multiline)]
    private static partial Regex ListEntryRegex();

    private static async Task<(int ExitCode, string Output)> RunProcessAsync(
        ProcessStartInfo startInfo, CancellationToken cancellationToken)
    {
        using var process = Process.Start(startInfo)
            ?? throw new GlobalPlatformException($"Could not start '{startInfo.FileName}'.");

        var stdout = process.StandardOutput.ReadToEndAsync(cancellationToken);
        var stderr = process.StandardError.ReadToEndAsync(cancellationToken);
        try
        {
            await process.WaitForExitAsync(cancellationToken);
        }
        catch (OperationCanceledException)
        {
            process.Kill(entireProcessTree: true);
            throw new GlobalPlatformException("GlobalPlatformPro did not finish in time and was stopped.");
        }

        return (process.ExitCode, await stdout + await stderr);
    }
}
