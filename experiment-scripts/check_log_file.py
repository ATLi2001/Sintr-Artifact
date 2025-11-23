import re
import sys

# Match the delete statement line robustly
pattern = re.compile(
    r"Delete statement:\s*(DELETE FROM .*?;)\s*.*?Commit/Prepare:\s*([01])"
)

def normalize_sql(sql):
    """Normalize whitespace and case to compare delete statements reliably."""
    sql = re.sub(r"\s+", " ", sql.strip()).lower()
    return sql

def check_delete_order(logfile):
    seen = {}   # {normalized_delete_statement: (last_commit_value, line_number)}
    errors = []

    with open(logfile, "r", encoding="utf-8", errors="ignore") as f:
        for lineno, line in enumerate(f, start=1):
            match = pattern.search(line)
            if not match:
                continue

            delete_stmt, commit_value = match.groups()
            commit_value = int(commit_value)
            norm_stmt = normalize_sql(delete_stmt)

            if norm_stmt in seen:
                prev_commit, prev_line = seen[norm_stmt]
                if prev_commit == 1 and commit_value == 0:
                    errors.append(
                        f"Order error for delete statement:\n  {delete_stmt}\n"
                        f"  -> Commit/Prepare:1 at line {prev_line}\n"
                        f"  -> Commit/Prepare:0 at line {lineno}\n"
                    )
            seen[norm_stmt] = (commit_value, lineno)

    if errors:
        print("\n".join(errors))
    else:
        print("✅ No ordering issues found.")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python check_commit_order.py <path-to-logfile>")
        sys.exit(1)

    check_delete_order(sys.argv[1])
