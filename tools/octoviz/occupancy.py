#!/usr/bin/env python3
"""occupancy.py -- build the resource-OCCUPANCY and FSM-transition tables from a raw event trace
(OCTOPUS_TRACE, docs/Trace.md), streaming the trace chunk by chunk with bounded memory so a
billion-cycle SPLASH trace converts on a laptop.

occupancy: one row per (resource, comp) busy interval, for EVERY message -- demand requests and the
non-request traffic (write-backs, back-invalidations, supplies, fills, LLC evictions):
    resource  REQ_BUS | RESP_BUS | SVC_BUS | MEM_BUS | ARRAY | DRAM | LLC_QUEUE
    comp      component id (bus 0, memory bus direction 0/1, array = controller id, DRAM id, queue owner)
    start,end absolute core cycles, [start, end)
    kind      Message::Kind name (GETS, WB_DATA, FILL, ...)
    core      Message::owner, msg_id the message id (joins the request table on id), addr
    flags     ARRAY action; cutin (port busy at claim); reg (1 MSHR-, 2 PWB-resident)
fsm: one row per FSM transition: cycle, comp, addr, line, msg_id, kind, core, event, old, new.

Pairing rules (docs/Trace.md, "Occupancy intervals"):
    REQ_BUS / RESP_BUS  ENTER (grant) -> EXIT (transmitted), same msg_id+kind, in order
    SVC_BUS / MEM_BUS   EXIT only     -> [EXIT - width, EXIT]   (SVC: 1 core cycle, the measured request-bus slot;
                                                                  MEM_BUS: A_req for reads/writes, A_res for fills)
    ARRAY               SERVICE       -> [SERVICE, SERVICE + A_LLC]  (PWB data merge: zero width)
    DRAM                ENTER -> EXIT by msg_id (reads); writes are ENTER only -> [ENTER, ENTER + 1]
    LLC_QUEUE           LLC_QUEUE.ENTER -> controller ENTER (admission), same comp+msg_id+kind, in order
Starts whose end lies in a later chunk are carried over, so chunk boundaries are invisible.

Output: <out_dir>/occupancy_NNN.parquet and fsm_NNN.parquet parts of --part-rows rows each (parts are
cycle-ordered, sorted by start inside; DuckDB reads them as one table via a glob), or single files
occupancy.parquet / fsm.parquet when one part suffices.

Usage: python occupancy.py <trace.bin> [-o <out_dir>] [--t0 T0 --t1 T1] [--part-rows N]
                           [--validate <octoviz.parquet>] [--a-req 2 --a-res 5 --a-llc 10 --a-l1 0] [--global-clock]
"""
import argparse, glob, os, sys, time
import numpy as np
import duckdb
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tracefile as tf

R = {n: i for i, n in enumerate(tf.RESOURCE)}
P = {n: i for i, n in enumerate(tf.PHASE)}
K = {n: i for i, n in enumerate(tf.KIND)}
BUS_ROLES = ("REQ_BUS", "RESP_BUS", "SVC_BUS", "MEM_BUS")
OCC_COLS = ("resource", "comp", "start", "end", "kind", "core", "msg_id", "flags", "cutin", "addr", "reg")
FSM_COLS = ("cycle", "comp", "addr", "line", "msg_id", "kind", "core", "event", "old", "new")


def _pairs(a, enter_mask, exit_mask, key_cols):
    """Pair the k-th ENTER-like with the k-th EXIT-like event of the same key (array order = time order).
    Returns (idx_enter, idx_exit, unmatched_enter_idx, unmatched_exit_count)."""
    ent = np.flatnonzero(enter_mask)
    ext = np.flatnonzero(exit_mask)
    if len(ent) == 0 or len(ext) == 0:
        return np.zeros(0, int), np.zeros(0, int), ent, len(ext)

    def with_seq(idx):
        keys = np.stack([a[c][idx].astype(np.int64) for c in key_cols], 1)
        order = np.lexsort(tuple(keys[:, i] for i in range(keys.shape[1] - 1, -1, -1)) + (idx,))
        ks = keys[order]
        new = np.ones(len(order), bool)
        new[1:] = np.any(ks[1:] != ks[:-1], axis=1)
        grp_start = np.maximum.accumulate(np.where(new, np.arange(len(order)), 0))
        d = np.zeros(len(order), dtype=[(f"k{i}", np.int64) for i in range(keys.shape[1])] + [("s", np.int64)])
        for i in range(keys.shape[1]):
            d[f"k{i}"] = ks[:, i]
        d["s"] = np.arange(len(order)) - grp_start
        return idx[order], d

    ie, pe = with_seq(ent)
    ix, px = with_seq(ext)
    _, ai, bi = np.intersect1d(pe, px, assume_unique=True, return_indices=True)
    un_e = np.setdiff1d(ie, ie[ai], assume_unique=True)
    return ie[ai], ix[bi], un_e, len(ix) - len(bi)


