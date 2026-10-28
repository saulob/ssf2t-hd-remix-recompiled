"""Builds the flat memory image the port is recompiled from: game/extracted/default.xex with the Title Update
delta patch game/extracted/default.xexp applied, written to game/analysis/default.image.bin.

  python -I tools/xex_image.py                 Title Update image (default.xex + default.xexp)
  python -I tools/xex_image.py --base-only --out game/analysis/default-0.0.1.3.image.bin

Mirrors ReXGlue v0.10.0 (XexModule::ReadImage, XexModule::ApplyPatch and libmspack lzxd), so the result is the image
codegen and the runtime load. Every page is checked against the hash chain of the final XEX security info, and nothing
is written when a check fails. Standard library only.
"""
import argparse
import hashlib
import os
import struct
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
XEX = os.path.join(ROOT, 'game', 'extracted', 'default.xex')
XEXP = os.path.join(ROOT, 'game', 'extracted', 'default.xexp')
IMAGE = os.path.join(ROOT, 'game', 'analysis', 'default.image.bin')

RETAIL_KEY = bytes.fromhex('20B185A59D28FDC340583FBB0896BF91')
DEVKIT_KEY = bytes(16)
OPT_FILE_FORMAT = 0x3FF
OPT_DELTA_PATCH = 0x5FF
OPT_EXECUTION_INFO = 0x40006
MODULE_PATCH_FLAGS = 0x10 | 0x20 | 0x40


class XexError(Exception):
    pass


def _aes_tables():
    sbox = [0] * 256
    p = q = 1
    while True:
        p = (p ^ (p << 1) ^ (0x1B if p & 0x80 else 0)) & 0xFF
        q ^= q << 1
        q ^= q << 2
        q ^= q << 4
        q &= 0xFF
        if q & 0x80:
            q ^= 0x09
        rot = lambda n: ((q << n) | (q >> (8 - n))) & 0xFF
        sbox[p] = q ^ rot(1) ^ rot(2) ^ rot(3) ^ rot(4) ^ 0x63
        if p == 1:
            break
    sbox[0] = 0x63
    inv = [0] * 256
    for i, v in enumerate(sbox):
        inv[v] = i

    def mul(a, b):
        r = 0
        while b:
            if b & 1:
                r ^= a
            a = ((a << 1) ^ 0x1B) & 0xFF if a & 0x80 else a << 1
            b >>= 1
        return r

    td0 = [(mul(inv[x], 14) << 24) | (mul(inv[x], 9) << 16) | (mul(inv[x], 13) << 8) | mul(inv[x], 11)
           for x in range(256)]
    ror = lambda w, n: ((w >> n) | (w << (32 - n))) & 0xFFFFFFFF
    return sbox, inv, td0, [ror(w, 8) for w in td0], [ror(w, 16) for w in td0], [ror(w, 24) for w in td0]


SBOX, INV_SBOX, TD0, TD1, TD2, TD3 = _aes_tables()


def _decrypt_round_keys(key):
    rk = list(struct.unpack('>4I', key))
    rcon = 1
    for i in range(4, 44):
        t = rk[i - 1]
        if i % 4 == 0:
            t = ((SBOX[(t >> 16) & 0xFF] << 24) | (SBOX[(t >> 8) & 0xFF] << 16) | (SBOX[t & 0xFF] << 8) |
                 SBOX[t >> 24]) ^ (rcon << 24)
            rcon = ((rcon << 1) ^ 0x1B) & 0xFF if rcon & 0x80 else rcon << 1
        rk.append(rk[i - 4] ^ t)
    rounds = [rk[4 * r:4 * r + 4] for r in range(11)][::-1]
    for r in range(1, 10):
        rounds[r] = [TD0[SBOX[w >> 24]] ^ TD1[SBOX[(w >> 16) & 0xFF]] ^ TD2[SBOX[(w >> 8) & 0xFF]] ^
                     TD3[SBOX[w & 0xFF]] for w in rounds[r]]
    return rounds


