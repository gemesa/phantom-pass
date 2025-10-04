from z3 import *
import time
import sys


def obfuscate_mba(expr, bit_width=16, bounds=20000):
    x, y = BitVecs("x y", bit_width)

    if isinstance(expr, int):
        original = BitVecVal(expr, bit_width)
        hide_const = expr
    else:
        original = expr
        hide_const = None

    c1, c2, c3, c4, c5, c6 = Ints("c1 c2 c3 c4 c5 c6")

    mba = Int2BV(
        c1 * BV2Int(x)
        + c2 * BV2Int(y)
        + c3 * BV2Int(x & y)
        + c4 * BV2Int(x | y)
        + c5 * BV2Int(x ^ y)
        + c6,
        bit_width,
    )

    solver = Solver()

    tests = [
        (0, 0),
        (1, 0),
        (0, 1),
        (1, 1),
        (2, 3),
        (5, 3),
        (7, 5),
        (10, 6),
        (15, 8),
        (12, 9),
    ]

    for x_val, y_val in tests:
        orig = substitute(
            original,
            [(x, BitVecVal(x_val, bit_width)), (y, BitVecVal(y_val, bit_width))],
        )
        mba_val = substitute(
            mba, [(x, BitVecVal(x_val, bit_width)), (y, BitVecVal(y_val, bit_width))]
        )
        solver.add(orig == mba_val)

    if hide_const is not None:
        solver.add(c6 != hide_const)

    for c in [c1, c2, c3, c4, c5, c6]:
        solver.add(c >= -bounds, c <= bounds)

    start = time.time()
    if solver.check() == sat:
        m = solver.model()
        terms = []
        if m[c1].as_long() != 0:
            terms.append(f"{m[c1]}*x")
        if m[c2].as_long() != 0:
            terms.append(f"{m[c2]}*y")
        if m[c3].as_long() != 0:
            terms.append(f"{m[c3]}*(x&y)")
        if m[c4].as_long() != 0:
            terms.append(f"{m[c4]}*(x|y)")
        if m[c5].as_long() != 0:
            terms.append(f"{m[c5]}*(x^y)")
        if m[c6].as_long() != 0:
            terms.append(str(m[c6].as_long()))

        result = " + ".join(terms).replace("+ -", "- ")
        print(f"TGT: {expr_str}")
        print(f"MBA: {result}")
        print(f"({time.time()-start:.2f}s)")
        return result
    else:
        print(f"No solution ({time.time()-start:.2f}s)")
        return None


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python mba.py <expr> [bit_width] [bounds]")
        print("Examples:")
        print("  python mba.py 42")
        print("  python mba.py 42 8")
        print("  python mba.py 42 16 10000")
        print("  python mba.py 'x+y'")
        print("  python mba.py 'x+y' 16 20000")
        print("  python mba.py 'x-y' 8 500")
        sys.exit(1)

    expr_str = sys.argv[1]
    bit_width = int(sys.argv[2]) if len(sys.argv) > 2 else 16
    bounds = int(sys.argv[3]) if len(sys.argv) > 3 else 20000

    x, y = BitVecs("x y", bit_width)

    try:
        if expr_str.lstrip("-").isdigit():
            obfuscate_mba(int(expr_str), bit_width, bounds)
        else:
            expr = eval(expr_str)
            obfuscate_mba(expr, bit_width, bounds)
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)