class Streamer:
    """Feeds trace chunks in order; accumulates occupancy / fsm columns and writes Parquet parts."""
    # pairing groups: name -> (start selector, end selector, key columns)
    GROUPS = {
        "REQ_BUS":   (lambda a: (a["resource"] == R["REQ_BUS"]) & (a["phase"] == P["ENTER"]),
                      lambda a: (a["resource"] == R["REQ_BUS"]) & (a["phase"] == P["EXIT"]), ["msg_id", "kind"]),
        "RESP_BUS":  (lambda a: (a["resource"] == R["RESP_BUS"]) & (a["phase"] == P["ENTER"]),
                      lambda a: (a["resource"] == R["RESP_BUS"]) & (a["phase"] == P["EXIT"]), ["msg_id", "kind"]),
        "DRAM":      (lambda a: (a["resource"] == R["DRAM"]) & (a["phase"] == P["ENTER"]) & (a["kind"] == K["MEM_READ"]),
                      lambda a: (a["resource"] == R["DRAM"]) & (a["phase"] == P["EXIT"]), ["msg_id"]),
        "LLC_QUEUE": (lambda a: (a["resource"] == R["LLC_QUEUE"]) & (a["phase"] == P["ENTER"]),
                      lambda a: ((a["resource"] == R["L1"]) | (a["resource"] == R["LLC"])) & (a["phase"] == P["ENTER"]),
                      ["comp", "msg_id", "kind"]),
    }

    def __init__(self, out_dir, names, a_req=2, a_res=5, a_llc=10, a_l1=0, a_svc=1, logger_base=True, part_rows=2_000_000):
        self.out_dir = out_dir; self.names = names
        self.a_req, self.a_res, self.a_llc, self.a_l1, self.a_svc = a_req, a_res, a_llc, a_l1, a_svc
        self.logger_base, self.part_rows = logger_base, part_rows
        self.carry = {g: np.zeros(0, dtype=tf.REC) for g in self.GROUPS}
        self.occ, self.fsm = [], []
        self.occ_rows = self.fsm_rows = 0
        self.occ_parts, self.fsm_parts = [], []
        self.orphans = {g: 0 for g in self.GROUPS}
        self.stats = {}
        self.fsm_total = 0

    # ---- one chunk ----
    def feed(self, a):
        res, ph, kind, comp = a["resource"], a["phase"], a["kind"], a["comp"]
        cyc = a["cycle"].astype(np.int64)
        for g, (sel_s, sel_e, keys) in self.GROUPS.items():
            m = sel_s(a) | sel_e(a)
            if not m.any() and len(self.carry[g]) == 0:
                continue
            batch = np.concatenate([self.carry[g], a[m]]) if len(self.carry[g]) else a[m]
            ie, ix, un_e, un_x = _pairs(batch, sel_s(batch), sel_e(batch), keys)
            self.orphans[g] += un_x
            self.carry[g] = batch[un_e]
            self._emit(batch, g, ie, batch["cycle"][ie].astype(np.int64), batch["cycle"][ix].astype(np.int64))
        # exit-only channels
        ie = np.flatnonzero((res == R["SVC_BUS"]) & (ph == P["EXIT"]))
        self._emit(a, "SVC_BUS", ie, cyc[ie] - self.a_svc, cyc[ie])   # INVs go out one per core cycle (a2time01/water: min gap 1)
        ie = np.flatnonzero((res == R["MEM_BUS"]) & (ph == P["EXIT"]))
        is_fill = kind[ie] == K["FILL"]
        self._emit(a, "MEM_BUS", ie, cyc[ie] - np.where(is_fill, self.a_res, self.a_req), cyc[ie], is_fill.astype(np.int32))
        # array port
        ie = np.flatnonzero((res == R["ARRAY"]) & (ph == P["SERVICE"]))
        fl = a["flags"][ie]
        pwb_merge = ((fl & 0x400) != 0) & ((fl & 0xff) == 6)
        self._emit(a, "ARRAY", ie, cyc[ie], cyc[ie] + np.where(pwb_merge, 0, np.where(comp[ie] == 10, self.a_llc, self.a_l1)))
        # DRAM writes
        iw = np.flatnonzero((res == R["DRAM"]) & (ph == P["ENTER"]) & (kind == K["MEM_WRITE"]))
        self._emit(a, "DRAM", iw, cyc[iw], cyc[iw] + 1)
        # FSM transitions
        self._fsm(a)
        if self.occ_rows >= self.part_rows: self._flush_occ()
        if self.fsm_rows >= self.part_rows: self._flush_fsm()

    def _emit(self, a, name, ie, start, end, comp_arr=None):
        if len(ie) == 0: return
        shift = -1 if (self.logger_base and name in BUS_ROLES) else 0
        fl = a["flags"][ie]
        self.occ.append((np.full(len(ie), name, dtype=object), (a["comp"][ie] if comp_arr is None else comp_arr).astype(np.int32),
                         start + shift, end + shift, a["kind"][ie].astype(np.int16), a["core"][ie].astype(np.int16),
                         a["msg_id"][ie].astype(np.int64), (fl & 0xff).astype(np.int32), ((fl >> 8) & 1).astype(np.bool_),
                         a["addr"][ie].astype(np.int64), ((fl >> 9) & 3).astype(np.int8)))
        self.occ_rows += len(ie)
        self.stats[name] = self.stats.get(name, 0) + len(ie)

    def _fsm(self, a):
        ie = np.flatnonzero(a["resource"] == R["FSM"])
        if len(ie) == 0: return
        comp = a["comp"][ie].astype(np.int32); ev = a["phase"][ie].astype(np.int32)
        old = (a["flags"][ie] >> 8).astype(np.int32); new = (a["flags"][ie] & 0xff).astype(np.int32)

        def resolve(ids, key):
            out = np.empty(len(ids), dtype=object)
            for c in np.unique(comp):
                m = comp == c; tab = self.names.get(int(c), {}).get(key, [])
                out[m] = [tab[i] if i < len(tab) else str(i) for i in ids[m]]
            return out
        kinds = np.array(tf.KIND, dtype=object)
        self.fsm.append((a["cycle"][ie].astype(np.int64), comp, a["addr"][ie].astype(np.int64),
                         (a["addr"][ie] & ~np.uint64(63)).astype(np.int64), a["msg_id"][ie].astype(np.int64),
                         kinds[a["kind"][ie]], a["core"][ie].astype(np.int16),
                         resolve(ev, "events"), resolve(old, "states"), resolve(new, "states")))
        self.fsm_rows += len(ie); self.fsm_total += len(ie)

    # ---- parts ----
    def _write(self, cols, names, prefix, parts, order):
        d = {n: np.concatenate([c[i] for c in cols]) for i, n in enumerate(names)}
        if prefix == "occupancy":   # kind is carried as the Message::Kind id until here
            d["kind"] = np.array(tf.KIND, dtype=object)[d["kind"]]
        for n in ("resource", "kind", "event", "old", "new"):
            if n in d: d[n] = d[n].astype(str)
        path = os.path.join(self.out_dir, f"{prefix}_{len(parts):03d}.parquet").replace(os.sep, "/")
        con = duckdb.connect(); con.register("t", d)
        con.execute(f"COPY (SELECT * FROM t ORDER BY {order}) TO '{path}' (FORMAT PARQUET, COMPRESSION ZSTD)")
        parts.append(path)

    def _flush_occ(self):
        if self.occ: self._write(self.occ, OCC_COLS, "occupancy", self.occ_parts, "start, resource, comp")
        self.occ, self.occ_rows = [], 0

    def _flush_fsm(self):
        if self.fsm: self._write(self.fsm, FSM_COLS, "fsm", self.fsm_parts, "cycle, comp")
        self.fsm, self.fsm_rows = [], 0

    def finish(self):
        """Flush; starts still carried have no end (killed run / window cut) and are dropped, counted."""
        self._flush_occ(); self._flush_fsm()
        pending = {g: int(len(c)) for g, c in self.carry.items() if len(c)}
        # a single part gets the plain file name the server and docs already use
        for parts, plain in ((self.occ_parts, "occupancy.parquet"), (self.fsm_parts, "fsm.parquet")):
            if len(parts) == 1:
                dst = os.path.join(self.out_dir, plain).replace(os.sep, "/")
                if os.path.exists(dst): os.remove(dst)
                os.replace(parts[0], dst); parts[0] = dst
        return pending


