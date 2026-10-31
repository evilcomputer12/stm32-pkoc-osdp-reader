namespace CredBench.Core.Services.GlobalPlatform;

/// <summary>
/// Static SCP02 key set for the Issuer Security Domain. Suppliers either give one key used for
/// all three roles, or separate ENC / MAC / DEK (sometimes called KEK) keys.
/// </summary>
public record GpKeySet(byte[] Enc, byte[] Mac, byte[] Dek)
{
    public static GpKeySet Single(byte[] key) => new(key, key, key);
}
