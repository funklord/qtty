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

import contextlib
import io
import os
import re
import sys
import tempfile

# design.md section 7 names four calls and the gate has always carried five,
# `setFixedHeight` being `setFixedWidth`'s obvious sibling. The rest of this
# list is the same step taken deliberately, because the RULE in that section
# is prose -- "must not hardcode margins, spacing or pixel sizes" -- and its
# parenthetical was a list of examples rather than the rule. Banning
# `setFixedWidth(200)` while allowing `setMinimumWidth(200)` is incoherent:
# on a cell grid both put a widget off it, and the minimum form is the more
# common idiom. Same for `addSpacing` beside `setSpacing`, and for the
# grid-and-form-layout spellings of spacing.
#
# **It costs this tree nothing, which is why it is safe to widen.** Measured
# 2026-11-03 over the eight files the gate reads: not one of the added calls
# appears in them at all, so the list grows and the verdict does not move.
# What it protects is the case the docstring below says this gate is FOR --
# an application pointing it at its own UI, where `setMinimumWidth(200)` and
# `addSpacing(8)` are everywhere and would have sailed through.
#
# DELIBERATELY NOT HERE: `resize`, `setGeometry` and `move`. Sizing and
# placing a top-level window in pixels is how Qt works and is what
# `GridMetrics::cells()` exists to feed; the rule is about the layout INSIDE
# shared view code, and the example resizes its window on purpose. Nor
# `setMaximum`/`setMinimum`, which are a spin box's and a slider's VALUE
# range and have nothing to do with pixels -- the substring search below
# would not reach them from these names, and that is worth knowing before
# anybody shortens one.
#
# THE LIMIT IT SHARES WITH `setFixedSize`: a QSize argument is one argument
# and not a number, so `setMinimumSize(QSize(200, 80))` is not flagged while
# `setMinimumSize(200, 80)` is. That hole predates this list and is not made
# worse by it; closing it means looking inside a constructor's arguments,
# which is a different gate.
BANNED = ("setContentsMargins", "setSpacing", "setFixedSize",
          "setFixedWidth", "setFixedHeight",
          "setMinimumWidth", "setMinimumHeight", "setMinimumSize",
          "setMaximumWidth", "setMaximumHeight", "setMaximumSize",
          "addSpacing", "setHorizontalSpacing", "setVerticalSpacing",
          "setColumnMinimumWidth", "setRowMinimumHeight")

# An argument that is ENTIRELY a number, which is the distinction that keeps
# this gate usable. Searching the argument list for digits flags `20 * cw`
# on its 20 -- cell arithmetic, portable by construction, and precisely what
# the rule wants people to write. Measured on a fixture: that false positive
# was there in the first version, and the docstring claimed the opposite.
LITERAL = re.compile(r"^[+-]?\d+(?:\.\d+)?[fF]?$")

# TWO CALLS TAKE AN INDEX FIRST, and a row number is legitimately a bare
# number. `setRowMinimumHeight(1, 24)` reported "hardcodes 1, 24" when these
# were added -- the 24 is the finding and the 1 is a row, so half the message
# was noise. The docstring above says what noise costs: a gate that flags
# what is not the code's fault is one somebody disables. So the leading
# index arguments are skipped by name, which is narrower than guessing that
# a first argument is never a size -- `setMinimumSize(200, 80)` proves it
# usually is.
INDEX_ARGS = {"setColumnMinimumWidth": 1, "setRowMinimumHeight": 1}

ALLOW = "qtty-allow:"


def arguments(text, start):
	"""The text between the parentheses that open at `start`, or None.

	`start` is the index just past the call's name, and only whitespace may
	separate it from the "(" -- including a newline, which is the whole point:
	the version of this that searched one line at a time skipped a wrapped call
	silently, so the ban was escapable by reformatting. Over a whole file a
	looser search would be worse than that, pairing a mention in a comment with
	some later call's parentheses."""
	open_at = start
	while open_at < len(text) and text[open_at].isspace():
		open_at += 1
	if open_at >= len(text) or text[open_at] != "(":
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
	"""(violations, call sites judged), or None when the file cannot be read.

	The whole file at once rather than a line at a time. A call wrapped across
	lines used to be skipped in silence -- not reported, not counted, and not
	required to carry the reason the rule is built around -- so the ban was
	escapable by pressing Enter."""
	try:
		with open(path, encoding="utf-8") as f:
			text = f.read()
	except (OSError, UnicodeDecodeError) as e:
		print("%s: %s" % (path, e), file=sys.stderr)
		return None
	lines = text.split("\n")

	bad, judged = 0, 0
	for call in BANNED:
		at = text.find(call)
		while at >= 0:                      # `at` strictly increases: it ends
			args = arguments(text, at + len(call))
			if args is not None:
				judged += 1
				n = text.count("\n", 0, at)
				hard = [a for a in split_args(args)[INDEX_ARGS.get(call, 0):]
				        if LITERAL.match(a) and float(a.rstrip("fF")) != 0]
				if hard and not allowed(lines, n):
					print("%s:%d: %s hardcodes %s -- use cells, or say why "
					      "with // %s <reason>"
					      % (path, n + 1, call, ", ".join(hard), ALLOW))
					bad += 1
			at = text.find(call, at + len(call))
	return bad, judged


