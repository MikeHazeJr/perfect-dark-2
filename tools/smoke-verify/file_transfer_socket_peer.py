#!/usr/bin/env python3
"""Disposable signed UDP peer for the production presence/file-transfer smoke.

The deterministic keys belong only to the smoke install. No server or user
profile is touched. The peer reports wire evidence; the caller verifies the
game's installed inbox and JSON sidecar after the smoke process exits.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from generate_friend_play_identity import IDENTITIES, agent_handle


FRAME_LEN = 1320
PAYLOAD_OFFSET = 200
PUB_OFFSET = 1224
SIG_OFFSET = 1256
DOMAIN = b"pd-ft-v2"
PORT = IDENTITIES["invitee"]["presence_port"]
PEER_PORT = IDENTITIES["initiator"]["presence_port"]
LOCAL = agent_handle(IDENTITIES["invitee"]["pub"], "invitee")
REMOTE = agent_handle(IDENTITIES["initiator"]["pub"], "initiator")
PAYLOAD = bytes((i * 37 + 11) & 255 for i in range(4097))
RETURN_PAYLOAD = bytes((i * 19 + 7) & 255 for i in range(4097))
RETURN_NAME = 'proof"file.bin'
RETURN_TID = 0x1122334455667788


class Signer:
    def __init__(self, openssl: Path, scratch: Path):
        self.openssl = str(openssl)
        self.key = scratch / "invitee-key.der"
        self.peer_key = scratch / "initiator-pub.der"
        self.message = scratch / "message.bin"
        self.signature = scratch / "signature.bin"
        self.key.write_bytes(bytes.fromhex("302e020100300506032b657004220420") + IDENTITIES["invitee"]["seed"])
        self.peer_key.write_bytes(bytes.fromhex("302a300506032b6570032100") + IDENTITIES["initiator"]["pub"])

    def sign(self, frame: bytearray) -> bytes:
        frame[PUB_OFFSET:SIG_OFFSET] = IDENTITIES["invitee"]["pub"]
        self.message.write_bytes(frame[:SIG_OFFSET] + DOMAIN)
        subprocess.run([self.openssl, "pkeyutl", "-sign", "-inkey", str(self.key),
                        "-keyform", "DER", "-rawin", "-in", str(self.message),
                        "-out", str(self.signature)], check=True, capture_output=True)
        frame[SIG_OFFSET:] = self.signature.read_bytes()
        if len(frame) != FRAME_LEN:
            raise AssertionError("signed frame length changed")
        return bytes(frame)

    def verify(self, frame: bytes) -> None:
        if len(frame) != FRAME_LEN or frame[:5] != b"PDFTX" or frame[5] != 2 or frame[7] != 0:
            raise AssertionError("invalid game file-transfer frame")
        if struct.unpack_from("<II", frame, 8) != (REMOTE, LOCAL):
            raise AssertionError("game frame has wrong sender or recipient")
        if frame[PUB_OFFSET:SIG_OFFSET] != IDENTITIES["initiator"]["pub"]:
            raise AssertionError("game frame public key differs from friend identity")
        self.message.write_bytes(frame[:SIG_OFFSET] + DOMAIN)
        self.signature.write_bytes(frame[SIG_OFFSET:])
        result = subprocess.run([self.openssl, "pkeyutl", "-verify", "-pubin",
                                 "-inkey", str(self.peer_key), "-keyform", "DER",
                                 "-rawin", "-in", str(self.message), "-sigfile",
                                 str(self.signature)], capture_output=True)
        if result.returncode != 0:
            raise AssertionError("game file-transfer signature invalid")


def frame(signer: Signer, kind: int, tid: int, sequence: int, chunks: int = 0,
          size: int = 0, digest: bytes = b"", name: str = "", payload: bytes = b"") -> bytes:
    packet = bytearray(FRAME_LEN)
    packet[:5] = b"PDFTX"
    packet[5:7] = bytes((2, kind))
    struct.pack_into("<IIQIIQ", packet, 8, LOCAL, REMOTE, tid, sequence, chunks, size)
    packet[40:40 + len(digest)] = digest
    packet[72:76] = b"file"
    encoded_name = name.encode("utf-8")
    if len(encoded_name) > 95 or len(payload) > 1024:
        raise AssertionError("invalid peer fixture geometry")
    packet[104:104 + len(encoded_name)] = encoded_name
    packet[PAYLOAD_OFFSET:PAYLOAD_OFFSET + len(payload)] = payload
    return signer.sign(packet)


def received(sock: socket.socket, signer: Signer, until: float) -> bytes:
    while time.monotonic() < until:
        sock.settimeout(min(1.0, max(0.01, until - time.monotonic())))
        try:
            data, address = sock.recvfrom(FRAME_LEN + 1)
        except socket.timeout:
            continue
        if address[0] != "127.0.0.1" or address[1] != PEER_PORT:
            raise AssertionError(f"unexpected source endpoint: {address}")
        # Presence pings share this socket and can precede the file INIT.
        if not data.startswith(b"PDFTX"):
            continue
        signer.verify(data)
        return data
    raise TimeoutError("timed out waiting for a signed game frame")


def control(sock: socket.socket, signer: Signer, tid: int, seq: int) -> None:
    sock.sendto(frame(signer, 3, tid, seq), ("127.0.0.1", PEER_PORT))


def expect_ack(sock: socket.socket, signer: Signer, tid: int, seq: int, packet: bytes,
               deadline: float) -> None:
    next_send = 0.0
    while time.monotonic() < deadline:
        if time.monotonic() >= next_send:
            sock.sendto(packet, ("127.0.0.1", PEER_PORT))
            next_send = time.monotonic() + 0.5
        try:
            incoming = received(sock, signer, min(deadline, next_send))
        except TimeoutError:
            continue
        kind = incoming[6]
        their_tid, their_seq = struct.unpack_from("<QI", incoming, 16)
        if kind == 2:  # Sender END may arrive after our final ACK.
            continue
        if kind == 4 and their_tid == tid:
            raise AssertionError("game rejected inbound transfer")
        if kind == 3 and their_tid == tid and their_seq == seq:
            return
    raise TimeoutError(f"no game ACK for transfer {tid} sequence {seq}")


def run_peer(openssl: Path, timeout: int) -> dict[str, object]:
    deadline = time.monotonic() + timeout
    with tempfile.TemporaryDirectory(prefix="pd-ft-peer-") as temp:
        signer = Signer(openssl, Path(temp))
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.bind(("127.0.0.1", PORT))
            print(f"peer ready on 127.0.0.1:{PORT}", flush=True)
            incoming = received(sock, signer, deadline)
            if incoming[6] != 0:
                raise AssertionError("first game frame is not INIT")
            tid, _, chunks, size = struct.unpack_from("<QIIQ", incoming, 16)
            if size != len(PAYLOAD) or chunks != 5 or incoming[40:72] != hashlib.sha256(PAYLOAD).digest():
                raise AssertionError("game INIT geometry or digest mismatched")
            if incoming[72:76] != b"file" or incoming[104:124].split(b"\0", 1)[0] != b"ft-socket-probe.bin":
                raise AssertionError("game INIT kind or name mismatched")
            control(sock, signer, tid, 0xFFFFFFFF)
            received_chunks: dict[int, bytes] = {}
            while len(received_chunks) < chunks:
                incoming = received(sock, signer, deadline)
                if incoming[6] != 1:
                    continue
                got_tid, seq, echoed = struct.unpack_from("<QII", incoming, 16)
                if got_tid != tid or echoed != chunks or seq >= chunks:
                    raise AssertionError("game CHUNK identity or sequence invalid")
                length = min(1024, size - seq * 1024)
                body = incoming[PAYLOAD_OFFSET:PAYLOAD_OFFSET + length]
                if body != PAYLOAD[seq * 1024:seq * 1024 + length]:
                    raise AssertionError("game CHUNK content mismatch")
                received_chunks[seq] = body
                control(sock, signer, tid, seq)
            if b"".join(received_chunks[i] for i in range(chunks)) != PAYLOAD:
                raise AssertionError("game transfer reassembly failed")
            print("outbound signed file verified and acknowledged", flush=True)

            digest = hashlib.sha256(RETURN_PAYLOAD).digest()
            init = frame(signer, 0, RETURN_TID, 0, 5, len(RETURN_PAYLOAD), digest, RETURN_NAME)
            expect_ack(sock, signer, RETURN_TID, 0xFFFFFFFF, init, deadline)
            last = b""
            for seq in range(5):
                last = frame(signer, 1, RETURN_TID, seq, 5,
                             payload=RETURN_PAYLOAD[seq * 1024:(seq + 1) * 1024])
                expect_ack(sock, signer, RETURN_TID, seq, last, deadline)
            # Resend a signed final chunk after successful save. A durable
            # receipt must replay the final ACK without publishing twice.
            expect_ack(sock, signer, RETURN_TID, 4, last, deadline)
            print("inbound signed file saved; final ACK replay verified", flush=True)
            return {"outbound_sha256": hashlib.sha256(PAYLOAD).hexdigest(),
                    "inbound_sha256": digest.hex(), "inbound_size": len(RETURN_PAYLOAD),
                    "inbound_name": RETURN_NAME, "peer_handle": LOCAL,
                    "final_ack_replayed": True}


def verify_install(install: Path, peer_result: Path) -> dict[str, object]:
    wire = json.loads(peer_result.read_text(encoding="utf-8"))
    if not wire.get("passed") or not wire.get("final_ack_replayed"):
        raise AssertionError("signed peer exchange did not pass")
    folder = install / "social" / "inbox" / "files" / f"{LOCAL:08x}"
    leaf = f"{wire['inbound_sha256'][:32]}_proof_file.bin"
    destination = folder / leaf
    actual = destination.read_bytes()
    if actual != RETURN_PAYLOAD:
        raise AssertionError("committed inbox bytes differ from signed peer payload")
    sidecar = json.loads(Path(str(destination) + ".meta.json").read_text(encoding="utf-8"))
    expected = {"sender_handle": LOCAL, "sender_agent": "invitee",
                "original_name": RETURN_NAME, "sha256": wire["inbound_sha256"],
                "size_bytes": len(RETURN_PAYLOAD), "kind": "file"}
    for key, value in expected.items():
        if sidecar.get(key) != value:
            raise AssertionError(f"sidecar {key} mismatch: {sidecar.get(key)!r}")
    if len(list(folder.glob("*.bin"))) != 1:
        raise AssertionError("duplicate or unexpected inbox publication")
    logs = list(install.rglob("pd-client.log"))
    if len(logs) != 1:
        raise AssertionError(f"expected one installed client log, found {len(logs)}")
    content = logs[0].read_text(encoding="utf-8", errors="replace")
    if content.count("FT: complete <-") != 1:
        raise AssertionError("inbound attachment published more or less than once")
    return {"passed": True, "installed_path": str(destination),
            "installed_sha256": hashlib.sha256(actual).hexdigest(),
            "sidecar": str(destination) + ".meta.json",
            "inbound_publications": 1, "final_ack_replayed": True}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--result", required=True, type=Path)
    parser.add_argument("--verify-install", type=Path)
    parser.add_argument("--peer-result", type=Path)
    parser.add_argument("--openssl", type=Path, default=Path("C:/msys64/mingw64/bin/openssl.exe"))
    parser.add_argument("--timeout", type=int, default=220)
    args = parser.parse_args()
    try:
        if args.verify_install:
            if not args.peer_result:
                raise ValueError("--peer-result is required with --verify-install")
            result = verify_install(args.verify_install, args.peer_result)
        else:
            result = run_peer(args.openssl, args.timeout)
        result["passed"] = True
        code = 0
    except Exception as exc:
        result = {"passed": False, "error": f"{type(exc).__name__}: {exc}"}
        code = 1
    args.result.parent.mkdir(parents=True, exist_ok=True)
    args.result.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, sort_keys=True), flush=True)
    return code


if __name__ == "__main__":
    sys.exit(main())