def clean(out_dir):
    for f in glob.glob(os.path.join(out_dir, "occupancy*.parquet")) + glob.glob(os.path.join(out_dir, "fsm*.parquet")):
        os.remove(f)


def stream(trace, out_dir, t0=None, t1=None, part_rows=2_000_000, log=print, **kw):
    """Convert a trace (or the cycle window [t0, t1] of it) into Parquet parts under out_dir."""
    os.makedirs(out_dir, exist_ok=True); clean(out_dir)
    st = Streamer(out_dir, tf.read_names(trace), part_rows=part_rows, **kw)
    n = 0; t = time.time(); chunks = 0
    for a in tf.iter_chunks(trace, t0, t1):
        n += len(a); chunks += 1; st.feed(a)
        if log and chunks % 256 == 0:
            log(f"  {n/1e6:.1f} M records, cycle {int(a['cycle'][-1])}, {len(st.occ_parts)}+{len(st.fsm_parts)} parts written, {time.time()-t:.0f} s")
    pending = st.finish()
    return st, n, pending


def occ_glob(out_dir):
    """The occupancy table as one DuckDB path (a single file or a glob over parts)."""
    single = os.path.join(out_dir, "occupancy.parquet")
    return (single if os.path.exists(single) else os.path.join(out_dir, "occupancy_*.parquet")).replace(os.sep, "/")


