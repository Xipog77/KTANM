import os
import glob

print("=== POSTGRESQL HANG FILES ===")
pg_hangs = glob.glob("outputs/pg_fuzzer*/hangs/id:*")
pg_hangs = [f for f in pg_hangs if os.path.getsize(f) > 0]
for path in sorted(pg_hangs)[:5]:
    try:
        with open(path, "r", errors="ignore") as f:
            print(f"File: {path}")
            print(f"Query: {f.read().strip()}")
            print("-" * 60)
    except Exception as e:
        print(f"Error reading {path}: {e}")

print("\n=== MYSQL HANG FILES ===")
mysql_hangs = glob.glob("outputs/mysql_fuzzer*/hangs/id:*")
mysql_hangs = [f for f in mysql_hangs if os.path.getsize(f) > 0]
for path in sorted(mysql_hangs)[:5]:
    try:
        with open(path, "r", errors="ignore") as f:
            print(f"File: {path}")
            print(f"Query: {f.read().strip()}")
            print("-" * 60)
    except PermissionError:
        print(f"File: {path} (Permission Denied: Please run this script with sudo to read MySQL files)")
        print("-" * 60)
    except Exception as e:
        print(f"Error reading {path}: {e}")
