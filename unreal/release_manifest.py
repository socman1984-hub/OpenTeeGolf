"""The file list an OpenTeeGolf Unreal release carries, for its updater (OTGUpdateSubsystem):
every file in the release folder with its size and SHA-1, the version and the GitHub repository.
make_release.ps1 writes it into the release folder as manifest.json; publish_release.ps1 adds each
file's download address when the release goes on GitHub.

    python release_manifest.py <release folder> <version> <owner/repo>
"""

import hashlib
import json
import os
import sys


def sha1(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(4 << 20), b""):
            h.update(block)
    return h.hexdigest()


def main():
    folder, version, repo = sys.argv[1], sys.argv[2], sys.argv[3]
    files = []
    for root, _, names in os.walk(folder):
        for n in names:
            full = os.path.join(root, n)
            rel = os.path.relpath(full, folder).replace("\\", "/")
            if rel == "manifest.json" or rel.startswith("Update/"):
                continue
            size = os.path.getsize(full)
            if size >= 2 * 1024 ** 3:
                sys.exit("%s is %.2f GB: GitHub takes files under 2 GB (split the content further)" % (rel, size / 1024 ** 3))
            files.append({"path": rel, "size": size, "sha1": sha1(full), "url": ""})
    files.sort(key=lambda f: f["path"])
    out = {"version": version, "repo": repo, "files": files}
    with open(os.path.join(folder, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(out, f, indent=1)
    print("manifest.json: %d files, %.2f GB" % (len(files), sum(f["size"] for f in files) / 1024 ** 3))


if __name__ == "__main__":
    main()