def fsm_glob(out_dir):
    single = os.path.join(out_dir, "fsm.parquet")
    return (single if os.path.exists(single) else os.path.join(out_dir, "fsm_*.parquet")).replace(os.sep, "/")


def check_no_double_booking(occ):
    """A resource instance must never host two overlapping intervals.
    Exempt: LLC_QUEUE (a set), DRAM (banks run in parallel; the trace has no bank id), ARRAY cut-ins
    (MSHR/PWB-resident accesses the model admits while the port is busy) and zero-width intervals."""
    con = duckdb.connect()
    return con.execute(f"""
      WITH o AS (SELECT resource, comp, start, "end", msg_id, kind,
                        lag("end") OVER (PARTITION BY resource, comp ORDER BY start, "end") AS prev_end,
                        lag(msg_id) OVER (PARTITION BY resource, comp ORDER BY start, "end") AS prev_id
                 FROM read_parquet('{occ}') WHERE resource NOT IN ('LLC_QUEUE','DRAM') AND NOT cutin AND "end" > start)
      SELECT resource, comp, count(*) AS overlaps, min(start) AS first_cycle, any_value(msg_id) AS id, any_value(prev_id) AS prev
      FROM o WHERE prev_end > start GROUP BY 1,2 ORDER BY 1,2""").fetchall()


