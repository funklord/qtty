#!/usr/bin/env python3
"""The public hygiene contract, which nothing asserted.

design.md 10.1 and the README state it in one sentence -- "everything in
namespace Qtty, no public macros, inert in GUI builds" -- and the first two
halves are properties of the installed headers that a grep can hold. The
third is behavioural and the suite already asserts it.

It matters more here than in a library people link: qtty is VENDORED by its
consumers, so a macro or a global symbol that escapes these headers lands in
somebody else's translation units, and the first they know of it is a
collision in code that has nothing to do with this project.

Written against the headers' real shape rather than a guess at it, which is
why it tracks braces instead of columns: declarations inside `namespace
Qtty {` are NOT indented in this tree, and Qt's own forward declarations
(`class QWidget;`) sit at column 0 OUTSIDE the namespace and must. A column
test would call every legitimate class a leak and every forward declaration
one too.

Both directions, and the counts are asserted: a gate over an empty file list
reports success exactly as loudly as a real pass.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PUBLIC = os.path.join(ROOT, "include", "qtty")

# Qt's own forward declarations, which belong outside the namespace: a
# header that needs QWidget declares it the way Qt's own headers do rather
# than including the world. Narrow on purpose -- a capital Q and nothing
# of ours starts that way.
FORWARD = re.compile(r"^\s*(class|struct)\s+Q[A-Za-z0-9_]*\s*;\s*$")
# A declaration that would own a name at file scope.
DECLARES = re.compile(r"^\s*(class|struct|union|enum|template|typedef|using|"
					  r"extern|inline|static|constexpr|[A-Za-z_][\w:<>,*&\s]*"
					  r"\s+[A-Za-z_]\w*\s*\()")


def strip_noise(text):
	"""Comments and string literals out, so a brace inside either is not
	counted. Done in one pass rather than with three regexes, because a
	`//` inside a string and a quote inside a comment each defeat the
	naive order."""
	out, i, n = [], 0, len(text)
	while i < n:
		c = text[i]
		if c == "/" and i + 1 < n and text[i + 1] == "/":
			j = text.find("\n", i)
			i = n if j < 0 else j
		elif c == "/" and i + 1 < n and text[i + 1] == "*":
			j = text.find("*/", i + 2)
			out.append(" " * 0)
			i = n if j < 0 else j + 2
		elif c in "\"'":
			q, j = c, i + 1
			while j < n and text[j] != q:
				j += 2 if text[j] == "\\" else 1
			out.append(" ")
			i = j + 1
		else:
			out.append(c)
			i += 1
	return "".join(out)


def check(path):
	"""(macro findings, escape findings) for one header."""
	macros, escapes = [], []
	raw = open(path, encoding="utf-8", errors="replace").read()
	for n, line in enumerate(raw.split("\n"), 1):
		if re.match(r"^\s*#\s*define\b", line):
			macros.append((n, line.strip()))

	depth = 0
	for n, line in enumerate(strip_noise(raw).split("\n"), 1):
		bare = line.strip()
		if depth == 0 and bare and not bare.startswith("#") \
		   and not bare.startswith("QT_") and not FORWARD.match(line) \
		   and not bare.startswith("namespace") and bare not in ("{", "}") \
		   and DECLARES.match(line):
			escapes.append((n, bare[:72]))
		depth += line.count("{") - line.count("}")
		if depth < 0:
			depth = 0
	return macros, escapes


def main():
	if not os.path.isdir(PUBLIC):
		print("hygiene-check: %s is not a directory, so this would pass"
			  " against nothing" % PUBLIC, file=sys.stderr)
		return 2
	headers = sorted(f for f in os.listdir(PUBLIC) if f.endswith(".h"))
	if not headers:
		print("hygiene-check: no public header found, so this would pass"
			  " against nothing", file=sys.stderr)
		return 2

	bad = 0
	for h in headers:
		macros, escapes = check(os.path.join(PUBLIC, h))
		for n, text in macros:
			print("hygiene-check: %s:%d defines a public macro: %s"
				  % (h, n, text), file=sys.stderr)
			bad += 1
		for n, text in escapes:
			print("hygiene-check: %s:%d declares a name outside"
				  " namespace Qtty: %s" % (h, n, text), file=sys.stderr)
			bad += 1
	if bad:
		print("hygiene-check: %d finding(s). design.md 10.1 says everything"
			  " is in" % bad, file=sys.stderr)
		print("               namespace Qtty with no public macros, and"
			  " these headers are", file=sys.stderr)
		print("               vendored into other projects' translation"
			  " units.", file=sys.stderr)
		return 1
	print("hygiene-check: %d public header(s), no public macro and no name"
		  " outside namespace Qtty" % len(headers))
	return 0


if __name__ == "__main__":
	sys.exit(main())
