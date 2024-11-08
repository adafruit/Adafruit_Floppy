import sys
import pathlib
sys.path.insert(0, str(
    pathlib.Path(__file__).parent / "greaseweazle/scripts"))
import click
from greaseweazle.track import MasterTrack
from greaseweazle.codec.ibm.mfm import IBM_MFM
from greaseweazle.codec.ibm.fm import IBM_FM
from bitarray import bitarray
from make_flux_fm import RX01

@click.command
@click.option("--fm/--no-fm", is_flag=True)
@click.argument("flux-file")
def main(flux_file, fm=False):
    print(f"{flux_file=!r}")
    with open(flux_file) as flux1:
        content = bitarray("".join(c for c in flux1.read() if c in "01"))

    print(content.count(0), content.count(1))

    if fm:
        master = MasterTrack(content[:41_750], .167)
        track = RX01(0,0)
        track.time_per_rev = .166
        track.clock = 4e-6
    else:
        master = MasterTrack(content[:200_000], 0.2)
        track = IBM_MFM(0,0)
        track.time_per_rev = 0.2
        track.clock = 1e-6


    track.decode_raw(master)
    print(flux_file, track.summary_string(), file=sys.stderr)
    print(flux_file, track.summary_string())
    print("".join("E."[sec.crc == 0] for sec in track.sectors))
    for i in track.iams:
        print(i)
    for s in track.sectors:
        print(s)

    if n := track.nr_missing():
        print(f"{n} missing sector(s)", file=sys.stderr)
        raise SystemExit(1)

if __name__ == '__main__':
    main()
