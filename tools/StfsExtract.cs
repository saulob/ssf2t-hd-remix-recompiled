// Minimal STFS (LIVE/PIRS/CON) package extractor.
// Usage (via tools/extract_stfs.ps1): StfsExtract <package> <outdir>
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

public static class StfsExtract
{
    static readonly long[] BlocksPerLevel = { 0xAA, 0x70E4, 0x4AF768 };

    static byte[] data;
    static long baseOffset;
    static int shift; // 0 = read-only (1 hash table per level), 1 = two tables

    static uint BE32(long o) { return (uint)(data[o] << 24 | data[o + 1] << 16 | data[o + 2] << 8 | data[o + 3]); }
    static int BE16(long o) { return data[o] << 8 | data[o + 1]; }
    static int BE24(long o) { return data[o] << 16 | data[o + 1] << 8 | data[o + 2]; }
    static int LE24(long o) { return data[o] | data[o + 1] << 8 | data[o + 2] << 16; }
    static int LE16(long o) { return data[o] | data[o + 1] << 8; }

    // Title ID from the package header (big-endian at 0x360), or 0 when the
    // file is not a LIVE/PIRS/CON package. Reads only the header.
    public static uint TitleId(string pkg)
    {
        var head = new byte[0x364];
        using (var f = File.OpenRead(pkg))
        {
            if (f.Read(head, 0, head.Length) != head.Length) return 0;
        }
        string magic = Encoding.ASCII.GetString(head, 0, 4);
        if (magic != "LIVE" && magic != "PIRS" && magic != "CON ") return 0;
        return (uint)(head[0x360] << 24 | head[0x361] << 16 | head[0x362] << 8 | head[0x363]);
    }

    static long BlockToOffset(long block)
    {
        long b = block;
        for (int i = 0; i < 3; i++)
        {
            b += ((block + BlocksPerLevel[i]) / BlocksPerLevel[i]) << shift;
            if (block < BlocksPerLevel[i]) break;
        }
        return baseOffset + (b << 12);
    }

    // Raw block number of the level-0 hash table covering `block`.
    static long HashBlockNumber(long block)
    {
        long step0 = shift == 0 ? 0xAB : 0xAC;
        if (block < 0xAA) return 0;
        long num = (block / 0xAA) * step0;
        num += ((block / 0x70E4) + 1) << shift;
        if (block / 0x70E4 == 0) return num;
        return num + (1L << shift);
    }

    static int NextBlock(long block)
    {
        long hashOff = baseOffset + (HashBlockNumber(block) << 12);
        long entry = hashOff + (block % 0xAA) * 0x18;
        return BE24(entry + 0x15);
    }

    public static int Run(string pkg, string outDir)
    {
        data = File.ReadAllBytes(pkg);
        string magic = Encoding.ASCII.GetString(data, 0, 4);
        if (magic != "LIVE" && magic != "PIRS" && magic != "CON ")
            throw new Exception("Not an STFS package: " + magic);

        uint headerSize = BE32(0x340);
        baseOffset = (headerSize + 0xFFF) & ~0xFFFL;
        byte flags = data[0x37B];
        shift = (flags & 1) != 0 ? 0 : 1;
        int ftBlockCount = LE16(0x37C);
        int ftBlock = LE24(0x37E);
        Console.WriteLine("magic={0} header=0x{1:X} flags=0x{2:X2} ftBlocks={3} ftStart={4}",
            magic, headerSize, flags, ftBlockCount, ftBlock);

        // Read file table (follow chain; it's usually contiguous)
        var entries = new List<long>();
        long blk = ftBlock;
        for (int i = 0; i < ftBlockCount; i++)
        {
            long off = BlockToOffset(blk);
            for (int e = 0; e < 0x40; e++) entries.Add(off + e * 0x40);
            blk = NextBlock(blk);
        }

        var names = new List<string>();
        var parents = new List<int>();
        var paths = new Dictionary<int, string>();
        for (int idx = 0; idx < entries.Count; idx++)
        {
            long e = entries[idx];
            int nameLen = data[e + 0x28] & 0x3F;
            if (nameLen == 0) { names.Add(null); parents.Add(-1); continue; }
            names.Add(Encoding.ASCII.GetString(data, (int)e, nameLen));
            parents.Add(BE16(e + 0x32));
        }

        Func<int, string> pathOf = null;
        pathOf = i =>
        {
            string p;
            if (paths.TryGetValue(i, out p)) return p;
            int parent = parents[i];
            p = parent == 0xFFFF ? names[i] : Path.Combine(pathOf(parent), names[i]);
            paths[i] = p;
            return p;
        };

        int count = 0;
        for (int idx = 0; idx < entries.Count; idx++)
        {
            if (names[idx] == null) continue;
            long e = entries[idx];
            byte f = data[e + 0x28];
            string rel = pathOf(idx);
            string full = Path.Combine(outDir, rel);
            if ((f & 0x80) != 0) { Directory.CreateDirectory(full); continue; }

            Directory.CreateDirectory(Path.GetDirectoryName(full));
            bool contiguous = (f & 0x40) != 0;
            int start = LE24(e + 0x2F);
            int validBlocks = LE24(e + 0x29);
            long size = BE32(e + 0x34);

            using (var fs = File.Create(full))
            {
                long remaining = size;
                long b = start;
                for (int i = 0; i < validBlocks && remaining > 0; i++)
                {
                    int n = (int)Math.Min(0x1000, remaining);
                    fs.Write(data, (int)BlockToOffset(b), n);
                    remaining -= n;
                    b = contiguous ? b + 1 : NextBlock(b);
                }
            }
            Console.WriteLine("{0,10}  {1}{2}", size, rel, contiguous ? "" : "  (fragmented)");
            count++;
        }
        Console.WriteLine("Extracted {0} files.", count);
        return 0;
    }
}
