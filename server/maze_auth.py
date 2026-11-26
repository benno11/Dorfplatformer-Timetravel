"""Project 3D one-time maze tokens for the TimeTravel server."""
import hashlib
import hmac
import os
import re
import sqlite3
import struct
import time

from cryptography.exceptions import InvalidSignature, InvalidTag
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.hazmat.primitives.hashes import SHA256
from cryptography.hazmat.primitives.kdf.hkdf import HKDF

USERNAME = re.compile(r"[A-Za-z0-9_-]{3,48}")
CHALLENGE = re.compile(r"[0-9A-Fa-f]{64}")
PORTAL_BODY_LIMIT = 4096
TICKET_LIFETIME = 12 * 60 * 60
TOKEN_FORMAT = b"P3D4"
CHALLENGE_BYTES = 32
CONNECTION_SECRET_BYTES = 32
TOKEN_BYTES = 185
REDEMPTION_HEX_LENGTH = 2 * (TOKEN_BYTES + CONNECTION_SECRET_BYTES)
REDEMPTION_BODY = re.compile(rf"[0-9A-Fa-f]{{{REDEMPTION_HEX_LENGTH}}}")


def fingerprint(ticket):
    return hashlib.sha512(ticket).digest()[:32]


class MazeTokens:
    def __init__(self, key, database):
        self.key = key
        self.database = database
        seed = key.private_bytes(serialization.Encoding.Raw, serialization.PrivateFormat.Raw,
                                 serialization.NoEncryption())
        self.encryption = AESGCM(HKDF(algorithm=SHA256(), length=32, salt=None,
                                     info=b"Project 3D maze token encryption V4").derive(seed))
        with self.connect() as db:
            db.execute("CREATE TABLE IF NOT EXISTS tokens (digest BLOB PRIMARY KEY, expires INTEGER NOT NULL)")
            db.execute("CREATE INDEX IF NOT EXISTS token_expiry ON tokens(expires)")

    def connect(self):
        return sqlite3.connect(self.database, factory=TokenConnection)

    def issue(self, username, challenge):
        if not isinstance(username, str) or not USERNAME.fullmatch(username) or not CHALLENGE.fullmatch(challenge):
            raise ValueError("Invalid account name or challenge")
        now = int(time.time())
        expires = now + TICKET_LIFETIME
        payload = bytes.fromhex(challenge) + struct.pack("!Q49s", expires, username.encode("ascii"))
        nonce = os.urandom(12)
        envelope = TOKEN_FORMAT + nonce + self.encryption.encrypt(nonce, payload, TOKEN_FORMAT)
        ticket = envelope + self.key.sign(envelope)
        with self.connect() as db:
            db.execute("DELETE FROM tokens WHERE expires <= ?", (now,))
            db.execute("INSERT INTO tokens VALUES (?, ?)", (fingerprint(ticket), expires))
        return ticket.hex()

    def redeem(self, ticket, secret):
        if len(secret) != CONNECTION_SECRET_BYTES or len(ticket) != TOKEN_BYTES or ticket[:4] != TOKEN_FORMAT:
            return None
        try:
            self.key.public_key().verify(ticket[-64:], ticket[:-64])
            payload = self.encryption.decrypt(ticket[4:16], ticket[16:-64], TOKEN_FORMAT)
        except (InvalidSignature, InvalidTag):
            return None
        challenge = hashlib.sha512(secret).digest()[:CHALLENGE_BYTES]
        if not hmac.compare_digest(payload[:CHALLENGE_BYTES], challenge):
            return None
        now = int(time.time())
        digest = fingerprint(ticket)
        with self.connect() as db:
            deleted = db.execute("DELETE FROM tokens WHERE digest = ? AND expires > ?", (digest, now)).rowcount
        if deleted != 1:
            return None
        receipt = b"P3DA" + challenge + digest + payload[CHALLENGE_BYTES + 8:]
        return (receipt + self.key.sign(receipt)).hex().encode("ascii")


class TokenConnection(sqlite3.Connection):
    def __exit__(self, *args):
        try:
            return super().__exit__(*args)
        finally:
            self.close()


def load_signing_key(path):
    key = serialization.load_pem_private_key(path.read_bytes(), password=None)
    if not isinstance(key, Ed25519PrivateKey):
        raise ValueError("The maze signing key must be Ed25519.")
    return key


def public_key_hex(key):
    return key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw).hex()
