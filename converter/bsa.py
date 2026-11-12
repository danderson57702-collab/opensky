"""Reader for Skyrim Special Edition BSA archives (version 105, LZ4-frame compressed)."""
import struct

import lz4.frame

ARCHIVE_DIR_NAMES = 0x1
ARCHIVE_FILE_NAMES = 0x2
ARCHIVE_COMPRESSED = 0x4
ARCHIVE_EMBED_NAMES = 0x100

FILE_SIZE_COMPRESS_TOGGLE = 0x40000000


class BSAEntry:
    __slots__ = ("path", "size", "offset", "compressed")

    def __init__(self, path, size, offset, compressed):
        self.path = path
        self.size = size
        self.offset = offset
        self.compressed = compressed


class BSA:
    def __init__(self, path):
        self.path = path
        self.entries = {}
        with open(path, "rb") as f:
            self._read_index(f)

    def _read_index(self, f):
        (magic, version, _folder_off, self.flags, folder_count, file_count,
         _total_dir_len, total_file_len, _file_flags, _pad) = struct.unpack("<4sIIIIIIIHH", f.read(36))
        if magic != b"BSA\0" or version != 105:
            raise ValueError(f"{self.path}: not a v105 BSA (magic={magic!r}, version={version})")
        if not (self.flags & ARCHIVE_DIR_NAMES and self.flags & ARCHIVE_FILE_NAMES):
            raise ValueError(f"{self.path}: archives without names are unsupported")

        # Folder records: hash u64, count u32, pad u32, offset u64
        folder_counts = [struct.unpack("<QIIQ", f.read(24))[1] for _ in range(folder_count)]

        # File record blocks follow in folder order: bzstring folder name + file records
        records = []
        for count in folder_counts:
            name_len = f.read(1)[0]
            folder = f.read(name_len)[:-1].decode("cp1252")
            for _ in range(count):
                _hash, size, offset = struct.unpack("<QII", f.read(16))
                records.append((folder, size, offset))

        names = f.read(total_file_len).split(b"\0")[:file_count]
        default_compressed = bool(self.flags & ARCHIVE_COMPRESSED)
        for (folder, size, offset), name in zip(records, names):
            compressed = default_compressed != bool(size & FILE_SIZE_COMPRESS_TOGGLE)
            full = f"{folder}\\{name.decode('cp1252')}".lower()
            self.entries[full] = BSAEntry(full, size & 0x3FFFFFFF, offset, compressed)

    def list(self, prefix="", suffix=""):
        prefix = prefix.lower().replace("/", "\\")
        return sorted(p for p in self.entries if p.startswith(prefix) and p.endswith(suffix))

    def read(self, path):
        e = self.entries[path.lower().replace("/", "\\")]
        with open(self.path, "rb") as f:
            f.seek(e.offset)
            size = e.size
            if self.flags & ARCHIVE_EMBED_NAMES:
                skip = f.read(1)[0]
                f.seek(skip, 1)
                size -= 1 + skip
            if not e.compressed:
                return f.read(size)
            original_size = struct.unpack("<I", f.read(4))[0]
            data = lz4.frame.decompress(f.read(size - 4))
            if len(data) != original_size:
                raise ValueError(f"{path}: decompressed {len(data)} bytes, expected {original_size}")
            return data
