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


def member_forms(n):
	"""The probe lines that name a member `A::B::c`, best first.

	TWO FORMS ARE NEEDED AND NEITHER COVERS THE OTHER'S CASE. `decltype(X)`
	names an ENUMERATOR, a static member, a data member and a free function
	in a nested namespace; `decltype(&X)` is the only way to name a
	NON-STATIC MEMBER FUNCTION and is ill-formed for an enumerator, because
	an enumerator has no address. The gate knew only the second, so it could
	never accept a documented enumerator -- and `Qtty::Color`'s three kinds,
	which every consumer reads a cell's `fg` and `bg` through, are
	enumerators.

	Bare goes first because it covers everything this tree documents:
	measured 2026-11-03, all 87 documented symbols resolve in ONE
	translation unit that way. The address form is the fallback the
	per-symbol path tries, so a document naming a non-static member function
	still passes -- by the slow route, and only that one name costs it.

	An earlier version of this chose the order by looking for the leaf name
	followed by `(` in the headers. That is a worse idea than it looks: the
	leaf is matched anywhere in any header, `Default` matches a `Default (`
	that has nothing to do with `Color::Default`, and the whole-set compile
	then fails while every symbol still resolves alone -- the one failure
	mode that reports no culprit. No heuristic is better than a cheap one
	here, because the compiler is the authority either way.
	"""
	return ["using X = decltype(%s);" % n, "using X = decltype(&%s);" % n]


def compiles(cxx, flags, names, form=0):
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
	# A MEMBER cannot be introduced by a using-declaration at namespace
	# scope -- `using Qtty::GridMetrics::ch;` is ill-formed however public
	# the member is -- so a document naming one made this gate refuse code
	# that was reachable all along. Members get a decltype instead, which
	# asks the same question (is this name declared) of a function or a data
	# member alike.
	#
	# The limit, written down rather than met later: decltype of an address
	# is AMBIGUOUS for an overloaded member, so a document naming one would
	# be reported unreachable. No document names one today; the next one to
	# do so gets this comment rather than a puzzle.
	lines = []
	for i, n in enumerate(names):
		if n.count("::") == 2:
			forms = member_forms(n)
			line = forms[min(form, len(forms) - 1)]
		else:
			line = "using %s;" % n
		lines.append("namespace probe_%d { %s }" % (i, line))
	body = "\n".join(lines)
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


def culprits(cxx, flags, names):
	"""The documented names no probe form can reach, found by bisection.

	WHY NOT THE LINEAR SCAN THIS REPLACES. It compiled one translation unit
	per documented symbol -- 87 of them -- and it ran on exactly the
	occasion somebody is waiting for the answer. Measured twice on
	2026-11-03: naming a single culprit took about five minutes, and one run
	hit its 300-second timeout before finishing and so reported nothing at
	all. Bisection costs O(k log n) for k culprits: about seven compiles for
	one, about twenty-eight for four.

	TERMINATION. Each call either returns without recursing, or splits into
	two strictly smaller lists; a list of one is the base case and compiles
	at most twice. So the recursion depth is at most log2(len(names)) + 1 and
	the number of compiles is bounded by 2 * len(names) in the worst case,
	which is the case where every name is unreachable.

	Both forms before condemning a single name: the batch probes with form 0,
	and a member needing form 1 is reachable rather than missing.
	"""
	if compiles(cxx, flags, names, 0)[0]:
		return []
	if len(names) == 1:
		return [] if compiles(cxx, flags, names, 1)[0] else list(names)
	mid = len(names) // 2
	return (culprits(cxx, flags, names[:mid])
	        + culprits(cxx, flags, names[mid:]))


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


def unreadable_settings():
	"""Documented setters whose public getter no document names.

	The docs are checked one way round -- every `Qtty::` name they use must
	compile against the public headers -- and that direction cannot see the
	opposite fault: a setting an application is told how to WRITE and not
	how to READ. The write side is what prose naturally covers, because it
	is what an example does; the read side is what a program needs when it
	wants to show the value, reason about it, or find out what somebody
	else set, and a process-wide setting has no other route to it.

	It is not hypothetical here twice over. 8.628 is this tree's own worked
	example reinventing an API it was demonstrating, and the same week a
	session hand-rolled a key-press helper that `testing.h` already
	shipped. Both read the prose and not the header. Reported from
	claude-guidelines the same day for a tool rather than a library, in the
	same shape: `running all` answered "whose is this job" the whole time,
	nothing a reader loads named it, and the session invented a glob that
	could not work.

	Deliberately narrow, so that it stays a finding rather than a list.
	Only a setter the docs ALREADY name counts -- documenting the write
	side is what creates the obligation -- and only where a public getter
	of the matching name exists, since there is nothing to say where the
	value cannot be read. Measured when this was written: 29 public
	setters, 4 of them documented with an undocumented getter, and three
	other undocumented readers correctly left out because their own write
	side is unmentioned too (`terminal_palette`, `is_visible`,
	`covers_terminal`).
	"""
	inc = os.path.join(ROOT, "include", "qtty")
	decls = ""
	for header in sorted(h for h in os.listdir(inc) if h.endswith(".h")):
		with open(os.path.join(inc, header), encoding="utf-8") as f:
			decls += f.read()
	prose = ""
	for rel in DOCS:
		path = os.path.join(ROOT, rel)
		if os.path.exists(path):
			with open(path, encoding="utf-8") as f:
				prose += f.read()

	def declared(name):
		return re.search(r"\b%s\s*\(" % re.escape(name), decls) is not None

	def named(name):
		return re.search(r"\b%s\b" % re.escape(name), prose) is not None

	gaps = []
	for setter in sorted(set(re.findall(r"\b(set_[a-z0-9_]+)\s*\(", decls))):
		getter = setter[len("set_"):]
		if not declared(getter):
			continue
		if named(setter) and not named(getter):
			gaps.append((setter, getter))
	return gaps

