# A folder as a .tar.gz for Linux, made on Windows (whose files have no execute bit): every file
# 0644, the programs listed - by name, anywhere in the tree - 0755. For make_release_linux.ps1.
#
#   python make_tarball.py <folder> <out.tar.gz> <program name>...
import os
import sys
import tarfile

src, out, programs = sys.argv[1], sys.argv[2], set(sys.argv[3:])
base = os.path.basename(os.path.normpath(src))


def mode(info):
    info.uid = info.gid = 0
    info.uname = info.gname = ''
    if info.isdir() or os.path.basename(info.name) in programs:
        info.mode = 0o755
    else:
        info.mode = 0o644
    return info


with tarfile.open(out, 'w:gz', compresslevel=6) as tar:
    tar.add(src, arcname=base, filter=mode)
print(f'Built {out} ({os.path.getsize(out) / 2**30:.2f} GB)')
