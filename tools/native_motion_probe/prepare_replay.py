"""Pack local readback metadata for the native GPU probe; no game data in source."""
import argparse
import json
import struct
from pathlib import Path


def prepare(folder):
    m = json.loads((folder / 'metadata.json').read_text())
    assert m['version'] in (1, 2) and m['color_format'] in (0, 10)
    data = bytearray(b'SSXNMV2\0')
    def pack(fmt, *values):
        data.extend(struct.pack('<' + fmt, *values))
    pack('II', m['width'], m['height'])
    for name in ['previous', 'current']:
        f = m[name]
        size = (folder / (name + '-geometry.bin')).stat().st_size
        assert 0 < size <= 8*1024*1024 and len(f['draws']) <= 32
        pack('Q4I', f['frame'], f['scene_width'], f['scene_height'], f['scale_x'], f['scale_y'])
        pack('16f', *f['camera'])
        pack('4f', *f['viewport'])
        pack('I2f', int(f.get('jitter_enabled', False)), *f.get('jitter_pixels', [0, 0]))
        pack('II', size, len(f['draws']))
        for d in f['draws']:
            pack('Q8I', int(d['shader'], 16), *(d[k] for k in [
                'vertex_address', 'index_address', 'vertex_offset', 'index_offset',
                'vertex_count', 'index_count', 'vertex_endian', 'index_endian']))
            pack('16f', *d['camera'])
            pack('4f', *d['viewport'])
    (folder / 'replay-metadata.bin').write_bytes(data)
    print('Packed local replay metadata:', len(data), 'bytes')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    prepare(parser.parse_args().directory)
