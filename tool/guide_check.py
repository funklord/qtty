#!/usr/bin/env python3
"""Check that every Qtty:: symbol the documentation names is one an
application can actually reach.

The documentation is read by people who cannot test what it says. A guide
that names a function which has since been renamed, moved into src/, or was
never public at all is worse than one that says nothing: the reader goes
looking, fails, and concludes they have misunderstood.

This asks the only question that settles it -- does a translation unit which
includes the PUBLIC headers and nothing else compile against that name. Those
headers are exactly what `make install` copies, so the set read here is the
set an application gets.

It is the shape count-check already uses for the suite total: a claim made in
prose, checked against the thing it describes, by a tool rather than by
somebody remembering.

TERMINATION. It reads a fixed list of documents, extracts a finite set of
symbols, and compiles at most len(symbols) + 2 translation units. No loop is
bounded by anything else, nothing is spawned in the background, and every
compile is a single -fsyntax-only invocation under a timeout.

THE CONTROL. A probe whose failure mode is silence has to show it can speak,
so a deliberately absent name is compiled first and MUST fail. If it does not,
the compile line proves nothing -- a missing include path answers "not
declared" for everything, and a malformed invocation can answer success for
everything -- so no result below would mean anything and this refuses instead.
"""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS = ["doc/keyboard-first.md", "README.md"]
# A name that is not a symbol and never will be. A real-but-private symbol
# would work today and rot the moment somebody made it public, which would
# stop the gate rather than the code.
ABSENT = "Qtty::guide_check_control_absent"
SYMBOL = re.compile(r"Qtty::[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)?")


def say(line):
	sys.stdout.write(line + "\n")
	sys.stdout.flush()


def qt_cflags():
	try:
		out = subprocess.run(["pkg-config", "--cflags", "Qt6Widgets"],
		                     capture_output=True, text=True, timeout=30)
	except (OSError, subprocess.SubprocessError):
		return None
	return out.stdout.split() if out.returncode == 0 else None


def compiles(cxx, flags, names):
	"""True when a translation unit naming every symbol in `names` compiles.

	A using-declaration, because it is the one form that names a TYPE, a
	function and an OVERLOAD SET alike while asking only whether the name is
	declared. The first version took the address of each symbol, which cannot
	be done to a class and is ambiguous for an overloaded function, so it
	reported InputRouter, NullBackend and exec as unreachable -- every one of
	them public. The control could not catch that: an absent name went on
	failing correctly the whole time, the probe being capable and misaimed
	rather than broken.
	"""
	body = "\n".join("namespace probe_%d { using %s; }" % (i, n)
	                 for i, n in enumerate(names))
	src = "#include <qtty/qtty.h>\n#include <QWidget>\n" + body + "\n"
	with tempfile.TemporaryDirectory() as tmp:
		path = os.path.join(tmp, "probe.cpp")
		with open(path, "w") as f:
			f.write(src)
		argv = [cxx, "-std=c++17", "-fPIC", "-fsyntax-only"]
		argv += ["-I", os.path.join(ROOT, "include")] + flags + [path]
		done = subprocess.run(argv, capture_output=True, text=True,
		                      timeout=300)
		return done.returncode == 0, done.stderr


def umbrella_gaps():
	"""Public headers that qtty.h does not reach.

	A header the umbrella does not name is unreachable to everybody who
	includes qtty.h, which the documented examples do, however public the
	file is. Two were missing when this was written: delegate.h, which the
	README lists among the things an application may ask for beside four
	siblings that were included; and version.h, which calls itself "the
	surface that reaches a person" for the copyright line a consuming
	program prints. Both were installed, both were listed in src.pro, and
	neither could be reached by the documented include.
	"""
	inc = os.path.join(ROOT, "include", "qtty")
	with open(os.path.join(inc, "qtty.h"), encoding="utf-8") as f:
		included = set(re.findall(r'#include\s+"([^"]+)"', f.read()))
	here = [h for h in os.listdir(inc) if h.endswith(".h")]
	return sorted(h for h in here if h != "qtty.h" and h not in included)


def main():
	cxx = os.environ.get("CXX", "g++")
	flags = qt_cflags()
	if flags is None:
		say("guide-check: pkg-config cannot describe Qt6Widgets, so the"
		    " public headers cannot be compiled.")
		say("             Refusing rather than skipping: every machine that"
		    " builds this project has them.")
		return 1

	absent = umbrella_gaps()
	if absent:
		say("guide-check: include/qtty/qtty.h does not reach %d public"
		    " header(s), so an application that includes it cannot use"
		    " them:" % len(absent))
		for header in absent:
			say("    %s" % header)
		return 1

	names = set()
	for rel in DOCS:
		path = os.path.join(ROOT, rel)
		if not os.path.exists(path):
			say("guide-check: %s is named here and is not in the tree." % rel)
			return 1
		with open(path, encoding="utf-8") as f:
			names.update(SYMBOL.findall(f.read()))
	symbols = sorted(names)
	if not symbols:
		say("guide-check: the documents name no Qtty:: symbol at all, so"
		    " this checked nothing. Refusing.")
		return 1

	ok, _ = compiles(cxx, flags, [ABSENT])
	if ok:
		say("guide-check: THE CONTROL PASSED. A name that does not exist"
		    " compiled, so the compile line proves nothing and")
		say("             no result below would mean anything. Refusing.")
		return 1

	ok, err = compiles(cxx, flags, symbols)
	if ok:
		say("guide-check: %d symbol(s) named in %s are reachable from the"
		    " public headers" % (len(symbols), ", ".join(DOCS)))
		return 0

	# Name the culprits rather than printing a wall of compiler output.
	missing = [s for s in symbols if not compiles(cxx, flags, [s])[0]]
	if missing:
		say("guide-check: the documentation names %d symbol(s) an"
		    " application cannot reach:" % len(missing))
		for sym in missing:
			say("    %s" % sym)
	else:
		say("guide-check: every symbol resolves alone, so the whole-set"
		    " translation unit failed for another reason:")
		say(err.strip()[:2000])
	return 1


if __name__ == "__main__":
	sys.exit(main())
