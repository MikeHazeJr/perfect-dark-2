#!/usr/bin/env python3
"""Create deterministic, isolated identities for friend-play smoke peers.

The seeds are test fixtures only. They are written solely into a smoke
install, never into the user's profile or the production source tree.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import time
from pathlib import Path


IDENTITIES = {
    "initiator": {
        "seed": bytes.fromhex("01" * 32),
        "pub": bytes.fromhex(
            "8a88e3dd7409f195fd52db2d3cba5d72"
            "ca6709bf1d94121bf3748801b40f6f5c"
        ),
        "presence_port": 28105,
    },
    "invitee": {
        "seed": bytes.fromhex("02" * 32),
        "pub": bytes.fromhex(
            "8139770ea87d175f56a35466c34c7ecc"
            "cb8d8a91b4ee37a25df60f5b8fc9b394"
        ),
        "presence_port": 28106,
    },
}

IDENTITY_MAGIC = 0x44494450
IDENTITY_VERSION = 3
SOCIAL_DOMAIN = b"pd-social-connect-v1\n"


def agent_handle(pubkey: bytes, agent_name: str) -> int:
    digest = hashlib.sha256(pubkey + agent_name.encode("utf-8") + SOCIAL_DOMAIN).digest()
    return int.from_bytes(digest[:4], "little")


def fixed(data: bytes, size: int) -> bytes:
    if len(data) >= size:
        raise ValueError(f"fixture field too long for {size}-byte slot")
    return data + bytes(size - len(data))


def write_identity(install_dir: Path, role: str) -> None:
    identity = IDENTITIES[role]
    uuid = bytes([1 if role == "initiator" else 2]) * 16
    profile = fixed(role.encode("utf-8"), 16) + bytes(64) + bytes(64) + bytes(4)
    payload = (
        struct.pack("<IB", IDENTITY_MAGIC, IDENTITY_VERSION)
        + uuid
        + bytes((1, 0, 0, 0))
        + profile
        + identity["seed"]
        + identity["pub"]
    )
    (install_dir / "pd-identity.dat").write_bytes(payload)


def write_social(install_dir: Path, role: str) -> dict[str, object]:
    peer_role = "invitee" if role == "initiator" else "initiator"
    local = IDENTITIES[role]
    peer = IDENTITIES[peer_role]
    local_handle = agent_handle(local["pub"], role)
    peer_handle = agent_handle(peer["pub"], peer_role)
    social_dir = install_dir / "social"
    social_dir.mkdir(parents=True, exist_ok=True)
    friends = {
        "version": 1,
        "friends": [
            {
                "code": f"smoke-{peer_role}-friend",
                "agent": peer_role,
                "nick": "",
                "handle": peer_handle,
                "muted": False,
                "lastSeen": 0,
                "endpoint": {
                    "ipv4": 0x7F000001,
                    "port": peer["presence_port"],
                    "ttl": int(time.time()) + 3600,
                },
            }
        ],
    }
    (social_dir / "friends.json").write_text(
        json.dumps(friends, indent=2) + "\n", encoding="utf-8"
    )
    (social_dir / "blocks.json").write_text(
        '{\n  "version": 1,\n  "blocks": []\n}\n', encoding="utf-8"
    )
    (social_dir / "presence.json").write_text(
        '{\n  "version": 1,\n  "visibility": "friends_only",\n'
        '  "notify": { "social": true, "invites": true }\n}\n',
        encoding="utf-8",
    )
    agent = {
        "version": 2,
        "name": role,
        "totaltime": 0,
        "autodifficulty": 0,
        "autostageindex": 0,
        "thumbnail": 0,
        "coopcompletions": [0, 0, 0],
        "firingrangescores": [0] * 9,
        "weaponsfound": [0] * 6,
    }
    (install_dir / f"agent_{role}.json").write_text(
        json.dumps(agent, indent=2) + "\n", encoding="utf-8"
    )
    return {
        "role": role,
        "handle": local_handle,
        "handle_hex": f"0x{local_handle:08x}",
        "peer_role": peer_role,
        "peer_handle": peer_handle,
        "peer_handle_hex": f"0x{peer_handle:08x}",
        "presence_port": local["presence_port"],
        "peer_presence_port": peer["presence_port"],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--install-dir", required=True, type=Path)
    parser.add_argument("--role", required=True, choices=sorted(IDENTITIES))
    args = parser.parse_args()
    install_dir = args.install_dir.resolve()
    install_dir.mkdir(parents=True, exist_ok=True)
    write_identity(install_dir, args.role)
    print(json.dumps(write_social(install_dir, args.role), sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
