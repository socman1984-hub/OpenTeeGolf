"""Put the OpenTeeGolf Unreal release (built by make_release.ps1) on GitHub, for its updater.

    python unreal/publish_release.py            (publish dist/OpenTeeGolf-Unreal-<version>-win-x64)
    python unreal/publish_release.py --dry-run  (say what would go up, change nothing)

Needs the GitHub CLI, signed in (gh auth login). Each release is tagged unreal-v<version>, and is
never marked "latest" (the web version's releases share the repository, and its website links to
the latest one).

What goes up:
- each file of the game that no earlier release already has, named by its SHA-1 - so an update
  uploads only what changed, and the game downloads only that;
- manifest.json: every file with its size, SHA-1 and download address (an earlier release's, for
  files that haven't changed);
- the installer, OpenTeeGolf-Setup-<version>.exe (Inno Setup, unreal/installer/OpenTeeGolf.iss),
  and a starter zip: the game without its tree scenery (pakchunk1-3: too big for one file on
  GitHub). Installed or unzipped and started, the game finds the scenery listed and missing and
  downloads it.
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def find_gh():
    """The GitHub CLI: on the path, or where its installer puts it (a terminal opened before it was
    installed doesn't have it on the path yet)"""
    for p in (shutil.which("gh"), os.path.join(os.environ.get("ProgramFiles", r"C:\Program Files"), "GitHub CLI", "gh.exe"),
              os.path.join(os.environ.get("LOCALAPPDATA", ""), "Programs", "GitHub CLI", "gh.exe")):
        if p and os.path.exists(p):
            return p
    return None


GH = find_gh()


def gh(*args, capture=True):
    r = subprocess.run([GH, *args], cwd=ROOT, capture_output=capture, text=True)
    if r.returncode != 0:
        raise RuntimeError("gh %s failed: %s" % (" ".join(args), (r.stderr or r.stdout or "").strip()))
    return r.stdout


MAX_ON_RELEASE = 950   # (new files on the release itself; GitHub's limit is 1000 assets)


def scenery(path):
    name = os.path.basename(path)
    return name.startswith("pakchunk") and not name.startswith("pakchunk0")


def main():
    dry = "--dry-run" in sys.argv
    version = json.load(open(os.path.join(ROOT, "package.json"), encoding="utf-8"))["version"]
    name = "OpenTeeGolf-Unreal-%s-win-x64" % version
    stage = os.path.join(ROOT, "dist", name)
    original = open(os.path.join(stage, "manifest.json"), encoding="utf-8").read()
    manifest = json.loads(original)
    if manifest["version"] != version:
        sys.exit("dist/%s is %s, package.json says %s: run make_release.ps1 again" % (name, manifest["version"], version))
    repo, tag = manifest["repo"], "unreal-v" + version
    if not GH:
        if not dry:
            sys.exit("The GitHub CLI isn't installed: https://cli.github.com, then gh auth login")
        print("(no GitHub CLI: a dry run as if this were the first release)")
        releases = []
    else:
        gh("auth", "status")
        releases = json.loads(gh("release", "list", "--limit", "100", "--json", "tagName,isDraft,isPrerelease"))
    if any(r["tagName"] == tag for r in releases):
        sys.exit("%s is already on GitHub: raise the version in package.json for a new release" % tag)

    # what the earlier releases already hold (the newest Unreal one's file list)
    known = {}
    earlier = [r["tagName"] for r in releases if r["tagName"].startswith("unreal-v") and not r["isDraft"] and not r["isPrerelease"]]
    tmp = tempfile.mkdtemp(prefix="otg_publish_")
    if earlier:
        gh("release", "download", earlier[0], "-p", "manifest.json", "-D", tmp)
        for f in json.load(open(os.path.join(tmp, "manifest.json"), encoding="utf-8"))["files"]:
            if f.get("url"):
                known[f["sha1"]] = f["url"]
        os.remove(os.path.join(tmp, "manifest.json"))

    up = os.path.join(tmp, "up")
    extra_up = os.path.join(tmp, "extra")
    os.makedirs(up)
    os.makedirs(extra_up)
    # (GitHub holds at most 1000 files a release: past MAX_ON_RELEASE new ones - with the installer,
    # the starter zip and manifest.json - the rest go on a companion pre-release, EXTRA; the manifest
    # gives each file's own address, and the game's updater skips pre-releases)
    extra_tag = tag + "-files2"
    new_shas = []
    new_bytes = reused = 0
    for f in manifest["files"]:
        if f["sha1"] in known:
            f["url"] = known[f["sha1"]]
            reused += 1
            continue
        if f["sha1"] not in new_shas:
            new_shas.append(f["sha1"])
        on_extra = new_shas.index(f["sha1"]) >= MAX_ON_RELEASE
        f["url"] = "https://github.com/%s/releases/download/%s/%s" % (repo, extra_tag if on_extra else tag, f["sha1"])
        dest = os.path.join(extra_up if on_extra else up, f["sha1"])
        if not os.path.exists(dest):
            src = os.path.join(stage, f["path"])
            try:
                os.link(src, dest)            # (no copy of gigabytes: the same file)
            except OSError:
                shutil.copyfile(src, dest)
            new_bytes += f["size"]
    with open(os.path.join(stage, "manifest.json"), "w", encoding="utf-8") as fh:
        json.dump(manifest, fh, indent=1)
    shutil.copyfile(os.path.join(stage, "manifest.json"), os.path.join(up, "manifest.json"))

    # the starter zip: everything but the scenery (Windows' own tar writes zips)
    starter = os.path.join(up, name + ".zip")
    excludes = [x for f in manifest["files"] if scenery(f["path"]) for x in ("--exclude", name + "/" + f["path"])]
    subprocess.run([os.path.join(os.environ["SystemRoot"], "System32", "tar.exe"), "-a", "-c", "-f", starter, *excludes, "-C", os.path.dirname(stage), name], check=True)
    if os.path.getsize(starter) >= 2 * 1024 ** 3:
        sys.exit("The starter zip is over 2 GB")

    # the installer, with this manifest (so a fresh install knows where to download the scenery)
    iscc = os.path.join(os.environ["LOCALAPPDATA"], "Programs", "Inno Setup 6", "ISCC.exe")
    if not os.path.exists(iscc):
        sys.exit("No Inno Setup 6 (jrsoftware.org): the installer can't be built")
    subprocess.run([iscc, "/Q", "/DVersion=" + version, "/DSource=" + stage, "/DOutDir=" + up,
                    os.path.join(HERE, "installer", "OpenTeeGolf.iss")], check=True)
    setup = os.path.join(up, "OpenTeeGolf-Setup-%s.exe" % version)
    if os.path.getsize(setup) >= 2 * 1024 ** 3:
        sys.exit("The installer is over 2 GB")

    files = sorted(os.listdir(up))
    extra = sorted(os.listdir(extra_up))
    print("%s: %d files to upload (%.2f GB; %d of them on %s), %d already on earlier releases; installer %.0f MB, starter zip %.0f MB"
          % (tag, len(files) - 3 + len(extra), new_bytes / 1024 ** 3, len(extra), extra_tag, reused, os.path.getsize(setup) / 1024 ** 2, os.path.getsize(starter) / 1024 ** 2))
    if dry:
        # (the release folder as it was: a dry run changes nothing)
        with open(os.path.join(stage, "manifest.json"), "w", encoding="utf-8") as fh:
            fh.write(original)
        print("(dry run: nothing published; the files are in %s)" % up)
        return
    notes = ("OpenTeeGolf for Unreal Engine %s.\n\nNew here: download OpenTeeGolf-Setup-%s.exe and run it (no admin needed; "
             "Windows may say it protected your PC: More info, Run anyway). Or download %s.zip, unzip it anywhere and run "
             "OpenTeeGolf.exe. On its first start the game downloads its scenery (about 3 GB) and restarts.\n\n"
             "Already have it: the game updates itself (it checks when it starts)." % (version, version, name))
    def upload(t, folder, names):
        # (in batches: Windows' command line holds about 32,000 characters)
        for i in range(0, len(names), 100):
            gh("release", "upload", t, *[os.path.join(folder, f) for f in names[i:i + 100]], capture=False)
            print("%s: uploaded %d of %d" % (t, min(i + 100, len(names)), len(names)), flush=True)
    # the companion first (published as a pre-release, so its addresses work), then the release
    if extra:
        gh("release", "create", extra_tag, "--title", "OpenTeeGolf for Unreal %s (more files)" % version, "--notes",
           "Files of OpenTeeGolf for Unreal %s that didn't fit on its release (GitHub holds 1000 a release). Download the game from %s." % (version, tag),
           "--prerelease", "--latest=false", "--draft", capture=False)
        upload(extra_tag, extra_up, extra)
        gh("release", "edit", extra_tag, "--draft=false", "--prerelease", "--latest=false", capture=False)
    # (a draft while its files go up: the game's updater skips drafts, so it never finds a release half there)
    gh("release", "create", tag, "--title", "OpenTeeGolf for Unreal %s" % version, "--notes", notes, "--latest=false", "--draft", capture=False)
    # (manifest.json last, so the game's updater never reads one whose files aren't up yet)
    upload(tag, up, [f for f in files if f != "manifest.json"])
    gh("release", "upload", tag, os.path.join(up, "manifest.json"), capture=False)
    gh("release", "edit", tag, "--draft=false", "--latest=false", capture=False)
    shutil.rmtree(tmp, ignore_errors=True)
    print("Published %s" % tag)


if __name__ == "__main__":
    main()
