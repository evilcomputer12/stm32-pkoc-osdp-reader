"""Verify a wolfCrypt-generated signature using Python cryptography/OpenSSL."""
from pathlib import Path
import subprocess
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, utils

root = Path(__file__).resolve().parent.parent
result = subprocess.run([root/'.build-host/reader/crypto_test'],check=True,text=True,capture_output=True)
signature = bytes.fromhex(next(x.split('=',1)[1] for x in result.stdout.splitlines() if x.startswith('SIGNATURE_DER=')))
digest = hashes.Hash(hashes.SHA256()); digest.update(b'abc'); digest = digest.finalize()
ec.derive_private_key(1,ec.SECP256R1()).public_key().verify(signature,digest,ec.ECDSA(utils.Prehashed(hashes.SHA256())))
print(result.stdout,end='')
print('PASS: independent OpenSSL verification of wolfCrypt signature')