def aes_cbc_decrypt(key, data):
    """AES-128-CBC with a zero IV, as ReXGlue's aes_decrypt_buffer; a trailing partial block is copied."""
    rounds = _decrypt_round_keys(key)
    last = rounds[10]
    i = INV_SBOX
    out = bytearray(len(data))
    p0 = p1 = p2 = p3 = 0
    words = struct.unpack('>%dI' % (len(data) // 4), data[:len(data) & ~3])
    for n in range(len(data) // 16):
        c0, c1, c2, c3 = words[4 * n:4 * n + 4]
        k = rounds[0]
        s0, s1, s2, s3 = c0 ^ k[0], c1 ^ k[1], c2 ^ k[2], c3 ^ k[3]
        for r in range(1, 10):
            k = rounds[r]
            s0, s1, s2, s3 = (
                TD0[s0 >> 24] ^ TD1[(s3 >> 16) & 0xFF] ^ TD2[(s2 >> 8) & 0xFF] ^ TD3[s1 & 0xFF] ^ k[0],
                TD0[s1 >> 24] ^ TD1[(s0 >> 16) & 0xFF] ^ TD2[(s3 >> 8) & 0xFF] ^ TD3[s2 & 0xFF] ^ k[1],
                TD0[s2 >> 24] ^ TD1[(s1 >> 16) & 0xFF] ^ TD2[(s0 >> 8) & 0xFF] ^ TD3[s3 & 0xFF] ^ k[2],
                TD0[s3 >> 24] ^ TD1[(s2 >> 16) & 0xFF] ^ TD2[(s1 >> 8) & 0xFF] ^ TD3[s0 & 0xFF] ^ k[3],
            )
        o0 = ((i[s0 >> 24] << 24) | (i[(s3 >> 16) & 0xFF] << 16) | (i[(s2 >> 8) & 0xFF] << 8) | i[s1 & 0xFF]) ^ last[0]
        o1 = ((i[s1 >> 24] << 24) | (i[(s0 >> 16) & 0xFF] << 16) | (i[(s3 >> 8) & 0xFF] << 8) | i[s2 & 0xFF]) ^ last[1]
        o2 = ((i[s2 >> 24] << 24) | (i[(s1 >> 16) & 0xFF] << 16) | (i[(s0 >> 8) & 0xFF] << 8) | i[s3 & 0xFF]) ^ last[2]
        o3 = ((i[s3 >> 24] << 24) | (i[(s2 >> 16) & 0xFF] << 16) | (i[(s1 >> 8) & 0xFF] << 8) | i[s0 & 0xFF]) ^ last[3]
        struct.pack_into('>4I', out, 16 * n, o0 ^ p0, o1 ^ p1, o2 ^ p2, o3 ^ p3)
        p0, p1, p2, p3 = c0, c1, c2, c3
    tail = len(data) & ~15
    out[tail:] = data[tail:]
    return bytes(out)


POSITION_SLOTS = {15: 30, 16: 32, 17: 34, 18: 36, 19: 38, 20: 42, 21: 50}
EXTRA_BITS = [0, 0, 0, 0] + [n // 2 for n in range(2, 34)]
POSITION_BASE = [0]
for _slot in range(1, 290):
    POSITION_BASE.append(POSITION_BASE[-1] + (1 << (EXTRA_BITS[_slot - 1] if _slot - 1 < 36 else 17)))
BITBUF_WIDTH = 64
BITBUF_MASK = (1 << BITBUF_WIDTH) - 1


def _huffman_table(lengths, allow_empty=False):
    if not any(lengths):
        return None if allow_empty else [None] * 65536
    table = [None] * 65536
    code = 0
    for bits in range(1, 17):
        for sym, length in enumerate(lengths):
            if length == bits:
                lo, hi = code << (16 - bits), (code + 1) << (16 - bits)
                if hi > 65536:
                    raise XexError('LZX: oversubscribed Huffman table')
                table[lo:hi] = [(sym, bits)] * (hi - lo)
                code += 1
        code <<= 1
    if code != 1 << 17:
        raise XexError('LZX: incomplete Huffman table')
    return table


class _Lzxd:
    """libmspack lzxd (non-delta format), with ReXGlue's reference window placed at the end of the window."""

    def __init__(self, data, window_size, out_len, ref_data=None):
        window_bits = window_size.bit_length() - 1
        if window_size != 1 << window_bits or window_bits not in POSITION_SLOTS:
            raise XexError(f'LZX: unsupported window size {window_size:#x}')
        if ref_data is not None and len(ref_data) > window_size:
            raise XexError('LZX: reference data larger than the window')
        self.data = data
        self.pos = 0
        self.padded = False
        self.bitbuf = 0
        self.bits_left = 0
        self.window_size = window_size
        self.out_len = out_len
        self.main_elems = 256 + (POSITION_SLOTS[window_bits] << 3)
        self.prefix = b'' if ref_data is None else bytes(window_size - len(ref_data)) + bytes(ref_data)

    def _ensure(self, n):
        while self.bits_left < n:
            if self.pos + 2 <= len(self.data):
                b0, b1 = self.data[self.pos], self.data[self.pos + 1]
            elif not self.padded:
                b0 = self.data[self.pos] if self.pos < len(self.data) else 0
                b1 = 0
                self.padded = True
            else:
                raise XexError('LZX: read past the end of the input')
            self.pos += 2
            self.bitbuf |= ((b1 << 8) | b0) << (BITBUF_WIDTH - 16 - self.bits_left)
            self.bits_left += 16

    def _remove(self, n):
        self.bitbuf = (self.bitbuf << n) & BITBUF_MASK
        self.bits_left -= n

    def _bits(self, n):
        if n == 0:
            return 0
        self._ensure(n)
        v = self.bitbuf >> (BITBUF_WIDTH - n)
        self._remove(n)
        return v

    def _sym(self, table):
        self._ensure(16)
        entry = table[self.bitbuf >> (BITBUF_WIDTH - 16)]
        if entry is None:
            raise XexError('LZX: bad Huffman code')
        self._remove(entry[1])
        return entry[0]

    def _read_lengths(self, lens, first, last):
        pretree = _huffman_table([self._bits(4) for _ in range(20)])
        x = first
        while x < last:
            z = self._sym(pretree)
            if z == 17:
                y = self._bits(4) + 4
                lens[x:x + y] = [0] * y
                x += y
            elif z == 18:
                y = self._bits(5) + 20
                lens[x:x + y] = [0] * y
                x += y
            elif z == 19:
                y = self._bits(1) + 4
                z = (lens[x] - self._sym(pretree)) % 17
                lens[x:x + y] = [z] * y
                x += y
            else:
                lens[x] = (lens[x] - z) % 17
                x += 1

    def run(self):
        out = bytearray(self.prefix)
        start = len(out)
        main_len = [0] * (self.main_elems + 64)
        len_len = [0] * (249 + 64)
        r0 = r1 = r2 = 1
        if self._bits(1):
            raise XexError('LZX: Intel E8 translation is not used by XEX images')
        block_type = None
        block_length = block_remaining = 0
        main_t = len_t = aligned_t = None
        while len(out) - start < self.out_len:
            frame_end = len(out) + min(32768, self.out_len - (len(out) - start))
            while len(out) < frame_end:
                if block_remaining == 0:
                    if block_type == 3 and block_length & 1:
                        self.pos += 1
                    block_type = self._bits(3)
                    block_length = block_remaining = (self._bits(16) << 8) | self._bits(8)
                    if block_type == 2:
                        aligned_t = _huffman_table([self._bits(3) for _ in range(8)])
                    if block_type in (1, 2):
                        self._read_lengths(main_len, 0, 256)
                        self._read_lengths(main_len, 256, self.main_elems)
                        main_t = _huffman_table(main_len[:self.main_elems])
                        self._read_lengths(len_len, 0, 249)
                        len_t = _huffman_table(len_len[:249], allow_empty=True)
                    elif block_type == 3:
                        if self.bits_left == 0:
                            self._ensure(16)
                        self.bits_left = 0
                        self.bitbuf = 0
                        r0, r1, r2 = struct.unpack_from('<III', self.data, self.pos)
                        self.pos += 12
                    else:
                        raise XexError(f'LZX: bad block type {block_type}')
                this_run = min(block_remaining, frame_end - len(out))
                block_remaining -= this_run
                if block_type == 3:
                    out += self.data[self.pos:self.pos + this_run]
                    self.pos += this_run
                    continue
                target = len(out) + this_run
                while len(out) < target:
                    m = self._sym(main_t)
                    if m < 256:
                        out.append(m)
                        continue
                    m -= 256
                    match_length = m & 7
                    if match_length == 7:
                        if len_t is None:
                            raise XexError('LZX: length symbol needed but the tree is empty')
                        match_length += self._sym(len_t)
                    match_length += 2
                    slot = m >> 3
                    if slot == 0:
                        offset = r0
                    elif slot == 1:
                        offset, r1, r0 = r1, r0, r1
                    elif slot == 2:
                        offset, r2, r0 = r2, r0, r2
                    else:
                        extra = 17 if slot >= 36 else EXTRA_BITS[slot]
                        offset = POSITION_BASE[slot] - 2
                        if extra >= 3 and block_type == 2:
                            if extra > 3:
                                offset += self._bits(extra - 3) << 3
                            offset += self._sym(aligned_t)
                        elif extra:
                            offset += self._bits(extra)
                        r2, r1, r0 = r1, r0, offset
                    if offset > self.window_size or offset > len(out):
                        raise XexError('LZX: match offset beyond the window')
                    src = len(out) - offset
                    if offset >= match_length:
                        out += out[src:src + match_length]
                    else:
                        for k in range(match_length):
                            out.append(out[src + k])
                if len(out) > target:
                    over = len(out) - target
                    if over > block_remaining:
                        raise XexError('LZX: match ran past the end of the block')
                    block_remaining -= over
            if len(out) != frame_end:
                raise XexError('LZX: decode crossed a frame boundary')
            if self.bits_left > 0:
                self._ensure(16)
            if self.bits_left & 15:
                self._remove(self.bits_left & 15)
        return bytes(out[start:start + self.out_len])


def lzx_decompress(data, window_size, out_len, ref_data=None):
    return _Lzxd(data, window_size, out_len, ref_data).run()


def lzxdelta_apply(patch, window_size, dest):
    p = 0
    while p + 12 <= len(patch):
        old_addr, new_addr, uncompressed_len, compressed_len = struct.unpack_from('>IIHH', patch, p)
        if not (old_addr or new_addr or uncompressed_len or compressed_len):
            break
        if compressed_len == 0:
            dest[new_addr:new_addr + uncompressed_len] = bytes(uncompressed_len)
            p += 12
        elif compressed_len == 1:
            dest[new_addr:new_addr + uncompressed_len] = dest[old_addr:old_addr + uncompressed_len]
            p += 12
        else:
            data = patch[p + 12:p + 12 + compressed_len]
            ref = bytes(dest[old_addr:old_addr + uncompressed_len])
            dest[new_addr:new_addr + uncompressed_len] = lzx_decompress(data, window_size, uncompressed_len, ref)
            p += 12 + compressed_len


class Xex:
    def __init__(self, path):
        self.path = path
        with open(path, 'rb') as f:
            self.file = f.read()
        if self.file[:4] != b'XEX2':
            raise XexError(f'{path}: not an XEX2 file')
        self.module_flags, header_size = struct.unpack_from('>II', self.file, 4)
        self.header = bytearray(self.file[:header_size])
        self.session_key = None

    def opt(self, key):
        count, = struct.unpack_from('>I', self.header, 20)
        for i in range(count):
            k, v = struct.unpack_from('>II', self.header, 24 + 8 * i)
            if k == key:
                return v
        return None

    @property
    def security(self):
        return struct.unpack_from('>I', self.header, 16)[0]

    @property
    def image_size(self):
        return struct.unpack_from('>I', self.header, self.security + 4)[0]

    @property
    def aes_key(self):
        return bytes(self.header[self.security + 0x150:self.security + 0x160])

    @property
    def rsa_signature(self):
        return bytes(self.header[self.security + 8:self.security + 0x108])

    def file_format(self):
        off = self.opt(OPT_FILE_FORMAT)
        _, encryption, compression, window_size, first_size = struct.unpack_from('>IHHII', self.header, off)
        return encryption, compression, window_size, first_size, bytes(self.header[off + 16:off + 36])

    def version(self):
        return struct.unpack_from('>I', self.header, self.opt(OPT_EXECUTION_INFO) + 4)[0]


def iter_blocks(payload, first_size, first_hash, what):
    p = 0
    size, digest = first_size, first_hash
    while size:
        block = payload[p:p + size]
        if hashlib.sha1(block).digest() != digest:
            raise XexError(f'{what}: block hash mismatch at {p:#x} (wrong key or damaged file)')
        yield block[24:]
        p += size
        size, digest = struct.unpack_from('>I', block, 0)[0], block[4:24]


def read_image(xex):
    encryption, compression, window_size, first_size, first_hash = xex.file_format()
    payload = xex.file[len(xex.header):]
    last_error = XexError('no key produced an image')
    for key in (RETAIL_KEY, DEVKIT_KEY):
        xex.session_key = aes_cbc_decrypt(key, xex.aes_key)
        data = aes_cbc_decrypt(xex.session_key, payload) if encryption == 1 else payload
        try:
            if compression == 0:
                image = bytearray(data[:xex.image_size])
            elif compression == 2:
                chunks = bytearray()
                for block in iter_blocks(data, first_size, first_hash, os.path.basename(xex.path)):
                    q = 0
                    while True:
                        chunk = (block[q] << 8) | block[q + 1]
                        q += 2
                        if not chunk:
                            break
                        chunks += block[q:q + chunk]
                        q += chunk
                image = bytearray(lzx_decompress(bytes(chunks), window_size, xex.image_size))
            else:
                raise XexError(f'unsupported compression type {compression}')
        except XexError as error:
            last_error = error
            continue
        if image[:2] == b'MZ':
            return image
        last_error = XexError('the decrypted image is not a PE')
    raise last_error


def apply_patch(base, image, patch):
    """XexModule::ApplyPatch: patches base.header and image in place."""
    desc = patch.opt(OPT_DELTA_PATCH)
    if not patch.module_flags & MODULE_PATCH_FLAGS or desc is None:
        raise XexError(f'{patch.path}: not an XEX delta patch')
    target_version, source_version = struct.unpack_from('>II', patch.header, desc + 4)
    digest_source = bytes(patch.header[desc + 0x0C:desc + 0x20])
    image_key_source = bytes(patch.header[desc + 0x20:desc + 0x30])
    (target_headers, hdr_src_off, hdr_src_size, hdr_tgt_off,
     img_src_off, img_src_size, img_tgt_off) = struct.unpack_from('>7I', patch.header, desc + 0x30)
    info = desc + 0x4C
    info_compressed, = struct.unpack_from('>H', patch.header, info + 10)

    if hashlib.sha1(base.rsa_signature).digest() != digest_source:
        raise XexError('the Title Update was made for a different default.xex')
    if base.version() != source_version:
        raise XexError(f'the Title Update needs version {version_text(source_version)}, '
                       f'default.xex is {version_text(base.version())}')

    _, _, window_size, first_size, first_hash = patch.file_format()
    original_image_size = base.image_size
    header = base.header
    if hdr_src_off > len(header) or hdr_src_size > len(header) - hdr_src_off or hdr_tgt_off > target_headers or \
            hdr_src_size > target_headers - hdr_tgt_off:
        raise XexError('the header patch bounds are invalid')
    header_target_size = target_headers or (hdr_tgt_off + hdr_src_size)
    if header_target_size > len(header):
        header += bytes(header_target_size - len(header))
    if hdr_src_off:
        header[hdr_tgt_off:hdr_tgt_off + hdr_src_size] = header[hdr_src_off:hdr_src_off + hdr_src_size]
    if header_target_size < len(header):
        header[header_target_size:] = bytes(len(header) - header_target_size)
    lzxdelta_apply(bytes(patch.header[info:info + info_compressed + 12]), window_size, header)
    del header[header_target_size:]

    new_image_size = base.image_size
    if new_image_size > original_image_size:
        image += bytes(new_image_size - original_image_size)

    original_session_key = base.session_key
    base.session_key = aes_cbc_decrypt(RETAIL_KEY, base.aes_key)
    patch.session_key = aes_cbc_decrypt(base.session_key, patch.aes_key)
    if aes_cbc_decrypt(base.session_key, image_key_source) != original_session_key:
        raise XexError('the Title Update image key does not match default.xex')

    payload = patch.file[len(patch.header):]
    data = aes_cbc_decrypt(patch.session_key, payload) if patch.file_format()[0] == 1 else payload
    if img_src_off:
        image[img_tgt_off:img_tgt_off + img_src_size] = image[img_src_off:img_src_off + img_src_size]
    image_target_size = img_tgt_off + img_src_size
    if image_target_size < original_image_size:
        image[image_target_size:original_image_size] = bytes(original_image_size - image_target_size)
    for block in iter_blocks(data, first_size, first_hash, os.path.basename(patch.path)):
        lzxdelta_apply(block, window_size, image)
    del image[new_image_size:]
    return source_version, target_version


def verify_pages(xex, image):
    """Each page digest covers the next region and its descriptor; the security info digest covers the first."""
    sec = xex.security
    image_flags, = struct.unpack_from('>I', xex.header, sec + 0x10C)
    page_size = 0x1000 if image_flags & 0x10000000 else 0x10000
    count, = struct.unpack_from('>I', xex.header, sec + 0x180)
    expected = bytes(xex.header[sec + 0x114:sec + 0x128])
    offset = 0
    for i in range(count):
        raw = bytes(xex.header[sec + 0x184 + 24 * i:sec + 0x184 + 24 * (i + 1)])
        size = (struct.unpack_from('>I', raw, 0)[0] >> 4) * page_size
        if hashlib.sha1(bytes(image[offset:offset + size]) + raw).digest() != expected:
            raise XexError(f'page region {i} (image offset {offset:#x}) does not match its hash')
        expected = raw[4:24]
        offset += size
    if offset != len(image):
        raise XexError(f'the page hashes cover {offset:#x} bytes, the image is {len(image):#x}')
    return count


def version_text(v):
    return f'{v >> 28}.{(v >> 24) & 0xF}.{(v >> 8) & 0xFFFF}.{v & 0xFF}'


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--xex', default=XEX)
    ap.add_argument('--patch', default=XEXP, help='Title Update delta patch (default: game/extracted/default.xexp)')
    ap.add_argument('--base-only', action='store_true', help='build the image of default.xex without the update')
    ap.add_argument('--out', default=IMAGE)
    args = ap.parse_args(argv)
    try:
        base = Xex(args.xex)
        image = read_image(base)
        print(f'default.xex {version_text(base.version())}: image {len(image):#x} bytes')
        if not args.base_only:
            if not os.path.isfile(args.patch):
                raise XexError(f'{args.patch} is missing (or pass --base-only)')
            source, target = apply_patch(base, image, Xex(args.patch))
            print(f'Title Update applied: {version_text(source)} -> {version_text(target)}')
        print(f'{verify_pages(base, image)} page hashes verified')
    except (XexError, OSError) as error:
        print(f'error: {error}', file=sys.stderr)
        return 1
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, 'wb') as f:
        f.write(image)
    print(f'wrote {args.out} (SHA-256 {hashlib.sha256(image).hexdigest().upper()})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
