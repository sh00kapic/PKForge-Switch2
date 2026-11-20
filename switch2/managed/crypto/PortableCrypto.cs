// SPDX-License-Identifier: GPL-3.0-or-later
using System;
using System.IO;
using System.Security.Cryptography;
using Org.BouncyCastle.Crypto;
using Org.BouncyCastle.Crypto.Digests;
using Org.BouncyCastle.Crypto.Engines;
using Org.BouncyCastle.Crypto.Modes;
using Org.BouncyCastle.Crypto.Parameters;

namespace PKHeX.Core;

// WASI has no OS cryptography provider. Keep the save formats' original algorithms.
internal static class SHA1
{
    public const int HashSizeInBytes = 20;
    public static int HashData(ReadOnlySpan<byte> data, Span<byte> result) => PortableCrypto.Hash(new Sha1Digest(),data,result);
}
internal static class SHA256
{
    public const int HashSizeInBytes = 32;
    public static int HashData(ReadOnlySpan<byte> data, Span<byte> result) => PortableCrypto.Hash(new Sha256Digest(),data,result);
}
internal enum HashAlgorithmName { SHA1, SHA256 }
internal sealed class IncrementalHash : IDisposable
{
    readonly IDigest? digest;
    readonly FileStream? stream;
    IncrementalHash(HashAlgorithmName name)
    {
        if (name==HashAlgorithmName.SHA256) stream=File.Create("sha256.stream");
        else if (name==HashAlgorithmName.SHA1) digest=new Sha1Digest();
        else throw new NotSupportedException(name.ToString());
    }
    public static IncrementalHash CreateHash(HashAlgorithmName name) => new(name);
    public void AppendData(ReadOnlySpan<byte> data)
    {
        if (stream!=null) stream.Write(data);
        else digest!.BlockUpdate(data);
    }
    public int GetCurrentHash(Span<byte> result)
    {
        if (stream==null) return digest!.DoFinal(result);
        stream.Dispose();
        var hash=File.ReadAllBytes("sha256.result");
        if (hash.Length!=32) throw new InvalidDataException("Incomplete SHA-256 digest.");
        hash.CopyTo(result);
        return hash.Length;
    }
    public void Dispose() { stream?.Dispose(); digest?.Reset(); }
}

public sealed class PortableCrypto : IAesCryptographyProvider, IMd5Provider
{
    public static void Install()
    {
        var provider = new PortableCrypto();
        RuntimeCryptographyProvider.Md5 = provider;
        RuntimeCryptographyProvider.Aes = provider;
    }
    internal static int Hash(IDigest digest,ReadOnlySpan<byte> data,Span<byte> result)
    {
        digest.BlockUpdate(data);
        return digest.DoFinal(result);
    }
    public void HashData(ReadOnlySpan<byte> input,Span<byte> output) => Hash(new MD5Digest(),input,output);
    public IAesCryptographyProvider.IAes Create(byte[] key,CipherMode mode,PaddingMode padding,byte[]? iv=null)
    {
        if (padding!=PaddingMode.None) throw new NotSupportedException("Only unpadded AES is used by these formats.");
        return new Cipher(key,iv);
    }
    sealed class Cipher(byte[] key, byte[]? iv) : IAesCryptographyProvider.IAes
    {
        void Transform(bool cbc,bool encrypt,ReadOnlySpan<byte> data,Span<byte> output)
        {
            IBlockCipher cipher = cbc ? new CbcBlockCipher(new AesEngine()) : new AesEngine();
            var secret = new KeyParameter(key);
            cipher.Init(encrypt,cbc ? new ParametersWithIV(secret,iv ?? throw new ArgumentException("Missing IV.")) : secret);
            for (int offset=0;offset<data.Length;offset+=16) cipher.ProcessBlock(data[offset..],output[offset..]);
        }
        public void EncryptEcb(ReadOnlySpan<byte> data,Span<byte> output) => Transform(false,true,data,output);
        public void DecryptEcb(ReadOnlySpan<byte> data,Span<byte> output) => Transform(false,false,data,output);
        public void EncryptCbc(ReadOnlySpan<byte> data,Span<byte> output) => Transform(true,true,data,output);
        public void DecryptCbc(ReadOnlySpan<byte> data,Span<byte> output) => Transform(true,false,data,output);
        public void Dispose() {}
    }
}