def self_test():
	"""Every banned call flagged once, and the control half silent.

	THE GATE HAD NO TEST AT ALL. No fixture in the tree, no sabotage entry,
	and the docstring's "measured on a fixture" referred to one that was
	never committed -- so every rule here rested on somebody having run it
	by hand once. `evidence.md`'s rule is that a check is untested until it
	has been seen to fail, and `guide_check.py` in this same directory
	already carries the answer: a control that must fail, run on every
	invocation, refusing when it does not.

	**Generated from BANNED rather than written out**, which is the property
	that matters. A fixture listing the calls by hand goes stale the moment
	somebody adds one -- it would pass while covering fifteen of sixteen,
	which is this gate's own failure mode wearing a test's clothes. Built
	this way, a call added to BANNED with no thought for the control still
	gets one.

	The gate does not care about arity -- `arguments()` reads whatever is
	between the parentheses -- so one literal argument exercises a call that
	really takes four. The two INDEX_ARGS calls get a leading zero so that
	the flagged value is in a position the gate reads.

	It costs string work and no compile, so it runs every time rather than
	behind a flag nobody passes.

	**THE LIMIT, and it is the honest half.** Generating the fixture from
	BANNED proves the gate IMPLEMENTS its list; it cannot prove the list is
	right. Remove a call from BANNED and both the fixture line and the
	expected count shrink together, so the control stays green over a
	narrower gate -- which is precisely the fault this whole entry is about,
	one level up. The policy lives in design.md section 7 and in
	project.md 8.666; this only holds the mechanism to it.
	"""
	def body(value):
		out = []
		for call in BANNED:
			lead = "0, " * INDEX_ARGS.get(call, 0)
			out.append("\to->%s(%s%s);" % (call, lead, value))
		return "void f() {\n" + "\n".join(out) + "\n}\n"

	cases = [
		# source,                            expected findings
		(body("7"),                          len(BANNED)),
		(body("0"),                          0),
		(body("3 * cw"),                     0),
		("void f() {\n\to->setSpacing(8);"
		 "   // %s measured, stays px\n}\n" % ALLOW,  0),
		("void f() {\n\to->setMaximum(9999);\n"
		 "\to->setMinimum(10);\n}\n",        0),
	]
	# check() prints each finding as it goes, which is right for the real run
	# and is noise here -- the control's sixteen deliberate violations would
	# be printed by every passing gate. Swallowed rather than made optional,
	# so the code path under test is EXACTLY the one the real run takes.
	with tempfile.TemporaryDirectory() as tmp:
		path = os.path.join(tmp, "control.cpp")
		for i, (src, want) in enumerate(cases):
			with open(path, "w", encoding="utf-8") as f:
				f.write(src)
			with io.StringIO() as sink, contextlib.redirect_stdout(sink):
				got = check(path)
			if got is None or got[0] != want:
				print("layout-gate: THE CONTROL FAILED on case %d -- expected"
				      " %d finding(s), got %s. The gate's own rules are not"
				      " doing what they say, so no verdict below would mean"
				      " anything. Refusing." % (i, want,
				      "unreadable" if got is None else got[0]),
				      file=sys.stderr)
				return False
	return True


def main(argv):
	if len(argv) < 2:
		print(__doc__.strip().split("\n")[-1], file=sys.stderr)
		return 2

	if not self_test():
		return 2

	# The population, reported whether or not anything was wrong. A gate that
	# prints nothing on success cannot be told apart from one that read nothing
	# -- the file list here comes from a Makefile wildcard, and a wildcard that
	# stops matching is exactly how a green run comes to mean nothing at all.
	files, judged, bad, unread = 0, 0, 0, 0
	for path in argv[1:]:
		result = check(path)
		if result is None:
			unread += 1
			continue
		files += 1
		bad += result[0]
		judged += result[1]
	if files == 0:
		print("layout-gate: none of the %d path(s) could be read -- this is a "
		      "failure, not a clean tree" % (len(argv) - 1), file=sys.stderr)
		return 2
	if bad:
		print("%d hardcoded layout value(s)" % bad, file=sys.stderr)
		return 1
	if unread:
		return 2
	print("layout-gate: %d file(s), %d call site(s) judged: no hardcoded "
	      "layout values" % (files, judged), file=sys.stderr)
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv))
