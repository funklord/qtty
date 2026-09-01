#!/usr/bin/env python3
"""design.md section 7's enforcement rule, as a gate.

Shared view code must not hardcode margins, spacing or pixel sizes: those
are the calls that make a GUI layout unportable, and on a cell grid they
are the calls that put a widget off it.

    A CI check bans setContentsMargins, setSpacing, setFixedSize, and
    setFixedWidth under src/ui/shared/, with an explicit `// qtty-allow:`
    escape comment requiring a reason.  -- design.md section 7

Two things about that sentence had to change to make a gate worth having,
and both came from running it over this tree first.

**It bans non-zero LITERALS, not the calls.** Every one of the 47 call
sites in this repository passes zero -- `setContentsMargins(0, 0, 0, 0)`
and `setSpacing(0)` -- and zero is not what the rule is about. A pixel
margin of 8 is unportable; "no margin" means the same on both targets and
is exactly what a cell grid wants, which is why the example says
`// window edge = cell edge` beside it. A gate that flagged all 47 would
be turned off within the day, which this project has already written down
about a timing threshold: a check that fails for reasons that are not the
code's is one somebody disables.

An expression is not a literal either. `setFixedWidth(20 * cw)` is
cell-derived and portable by construction; only a bare number is flagged.

**The path is an argument.** There is no `src/ui/shared/` here -- qtty is
the library, and the shared view code it is about lives in whatever
application uses it. The gate takes paths so an application can point it
at its own, and this tree points it at the UI it does have.

Usage:  layout_gate.py PATH...        (exit 1 on any violation)
"""

import re
import sys

BANNED = ("setContentsMargins", "setSpacing", "setFixedSize",
          "setFixedWidth", "setFixedHeight")

# An argument that is ENTIRELY a number, which is the distinction that keeps
# this gate usable. Searching the argument list for digits flags `20 * cw`
# on its 20 -- cell arithmetic, portable by construction, and precisely what
# the rule wants people to write. Measured on a fixture: that false positive
# was there in the first version, and the docstring claimed the opposite.
LITERAL = re.compile(r"^[+-]?\d+(?:\.\d+)?[fF]?$")

ALLOW = "qtty-allow:"


def arguments(text, start):
	"""The text between the parentheses that follow `start`, or None."""
	open_at = text.find("(", start)
	if open_at < 0:
		return None
	depth = 0
	for i in range(open_at, len(text)):
		if text[i] == "(":
			depth += 1
		elif text[i] == ")":
			depth -= 1
			if depth == 0:
				return text[open_at + 1:i]
	return None


def split_args(args):
	"""Top-level comma-separated arguments, stripped. Nested calls and
	template commas stay inside one argument, so QSize(20, 1) is a single
	argument and is not a bare literal -- deliberately conservative, because
	a gate with false positives is a gate somebody turns off."""
	out, depth, last = [], 0, 0
	for i, c in enumerate(args):
		if c in "([{<":
			depth += 1
		elif c in ")]}>":
			depth -= 1
		elif c == "," and depth == 0:
			out.append(args[last:i].strip())
			last = i + 1
	out.append(args[last:].strip())
	return [a for a in out if a]


def allowed(lines, n):
	"""An escape comment on this line or the one above, WITH a reason."""
	for line in (lines[n], lines[n - 1] if n else ""):
		at = line.find(ALLOW)
		if at >= 0 and line[at + len(ALLOW):].strip():
			return True
	return False


def check(path):
	try:
		with open(path, encoding="utf-8") as f:
			lines = f.read().split("\n")
	except (OSError, UnicodeDecodeError) as e:
		print("%s: %s" % (path, e), file=sys.stderr)
		return 1

	bad = 0
	for n, line in enumerate(lines):
		for call in BANNED:
			at = line.find(call)
			if at < 0:
				continue
			args = arguments(line, at)
			if args is None:            # split across lines: not judged here
				continue
			hard = [a for a in split_args(args)
			        if LITERAL.match(a) and float(a.rstrip("fF")) != 0]
			if not hard or allowed(lines, n):
				continue
			print("%s:%d: %s hardcodes %s -- use cells, or say why with "
			      "// %s <reason>" % (path, n + 1, call, ", ".join(hard), ALLOW))
			bad += 1
	return bad


def main(argv):
	if len(argv) < 2:
		print(__doc__.strip().split("\n")[-1], file=sys.stderr)
		return 2
	bad = sum(check(p) for p in argv[1:])
	if bad:
		print("%d hardcoded layout value(s)" % bad, file=sys.stderr)
	return 1 if bad else 0


if __name__ == "__main__":
	sys.exit(main(sys.argv))
