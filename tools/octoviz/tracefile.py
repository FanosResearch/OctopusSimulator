"""tracefile.py -- reader for the Octopus raw event trace (OCTOPUS_TRACE=<file>, docs/Trace.md).

File layout (little-endian):
  magic "OCTV2\\0\\0\\0"
  records: 64K-record chunks of fixed 32-byte records
           {cycle u64, msg_id u64, addr u64, resource u8, phase u8, kind u8, core u8, comp u16, flags u16}
  FSM records (resource 11): phase = FSM event id, flags = old_state<<8 | new_state, comp = controller;
           names per controller in the "<file>.names" sidecar (read_names)
  index:   n chunks x {offset u64, count u64, cycle_min u64, cycle_max u64, id_min u64, id_max u64}
  trailer: index_offset u64, n_chunks u64, magic
A file cut off before the trailer (killed run) is still readable sequentially (no index).
"""
import struct, os
import numpy as np

MAGIC = b"OCTV2\0\0\0"
REC = np.dtype([("cycle", "<u8"), ("msg_id", "<u8"), ("addr", "<u8"), ("resource", "u1"), ("phase", "u1"), ("kind", "u1"),
                ("core", "u1"), ("comp", "<u2"), ("flags", "<u2")])
RESOURCE = ["CPU", "L1", "REQ_BUS", "RESP_BUS", "LLC", "MEM_BUS", "DRAM", "?", "SVC_BUS", "ARRAY", "LLC_QUEUE", "FSM"]
PHASE = ["ENTER", "SERVICE", "EXIT"]
KIND = ["UNKNOWN", "DEMAND", "GETS", "GETM", "PUTM", "INV", "MEM_READ", "EVICT", "WB_DATA", "WB_INV", "SUPPLY",
        "SUPPLY_DEFERRED", "RESP", "FILL", "FILL_ROLLBACK", "MEM_WRITE"]
ACTION = {0: "REMOVE_PENDING", 1: "HIT_Action", 2: "ADD_PENDING", 3: "SEND_BUS_MSG", 4: "WRITE_BACK", 5: "UPDATE_CACHE_LINE",
          6: "WRITE_CACHE_LINE_DATA", 7: "MODIFY_DATA"}   # ControllerAction::Type ordinals used in ARRAY flags

def read_names(path):
    """{comp: {"states": [...], "events": [...]}} from the sidecar written at trace close."""
    out = {}
    try:
        for line in open(path + ".names", encoding="utf-8"):
            p = line.rstrip("\n").split(" ", 3)
            if len(p) < 3 or p[0] != "comp": continue
            out.setdefault(int(p[1]), {})[p[2]] = p[3].split(",") if len(p) > 3 and p[3] else []
    except FileNotFoundError:
        pass
    return out

def read_index(path):
    size = os.path.getsize(path)
    with open(path, "rb") as f:
        f.seek(0); m = f.read(8); assert m == MAGIC, "not an Octopus v2 trace (%r); re-run the simulation with the current build" % m
        f.seek(size - 24); idx_off, n = struct.unpack("<QQ", f.read(16))
        if f.read(8) != MAGIC:   # truncated: synthesize one chunk over the whole body
            return None, size
        f.seek(idx_off); raw = f.read(48 * n)
    idx = np.frombuffer(raw, dtype=np.dtype([("offset", "<u8"), ("count", "<u8"), ("cmin", "<u8"), ("cmax", "<u8"), ("imin", "<u8"), ("imax", "<u8")]))
    return idx, size

def read_all(path):
    """All records as a structured numpy array (a giant SPLASH trace does not fit -- use read_window)."""
    idx, size = read_index(path)
    with open(path, "rb") as f:
        if idx is None:
            f.seek(8); n = (size - 8) // REC.itemsize; return np.fromfile(f, dtype=REC, count=n)
        parts = []
        for c in idx:
            f.seek(int(c["offset"])); parts.append(np.fromfile(f, dtype=REC, count=int(c["count"])))
    return np.concatenate(parts) if parts else np.zeros(0, dtype=REC)

def iter_chunks(path, t0=None, t1=None):
    """Yield the trace's 64K-record chunks in file order (bounded memory, ~2 MB each). With a cycle
    window only the chunks whose range intersects it are read, trimmed to the window; a truncated
    file (no trailer) is read sequentially in 64K-record slices."""
    idx, size = read_index(path)
    with open(path, "rb") as f:
        if idx is None:
            f.seek(8); left = (size - 8) // REC.itemsize
            while left > 0:
                a = np.fromfile(f, dtype=REC, count=min(left, 65536)); left -= len(a)
                if len(a) == 0: break
                if t0 is not None: a = a[(a["cycle"] >= t0) & (a["cycle"] <= t1)]
                if len(a): yield a
            return
        for c in idx:
            if t0 is not None and (c["cmax"] < t0 or c["cmin"] > t1): continue
            f.seek(int(c["offset"])); a = np.fromfile(f, dtype=REC, count=int(c["count"]))
            if t0 is not None: a = a[(a["cycle"] >= t0) & (a["cycle"] <= t1)]
            if len(a): yield a

def read_window(path, t0, t1):
    """Records with cycle in [t0, t1], reading only the chunks whose cycle range intersects."""
    idx, size = read_index(path)
    if idx is None:
        a = read_all(path); return a[(a["cycle"] >= t0) & (a["cycle"] <= t1)]
    sel = idx[(idx["cmax"] >= t0) & (idx["cmin"] <= t1)]
    parts = []
    with open(path, "rb") as f:
        for c in sel:
            f.seek(int(c["offset"])); a = np.fromfile(f, dtype=REC, count=int(c["count"])); parts.append(a[(a["cycle"] >= t0) & (a["cycle"] <= t1)])
    return np.concatenate(parts) if parts else np.zeros(0, dtype=REC)