def practice_count():
	"""How many numbered practices the guide has, counted from the guide."""
	path = os.path.join(ROOT, "doc", "keyboard-first.md")
	with open(path, encoding="utf-8") as f:
		return len(re.findall(r"^\*\*(\d+)\. ", f.read(), re.M))


def practice_numbering():
	"""Where the guide's numbered practices disagree with themselves.

	The practices are the guide's spine and they are numbered by hand, so
	three things can go wrong silently: a number can repeat, the run can
	skip one, and -- the case that produced this check -- prose elsewhere
	can name a count the list has since outgrown.

	Measured 2026-09-19: the section's own map said "only thirteen, which
	is ordinary advice again, comes after them" while practice 14 had
	existed for a day. A reader reaching twelve is told one more follows,
	stops, and misses the newest practice in the document -- which was the
	guidance for the newest feature area in the tree.

	`git log -S` on that sentence says it was TRUE when written and was
	falsified by a later commit that added a practice without touching it.
	That is the shape worth gating rather than re-sweeping for: the numbers
	are re-derivable from the list itself, so nothing here is a second copy
	of a fact -- it is the fact, checked against every prose claim about it.

	Returns a list of complaints, empty when the numbering is sound.
	"""
	path = os.path.join(ROOT, "doc", "keyboard-first.md")
	with open(path, encoding="utf-8") as f:
		text = f.read()
	seen = [int(n) for n in re.findall(r"^\*\*(\d+)\. ", text, re.M)]
	out = []
	if not seen:
		return ["the guide names no numbered practice at all, so this"
		        " checked nothing"]
	want = list(range(1, len(seen) + 1))
	if sorted(seen) != want:
		out.append("the practices are numbered %s, which is not 1..%d"
		           % (", ".join(str(n) for n in seen), len(seen)))
	# WHAT THIS DELIBERATELY DOES NOT CHECK, and the reason is worth the
	# paragraph because the first version of this gate did try.
	#
	# The prose above the list also counts the practices -- "only thirteen
	# ... comes after them" -- and that sentence was the rot this gate was
	# written for. Gating it by matching the phrase failed twice in five
	# minutes. First it matched nothing, the document being wrapped at 75
	# columns so the claim falls across a line break, which is the
	# wrapped-phrase trap `evidence.md` names. Then, with the text
	# normalised, it went green against a sentence rewritten to say "only
	# FOURTEEN comes after them" -- which satisfies the gate and denies
	# that thirteen comes after twelve. A gate a reword can satisfy is
	# worse than no gate, because the deformation outlives it and nothing
	# complains again; `evidence.md` says exactly that, and this is the
	# rule catching its own author.
	#
	# So the numbering is gated, being derived from the list and
	# ungameable, and the prose claim is left to a reader. The honest
	# statement of the limit is here rather than in a green line implying
	# it was covered.
	return out


def main():
	cxx = os.environ.get("CXX", "g++")
	flags = qt_cflags()
	if flags is None:
		say("guide-check: pkg-config cannot describe Qt6Widgets, so the"
		    " public headers cannot be compiled.")
		say("             Refusing rather than skipping: every machine that"
		    " builds this project has them.")
		return 1

	numbering = practice_numbering()
	if numbering:
		say("guide-check: the guide's numbered practices disagree with"
		    " themselves:")
		for line in numbering:
			say("    %s" % line)
		return 1

	unreadable = unreadable_settings()
	if unreadable:
		say("guide-check: %d documented setter(s) whose getter no document"
		    " names, so an application is told how to write the setting and"
		    " not how to read it:" % len(unreadable))
		for setter, getter in unreadable:
			say("    Qtty::%s() is documented; Qtty::%s() is not"
			    % (setter, getter))
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
		    " public headers, and its practices number 1..%d without a"
		    " gap or a repeat"
		    % (len(symbols), ", ".join(DOCS), practice_count()))
		return 0

	# Name the culprits rather than printing a wall of compiler output.
	missing = culprits(cxx, flags, symbols)
	if missing:
		say("guide-check: the documentation names %d symbol(s) an"
		    " application cannot reach:" % len(missing))
		for sym in missing:
			say("    %s" % sym)
		return 1

	# AND THIS IS A PASS, where it used to be a failure. The question this
	# gate asks is whether every documented name is reachable, and the line
	# above has just answered yes for all of them -- so a whole-set
	# translation unit that will not compile is a fault in THIS PROBE's
	# choice of form, not in the documents. It used to `return 1` here, and
	# that false failure was met on 2026-11-03: `Qtty::Color::Default` needs
	# `decltype(X)` where the batch had guessed `decltype(&X)`, every symbol
	# resolved alone, and the gate went red with nothing wrong. member_forms()
	# now orders the forms so that everything documented today batches in one
	# compile, and the comment there says a non-static member function would
	# re-enter this branch -- which must not be a failure when it does.
	#
	# The compiler's own words are kept, because somebody should still look:
	# a whole-set failure that is not explained by any single name is worth
	# reading even when it cannot make the documents wrong.
	say("guide-check: all %d documented symbol(s) are reachable, and the"
	    " whole-set translation unit did not compile -- which is this"
	    " probe's form guess rather than a fault in the documents, so it"
	    " passes. See member_forms(). The compiler said:" % len(symbols))
	say(err.strip()[:2000])
	return 0


if __name__ == "__main__":
	sys.exit(main())
