#!/usr/bin/env python3
"""Optional stress test: 300,000 files, checks (1) duplicate results against an independent
Python/hashlib reference and (2) peak memory of the duplicate search.

    python3 tests/stress.py ./cna [N_FILES]       needs ~1.2 GB temp disk for 300k files
Peak RSS is measured with resource.getrusage (Linux/macOS only); on Windows use Task Manager.
"""
import collections, hashlib, json, os, random, shutil, subprocess, sys, tempfile
CNA = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "./cna")
N = int(sys.argv[2]) if len(sys.argv) > 2 else 300000
root = tempfile.mkdtemp(prefix="cna-stress-")
data = os.path.join(root, "data")
env = dict(os.environ, CNA_HOME=os.path.join(root, "state"))
try:
    random.seed(1)
    pool = [os.urandom(random.randint(50, 3000)) for _ in range(2000)]
    per_dir, n = 200, 0
    for d in range(N // per_dir):
        dp = os.path.join(data, "d%d" % (d // 50), "s%d" % (d % 50))
        os.makedirs(dp, exist_ok=True)
        for i in range(per_dir):
            blob = random.choice(pool) if random.random() < 0.5 else os.urandom(random.randint(50, 3000))
            with open(os.path.join(dp, "f%d.bin" % i), "wb") as f: f.write(blob)
            n += 1
    print("created", n, "files")
    by = collections.defaultdict(list)
    for dp, _, fs in os.walk(data):
        for f in fs:
            p = os.path.join(dp, f)
            with open(p, "rb") as fh: by[hashlib.sha256(fh.read()).hexdigest()].append(os.path.getsize(p))
    ref_sets = sum(1 for s in by.values() if len(s) > 1)
    ref_wasted = sum(s[0] * (len(s) - 1) for s in by.values() if len(s) > 1)
    p = subprocess.run([CNA, "dupes", data, "--min-size", "1", "--json"], env=env, capture_output=True, text=True)
    out = json.loads(p.stdout)
    try:
        import resource
        peak = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
        peak_mb = peak / (1024 * 1024 if sys.platform == "darwin" else 1024)
    except ImportError:
        peak_mb = float("nan")
    print("reference : sets=%d wasted=%d" % (ref_sets, ref_wasted))
    print("cna       : sets=%d wasted=%d" % (len(out["sets"]), out["total_wasted"]))
    print("peak RSS  : %.1f MB (cap 125 MB)" % peak_mb)
    ok = ref_sets == len(out["sets"]) and ref_wasted == out["total_wasted"] and not (peak_mb > 125)
    print("PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)
finally:
    shutil.rmtree(root, ignore_errors=True)
