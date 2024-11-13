import sys
import pathlib
sys.path.insert(0, str(
    pathlib.Path(__file__).parent / "greaseweazle/src"))

from greaseweazle.codec.codec import get_diskdef

track = get_diskdef("ibm.1440").mk_track(0,0)
track.set_img_track(b'adaf00' + b'\0' * 512 * 18)
#track.decode_raw(track)
print(track.summary_string())
flux = track.flux()
print(flux.list[:25],len(flux.list))
with open(sys.argv[1], "wt") as f:
    for i, fi in enumerate(flux.list):
        print(f"{fi*2},", end="\n" if i % 16 == 15 else " ", file=f)
    print(file=f)
