"""A byte-level FBX binary reader, written from the published format.

Used to LEARN the format from files Maya and the FBX SDK produce, and later as
an oracle for our own writer. Nothing here is translated from anyone's code --
the record layout is public and this is the obvious way to walk it.
"""
import struct, sys, zlib

def u8(d, o):  return d[o], o + 1
def u32(d, o): return struct.unpack_from('<I', d, o)[0], o + 4
def u64(d, o): return struct.unpack_from('<Q', d, o)[0], o + 8

ARRAY = {'f': ('f', 4), 'd': ('d', 8), 'l': ('q', 8), 'i': ('i', 4), 'b': ('b', 1)}
SCALAR = {'Y': ('h', 2), 'C': ('?', 1), 'I': ('i', 4), 'F': ('f', 4), 'D': ('d', 8), 'L': ('q', 8)}

def read_property(d, o):
    t = chr(d[o]); o += 1
    if t in SCALAR:
        fmt, n = SCALAR[t]
        return t, struct.unpack_from('<' + fmt, d, o)[0], o + n
    if t in ARRAY:
        # The element size is not needed: struct works it out from the count
        # and the format character, and the DECOMPRESSED length is what matters.
        fmt, _ = ARRAY[t]
        n, o = u32(d, o); enc, o = u32(d, o); clen, o = u32(d, o)
        raw = d[o:o + clen]; o += clen
        if enc == 1:
            raw = zlib.decompress(raw)
        return t, list(struct.unpack_from('<%d%s' % (n, fmt), raw, 0)), o
    if t in ('S', 'R'):
        n, o = u32(d, o)
        v = d[o:o + n]; o += n
        return t, (v.decode('utf-8', 'replace') if t == 'S' else v), o
    raise ValueError('unknown property type %r at %d' % (t, o - 1))

def read_node(d, o, version):
    # 64-bit counts from 7500 on; 32-bit before. That single difference is the
    # whole of what changed in the record header, and reading a 7700 file with
    # the 7400 layout desynchronises on the very first node.
    if version >= 7500:
        end, o = u64(d, o); nprops, o = u64(d, o); _, o = u64(d, o)
    else:
        end, o = u32(d, o); nprops, o = u32(d, o); _, o = u32(d, o)
    nlen, o = u8(d, o)
    name = d[o:o + nlen].decode('utf-8', 'replace'); o += nlen
    if end == 0:
        return None, o
    props = []
    for _ in range(nprops):
        t, v, o = read_property(d, o)
        props.append((t, v))
    # A node with children ends its list with a NULL record -- all zeros, which
    # read_node reports as None by way of a zero EndOffset. Trusting `end`
    # alone would walk into it and read a nameless node with no properties.
    kids = []
    while o < end:
        kid, o = read_node(d, o, version)
        if kid is None:
            break
        kids.append(kid)
    return {'name': name, 'props': props, 'children': kids}, end

def parse(path):
    d = open(path, 'rb').read()
    assert d[:21] == b'Kaydara FBX Binary  \x00', d[:21]
    version = struct.unpack_from('<I', d, 23)[0]
    o = 27
    roots = []
    while o < len(d) - 160:
        node, o = read_node(d, o, version)
        if node is None:
            break
        roots.append(node)
    return version, roots, d

def show(node, depth=0, maxdepth=3):
    pad = '  ' * depth
    def brief(t, v):
        if isinstance(v, list):
            return '%s[%d]%s' % (t, len(v), v[:4] if len(v) <= 4 else str(v[:4]) + '...')
        if isinstance(v, bytes):
            return '%s<%d bytes>' % (t, len(v))
        s = str(v)
        return '%s:%s' % (t, s if len(s) < 40 else s[:40] + '...')
    print('%s%s(%s)' % (pad, node['name'], ', '.join(brief(t, v) for t, v in node['props'])))
    if depth < maxdepth:
        for k in node['children']:
            show(k, depth + 1, maxdepth)

if __name__ == '__main__':
    version, roots, d = parse(sys.argv[1])
    print('VERSION', version, 'size', len(d))
    depth = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    for r in roots:
        show(r, 0, depth)