def check_against_requests(occ, viz, t0=None, t1=None):
    """Every demand request's occupancy from the trace must agree with the intervals derived from its
    Logger row (docs/Logger.md S5). Returns {check: (rows checked, mismatches)}. With a window, only
    requests fully inside it are checked."""
    con = duckdb.connect()
    win = f" WHERE issue >= {t0} AND retire <= {t1}" if t0 is not None and t1 is not None else ""
    con.execute(f"CREATE VIEW v AS SELECT * FROM read_parquet('{viz}'){win}")
    con.execute(f"CREATE VIEW o AS SELECT * FROM read_parquet('{occ}')")
    q = lambda sql: con.execute(sql).fetchone()
    c = {}
    c["reqbus_end"] = q("""SELECT count(*), coalesce(sum(CASE WHEN o."end" <> v.e_reqb THEN 1 END),0)
        FROM v JOIN o ON o.msg_id = v.id AND o.resource='REQ_BUS' AND o.kind IN ('GETS','GETM') WHERE v.reqb > 0""")
    # (a data message with this id can also leave AFTER retirement: the line returned to the LLC
    #  when the request was invalidated while waiting -- IS_dI -- so only transfers before retire count)
    c["respbus_end"] = q("""SELECT count(*), coalesce(sum(CASE WHEN o."end" <> v.e_resp THEN 1 END),0)
        FROM v JOIN o ON o.msg_id = v.id AND o.resource='RESP_BUS' AND o.kind IN ('RESP','SUPPLY','SUPPLY_DEFERRED') AND o.core = v.core
                     AND o."end" <= v.retire
        WHERE v.resp > 0""")
    c["array_start"] = q("""SELECT count(*), coalesce(sum(CASE WHEN o.start <> v.o_array_s THEN 1 END),0)
        FROM v JOIN o ON o.msg_id = v.id AND o.resource='ARRAY' AND o.comp = 10 AND o.flags = 1 AND o.kind IN ('GETS','GETM','RESP')
        WHERE v.o_array_s IS NOT NULL AND v.llc_state IN (3,4,5)""")
    c["dram_length"] = q("""SELECT count(*), coalesce(sum(CASE WHEN (o."end"-o.start) <> v.dram THEN 1 END),0)
        FROM v JOIN o ON o.msg_id = v.id AND o.resource='DRAM' AND o.kind='MEM_READ' WHERE v.dram > 0""")
    c["fill_end"] = q("""SELECT count(*), coalesce(sum(CASE WHEN o."end" <> v.e_dram THEN 1 END),0)
        FROM v JOIN o ON o.msg_id = v.id AND o.resource='MEM_BUS' AND o.kind='FILL' WHERE v.dram > 0""")
    c["memread_leg"] = q("""SELECT count(*), coalesce(sum(CASE WHEN NOT (m."end" >= v.s_membus AND m."end" <= d.start) THEN 1 END),0)
        FROM v JOIN o m ON m.msg_id = v.id AND m.resource='MEM_BUS' AND m.kind='MEM_READ'
               JOIN o d ON d.msg_id = v.id AND d.resource='DRAM' AND d.kind='MEM_READ' WHERE v.dram > 0""")
    return c


def report(st, n, pending, occ, fsm, viz=None, t0=None, t1=None, log=print):
    log(f"{occ}: {sum(st.stats.values())} intervals from {n} records in {len(st.occ_parts)} part(s)")
    log(f"{fsm}: {st.fsm_total} coherence transitions in {len(st.fsm_parts)} part(s)")
    for name in sorted(st.stats):
        log(f"  {name:10s} {st.stats[name]:10d}")
    if pending: log(f"  starts without an end (trace cut / window edge), dropped: {pending}")
    orph = {g: v for g, v in st.orphans.items() if v}
    if orph: log(f"  ends without a start (window edge): {orph}")
    dbl = check_no_double_booking(occ)
    log("  double-booking: " + ("none" if not dbl else str(dbl)))
    if viz:
        for k, (cnt, bad) in check_against_requests(occ, viz, t0, t1).items():
            log(f"  {k:12s} checked={cnt} mismatches={bad}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("trace")
    ap.add_argument("-o", "--out", help="output directory (default: the trace's directory)")
    ap.add_argument("--t0", type=int); ap.add_argument("--t1", type=int, help="convert only this cycle window (uses the chunk index)")
    ap.add_argument("--part-rows", type=int, default=2_000_000)
    ap.add_argument("--validate", help="octoviz.parquet of the same run")
    ap.add_argument("--a-req", type=int, default=2); ap.add_argument("--a-res", type=int, default=5)
    ap.add_argument("--a-llc", type=int, default=10); ap.add_argument("--a-l1", type=int, default=0)
    ap.add_argument("--a-svc", type=int, default=1, help="service-channel (INV) transfer width in core cycles")
    ap.add_argument("--global-clock", action="store_true", help="keep bus intervals on the global clock (no -1 Logger alignment)")
    a = ap.parse_args()
    out_dir = a.out or os.path.dirname(os.path.abspath(a.trace))
    st, n, pending = stream(a.trace, out_dir, a.t0, a.t1, a.part_rows, a_req=a.a_req, a_res=a.a_res, a_llc=a.a_llc, a_l1=a.a_l1, a_svc=a.a_svc, logger_base=not a.global_clock)
    report(st, n, pending, occ_glob(out_dir), fsm_glob(out_dir), a.validate and a.validate.replace(os.sep, "/"), a.t0, a.t1)


if __name__ == "__main__":
    main()
