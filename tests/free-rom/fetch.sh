#!/bin/sh
# fetch.sh <out dir>: the test script's free CPS-1 game (chimera issue 260).
#
# Every game the core's systems run is somebody's, and none may go on a
# public CI runner. Frog Feast for the Capcom CPS-1 (Charles Doty, RasterSoft,
# 2006) is homebrew its author gives away from his own site, the "finished
# version", with its source released for any use beside it. The owner's
# decision (2026-10-14): a test may use it, downloaded in CI and never kept in
# this repository or put in the package.
#
# The download is a zip holding "ffight.zip" - named so MAME runs it in place
# of Final Fight. This refuses a download that is not the file frog.ref
# names, and writes the twelve ROMs under the names FBNeo's cps1frog set
# gives them (matched by their CRC-32s, which FBNeo lists) as cps1frog.zip.
set -eu
out="$1"
here="$(cd "$(dirname "$0")" && pwd)"
url="$(sed -n 's/^url //p' "$here/frog.ref")"
want="$(sed -n 's/^sha256 //p' "$here/frog.ref")"
rm -rf "$out"
mkdir -p "$out"
curl -sS -f -L -m 120 -o "$out/CPS1Frog.zip" "$url"
have="$(sha256sum "$out/CPS1Frog.zip" | cut -c1-64)"
if [ "$have" != "$want" ]; then
	echo "CPS1Frog.zip is $have, not $want (tests/free-rom/frog.ref): not the file the test was written for"
	exit 1
fi
python3 - "$out/CPS1Frog.zip" "$out/cps1frog.zip" <<'PY'
import io, sys, zipfile, zlib
# FBNeo's cps1frog set (src/burn/drv/capcom/d_cps1.cpp): name by CRC-32
names = {
    0x8eb8ddbe: ["frog30-36.bin"], 0xacb5a988: ["frog35-42.bin"],
    0x154803cc: ["frog31-37.bin", "frog36-43.bin"], 0x75660aac: ["frog32-32m.bin"],
    0x7bbc4b54: ["frog05-05m.bin"], 0x0beadd80: ["frog07-07m.bin"],
    0xf84d02bb: ["frog01-01m.bin"], 0xd784a5b0: ["frog03-03m.bin"],
    0x3ac7fb5c: ["frog09-09.bin"], 0x65d1ef07: ["frog18-18.bin"],
    0x7ee8cdcd: ["frog19-19.bin"],
}
outer = zipfile.ZipFile(sys.argv[1])
inner = zipfile.ZipFile(io.BytesIO(outer.read("ffight.zip")))
written = set()
with zipfile.ZipFile(sys.argv[2], "w", zipfile.ZIP_DEFLATED) as z:
    for info in sorted(inner.infolist(), key=lambda i: i.filename):
        data = inner.read(info.filename)
        for name in names.get(zlib.crc32(data) & 0xffffffff, []):
            if name not in written:
                z.writestr(name, data)
                written.add(name)
                break
missing = sorted(n for ns in names.values() for n in ns if n not in written)
if missing:
    sys.exit("not in the download: " + ", ".join(missing))
print("cps1frog.zip: %d ROMs" % len(written))
PY
