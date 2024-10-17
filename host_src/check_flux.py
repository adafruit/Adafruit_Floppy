import sys
import click
from greaseweazle.track import MasterTrack
from greaseweazle.codec.ibm.mfm import IBM_MFM
from bitarray import bitarray


@click.command
@click.option("--compact/--no-compact", is_flag=True)
@click.argument("flux-file")
def main(flux_file, compact=False):
    assert not compact
    with open(flux_file) as flux1:
        content = bitarray("".join(c for c in flux1.read() if c in "01"))

    nominal_bitrate = 1_000_000

    master = MasterTrack(content[:200_000], 0.2)
    print(master.flux().list[:25])
    track = IBM_MFM(0,0)
    track.time_per_rev = 0.2
    track.clock = 1e-6


    track.decode_raw(master)
    print(sys.argv[1], track.summary_string(), file=sys.stderr)
    print(sys.argv[1], track.summary_string())
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
