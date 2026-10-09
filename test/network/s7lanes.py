import duckdb, glob, sys
p = [x.replace(chr(92), "/") for x in glob.glob(sys.argv[1] + "/**/occupancy.parquet", recursive=True)]
print(duckdb.connect().execute("SELECT count(DISTINCT comp) FROM read_parquet(%r) WHERE resource = %r" % (p[0], "NOC")).fetchone()[0] if p else "no-parquet")
