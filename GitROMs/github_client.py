"""Minimal GitHub API client for GitROMs."""

import json
import urllib.request
import urllib.error
from typing import List, Dict, Optional, Tuple

from config import GITHUB_API, USER_AGENT, GITHUB_TOKEN, ROM_EXTENSIONS


def _headers() -> Dict[str, str]:
    h = {
        "Accept": "application/vnd.github+json",
        "User-Agent": USER_AGENT,
        "X-GitHub-Api-Version": "2022-11-28",
    }
    if GITHUB_TOKEN:
        h["Authorization"] = f"Bearer {GITHUB_TOKEN}"
    return h


def _get(url: str) -> Tuple[Optional[object], Optional[str]]:
    """Return (parsed_json, error_message)."""
    try:
        req = urllib.request.Request(url, headers=_headers())
        with urllib.request.urlopen(req, timeout=20) as resp:
            data = resp.read()
            return json.loads(data.decode("utf-8")), None
    except urllib.error.HTTPError as e:
        try:
            body = e.read().decode("utf-8")
            msg = json.loads(body).get("message", str(e))
        except Exception:
            msg = str(e)
        return None, f"HTTP {e.code}: {msg}"
    except Exception as e:
        return None, str(e)


def list_user_repos(username: str) -> Tuple[List[Dict], Optional[str]]:
    """Return list of public repos for a user."""
    url = f"{GITHUB_API}/users/{username}/repos?per_page=100&sort=updated&type=public"
    data, err = _get(url)
    if err:
        return [], err
    if not isinstance(data, list):
        return [], "Unexpected response"
    repos = []
    for r in data:
        repos.append({
            "name": r.get("name", ""),
            "full_name": r.get("full_name", ""),
            "description": (r.get("description") or "")[:120],
            "default_branch": r.get("default_branch") or "main",
            "updated_at": r.get("updated_at", ""),
        })
    return repos, None


def list_contents(owner: str, repo: str, path: str = "", ref: str = "main") -> Tuple[List[Dict], Optional[str]]:
    """List directory contents. Only returns directories + ROM files."""
    api_path = path.strip("/")
    url = f"{GITHUB_API}/repos/{owner}/{repo}/contents/{api_path}"
    if ref:
        url += f"?ref={ref}"
    data, err = _get(url)
    if err:
        return [], err

    if isinstance(data, dict):
        data = [data]
    if not isinstance(data, list):
        return [], "Unexpected response"

    items = []
    for entry in data:
        name = entry.get("name") or ""
        typ = entry.get("type")
        if typ == "dir":
            items.append({
                "name": name,
                "path": entry.get("path") or name,
                "type": "dir",
                "size": 0,
                "download_url": None,
            })
        elif typ == "file":
            lower = name.lower()
            if any(lower.endswith(ext) for ext in ROM_EXTENSIONS):
                items.append({
                    "name": name,
                    "path": entry.get("path") or name,
                    "type": "file",
                    "size": entry.get("size") or 0,
                    "download_url": entry.get("download_url"),
                })

    items.sort(key=lambda x: (0 if x["type"] == "dir" else 1, x["name"].lower()))
    return items, None


def download_file(url: str, dest_path: str) -> Optional[str]:
    """Download a file to dest_path. Returns error string or None on success."""
    try:
        req = urllib.request.Request(url, headers=_headers())
        with urllib.request.urlopen(req, timeout=120) as resp:
            with open(dest_path, "wb") as f:
                while True:
                    chunk = resp.read(64 * 1024)
                    if not chunk:
                        break
                    f.write(chunk)
        return None
    except Exception as e:
        return str(e)
