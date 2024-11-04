import sys
import pathlib
sys.path.insert(0, str(
    pathlib.Path(__file__).parent / "greaseweazle/scripts"))
    
from greaseweazle.codec.ibm.fm import IBM_FM_Predefined

class RX01(IBM_FM_Predefined):
    id0 = 0
    nsec = 26
    sz = 0
    cskew = 1
    gap_3 = 27  # old GW has 26 but we want the same gap3 that mfm_impl will use
    time_per_rev = 60/360
    clock = 4e-6

def convertflux(flux):
    for x in flux:
        yield 1
        for i in range(x-1):
            yield 0

track = RX01(0, 0)
trackdata = b''.join(bytes([65 + i]) * 128 for i in range(RX01.nsec))
track.set_img_track(trackdata)
track.decode_raw(track)
print(track.summary_string())
flux = track.flux()
with open(sys.argv[1], "wt") as f:
    for i, fi in enumerate(convertflux(flux.list[1:])):
        print(f"{fi}", end="\n" if i % 16 == 15 else "", file=f)
    print(file=f)
