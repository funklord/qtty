#!/usr/bin/env python3
"""Apply each sabotage in tool/sabotage.toml and confirm it reddens the
check it names.

Why this exists rather than a convention that somebody sabotages by hand:
a check is untested until it has been seen to fail, and the moment where
being careful is a choice is the moment it gets skipped. Every entry in
the spec was performed by hand once; this is what stops them having to be
remembered.

Three ways a run of this can lie, and each is refused rather than
reported:

  - The substitution does not apply. A sabotage that did not land and a
    check that cannot fail are indistinguishable from the output -- both
    give a green suite -- so the anchor's occurrence count is asserted
    before anything is written, and again after.
  - The named check was already failing. Then its failure says nothing
    about the sabotage, so the baseline run must have it passing.
  - The tree is dirty. Restoring a file this wrote would be fine; more
    than one session works these trees, and restoring a file SOMEBODY
    ELSE is editing is not. It refuses instead.

Restoration is the part that has to hold under every exit path: the
original bytes are kept in memory, written back in a finally, registered
with atexit, and re-registered against SIGINT and SIGTERM. The tree is
verified byte-identical afterwards and the run fails if it is not.
"""

import atexit
import hashlib
import os
import signal
import subprocess
import sys
import tomllib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPEC = os.path.join(ROOT, "tool", "sabotage.toml")

# Bounds. The spec is a finite list and each entry costs one build and one
# suite run, so the run terminates when the list is exhausted; these are
# the ceilings for a single step going wrong rather than for the whole.
BUILD_TIMEOUT = 600
TEST_TIMEOUT = 600


def say(s):
	sys.stdout.write(s + "\n")
	sys.stdout.flush()


class Restorer:
	"""Holds original bytes and puts them back, once, under any exit."""

	def __init__(self):
		self.saved = {}
		self.done = False

	def keep(self, path, data):
		self.saved.setdefault(path, data)

	def restore(self):
		if self.done:
			return
		self.done = True
		for path, data in self.saved.items():
			try:
				with open(path, "wb") as f:
					f.write(data)
			except OSError as exc:
				say("sabotage: COULD NOT RESTORE %s: %s" % (path, exc))

	def verify(self):
		bad = []
		for path, data in self.saved.items():
			with open(path, "rb") as f:
				if f.read() != data:
					bad.append(path)
		return bad


def run(cmd, timeout, cwd=ROOT):
	env = dict(os.environ)
	env["QTEST_DISABLE_STACK_DUMP"] = "1"
	try:
		# stderr is captured SEPARATELY, not merged. Qt writes warnings
		# there while the suite writes results to stdout, and merging the
		# two lets a warning land inside a result line:
		#
		#     PASS: a This plugin does not support propagateSizeHints()
		#
		# That is a real line from a real run. A corrupted PASS reads as a
		# check that did not pass, and a corrupted FAIL reads as a check
		# that did not fail -- which this harness reports as "the code was
		# broken and nothing noticed", the one alarm it exists to raise.
		# Parsing a stream nobody else writes to removes the whole class.
		return subprocess.run(cmd, cwd=cwd, env=env, timeout=timeout,
		                      stdout=subprocess.PIPE,
		                      stderr=subprocess.PIPE,
		                      text=True, errors="replace")
	except subprocess.TimeoutExpired:
		return None


def build_and_test():
	"""Build, then run the suite. Returns (ok, output) or (None, why).

	The build's status is read before the suite is judged: a suite run
	against a binary that did not rebuild reports on the previous code,
	which is this file's whole subject wearing a different hat.
	"""
	b = run(["make"], BUILD_TIMEOUT)
	if b is None:
		return None, "the build timed out"
	if b.returncode != 0:
		return None, "the build failed:\n" + (b.stdout + b.stderr)[-2000:]
	t = run(["make", "test"], TEST_TIMEOUT)
	if t is None:
		return None, "the suite timed out"
	return t.returncode == 0, t.stdout


def suite_finished(output):
	"""Did the suite print its own last line, rather than being stopped?

	The summary is the only thing that says a run ended by choice. A
	truncated run and a completed one are otherwise identical in shape --
	both are a list of PASS lines -- so nothing else in the output can tell
	a killed suite from one that simply never ran a check.
	"""
	return any(ln.startswith(("OK (", "FAILED ("))
	           for ln in output.splitlines())


def failing_checks(output):
	return [ln[len("FAIL: "):].strip()
	        for ln in output.splitlines() if ln.startswith("FAIL: ")]


def passing_checks(output):
	return [ln[len("PASS: "):].strip()
	        for ln in output.splitlines() if ln.startswith("PASS: ")]


def main():
	if not os.path.isfile(SPEC):
		say("sabotage: no spec at %s" % SPEC)
		return 2

	# Read-only, so it runs BEFORE the dirty-tree refusal below rather than
	# after it. Validation writes nothing and restores nothing, and a gate
	# that declines while the tree has uncommitted source is a gate that is
	# off during exactly the work that breaks anchors.
	if "--validate" in sys.argv:
		with open(SPEC, "rb") as f:
			spec = tomllib.load(f).get("sabotage", [])
	# It
	# exists because an entry whose `find` has drifted out of the source is
	# a sabotage that cannot be applied, and therefore a behaviour this
	# harness no longer defends -- and it says so only when that entry is
	# RUN, which at one build and one suite run apiece is rarely.
	#
	# Two entries were found dead this way, both broken by edits to the very
	# lines they anchor on: the icon substitution and the stroke ink. Both
	# had been silent for a session's worth of commits, because every run in
	# between was an --only for something else. String matching costs
	# milliseconds where running costs minutes, so this can be a gate and
	# the sweep cannot.
		bad = 0
		for e in spec:
			path = os.path.join(ROOT, e["file"])
			if not os.path.isfile(path):
				say("sabotage: %s names a missing file %s"
				    % (e["name"], e["file"]))
				bad += 1
				continue
			with open(path, encoding="utf-8") as f:
				got = f.read().count(e["find"])
			if got != e.get("count", 1):
				say("sabotage: %s" % e["name"])
				say("          its anchor matches %d time(s) in %s, and the"
				    % (got, e["file"]))
				say("          spec says %d. The entry cannot be applied, so"
				    % e.get("count", 1))
				say("          the check it names is undefended.")
				bad += 1
		if bad:
			say("sabotage: %d of %d entries cannot be applied" % (bad, len(spec)))
			return 1
		if not spec:
			say("sabotage: the spec is empty, so validating it proves nothing")
			return 1
		say("sabotage: %d entries, every anchor matches its source" % len(spec))
		return 0

	dirty = subprocess.run(["git", "status", "--porcelain", "--", "src", "test",
	                        "include"], cwd=ROOT, stdout=subprocess.PIPE,
	                       text=True).stdout.strip()
	if dirty and "--dirty-ok" not in sys.argv:
		say("sabotage: refusing to run -- src/, test/ or include/ is dirty.")
		say("          This edits source files and puts them back, and in a")
		say("          tree more than one session works in, the file it")
		say("          would restore may be somebody else's work in")
		say("          progress. Commit or stash first, or pass --dirty-ok")
		say("          if every line below is yours.")
		for ln in dirty.splitlines():
			say("          " + ln)
		return 2

	with open(SPEC, "rb") as f:
		spec = tomllib.load(f).get("sabotage", [])


	# --only <substring> runs the entries whose name matches. It exists so
	# that the harness's OWN positive control is affordable: proving this
	# can report a failure means breaking a check on purpose and watching
	# it say so, and at one build and one suite run per entry a full sweep
	# is too dear to do for that. It also refuses an --only that selects
	# nothing, which would otherwise report success over an empty list --
	# the vacuous pass, in the tool written to find vacuous passes.
	only = None
	for i, a in enumerate(sys.argv):
		if a == "--only" and i + 1 < len(sys.argv):
			only = sys.argv[i + 1]
	if only is not None:
		spec = [e for e in spec if only in e["name"]]
		if not spec:
			say("sabotage: --only %r matched no entry" % only)
			return 2
		say("sabotage: --only %r selected %d of the spec" % (only, len(spec)))

	if not spec:
		say("sabotage: the spec is empty, so this run proves nothing")
		return 2

	restorer = Restorer()
	atexit.register(restorer.restore)
	for sig in (signal.SIGINT, signal.SIGTERM):
		signal.signal(sig, lambda *a: sys.exit(130))

	rc = 0
	try:
		say("sabotage: baseline -- building and running the suite unbroken")
		ok, out = build_and_test()
		if ok is not True:
			say("sabotage: the baseline is not green, so nothing below would")
			say("          mean anything. %s" % (out if ok is None else
			                                     "the suite failed"))
			if ok is not None:
				for c in failing_checks(out):
					say("          FAIL: " + c)
			return 2
		green = set(passing_checks(out))
		say("sabotage: baseline green, %d checks passing" % len(green))

		for i, item in enumerate(spec, 1):
			name = item["name"]
			path = os.path.join(ROOT, item["file"])
			find, into = item["find"], item["into"]
			want = item.get("count", 1)
			check = item["check"]

			say("")
			say("sabotage %d/%d: %s" % (i, len(spec), name))

			# The named check has to be passing before, or its failure
			# afterwards is not attributable to anything.
			hit = [c for c in green if check in c]
			if len(hit) != 1:
				say("  REFUSED: the named check matches %d passing checks,"
				    " not 1" % len(hit))
				say("           check: %s" % check)
				rc = 1
				continue

			with open(path, "rb") as f:
				original = f.read()
			restorer.keep(path, original)
			text = original.decode("utf-8")

			got = text.count(find)
			if got != want:
				say("  REFUSED: the anchor appears %d time(s), the spec says"
				    " %d." % (got, want))
				say("           An anchor that has stopped being unique"
				    " applies to the")
				say("           wrong place; one that matches nothing applies"
				    " to none.")
				rc = 1
				continue

			with open(path, "w", encoding="utf-8") as f:
				f.write(text.replace(find, into, want))

			# Confirm it landed. Reading it back is the whole point: the
			# write could have gone to a path that is not what is built.
			with open(path, "r", encoding="utf-8") as f:
				after = f.read()
			if after.count(into) < want or after == text:
				say("  REFUSED: the substitution did not land in the file")
				with open(path, "wb") as f:
					f.write(original)
				rc = 1
				continue

			ok, out = build_and_test()
			with open(path, "wb") as f:
				f.write(original)

			if ok is None:
				# A sabotage that will not compile is a legitimate outcome
				# only if the spec said so; it is not evidence about a
				# check, because no check ran.
				say("  INCONCLUSIVE: %s" % out.splitlines()[0])
				say("                No check ran, so this says nothing about"
				    " the one named.")
				rc = 1
				continue

			red = [c for c in failing_checks(out) if check in c]
			if red:
				say("  ok -- reddened: %s" % red[0])
				others = [c for c in failing_checks(out) if check not in c]
				if others:
					say("     and %d other check(s) with it" % len(others))
			elif check not in " ".join(passing_checks(out)):
				# Neither red nor green: the run never got there. A hang or a
				# crash produces no line of either kind, and reporting that
				# as "nothing noticed" is a different and much more alarming
				# claim than the truth. Measured on the day this was added:
				# a sabotage that stopped a drag from ever ending hung the
				# suite at 212 checks, and this branch said the code was
				# broken and nothing noticed.
				#
				# Two conditions, split because they send a reader to
				# different places. A suite that printed no summary line was
				# CUT OFF -- killed, hung or crashed -- and what to look at
				# is the run. One that finished and still never mentioned the
				# check did not RUN it, and what to look at is the suite: a
				# section returning early, or a check behind a condition that
				# was false. The one message covering both used to say "a
				# hang or a crash" for the second case too.
				#
				# What produced the split: a `make count-check` run in the
				# same tree WHILE this harness was running, which rebuilt and
				# relinked the test binary underneath the suite. The verdict
				# said the check was never reached, which was true, and
				# nothing said the run had been truncated rather than the
				# check skipped -- so the next two builds were spent on a
				# guess about timeouts. The arithmetic is what settled it:
				# count-check reported 1205 passes where the tree has 1209
				# checks and the sabotage reddens 4, so count-check had run
				# the SABOTAGED suite. Anything that builds in this tree
				# while this runs will do the same.
				cut_off = not suite_finished(out)
				say("  INCONCLUSIVE: the suite %s the named check."
				    % ("was cut off before" if cut_off else "finished without"))
				say("          check: %s" % check)
				say("          It reported %d pass(es) and %d failure(s)."
				    % (len(passing_checks(out)), len(failing_checks(out))))
				if cut_off:
					say("          No summary line, so it was killed, hung"
					    " or crashed --")
					say("          not a silent pass. Check the run before"
					    " the check.")
				else:
					say("          It ran to the end, so the check did not"
					    " run at all:")
					say("          a section returning early, or a condition"
					    " that was false.")
					say("          Fix the check so it FAILS rather than"
					    " stops, then re-run.")
				rc = 1
			else:
				say("  FAILED: the named check PASSED against broken code.")
				say("          check: %s" % check)
				say("          The code was broken and nothing noticed, which"
				    " is the")
				say("          one thing this target exists to find.")
				for c in failing_checks(out)[:5]:
					say("          (it did report: %s)" % c)
				rc = 1
	finally:
		restorer.restore()

	bad = restorer.verify()
	if bad:
		say("")
		say("sabotage: THE TREE WAS NOT RESTORED. Check these by hand:")
		for p in bad:
			say("          " + p)
		return 2

	# Rebuild, so that whatever is on disk matches the source again. A
	# session that ran this and then judged a test result from the last
	# sabotage's binary is the staleness trap this file is about.
	say("")
	say("sabotage: restored %d file(s); rebuilding to the real source"
	    % len(restorer.saved))
	b = run(["make"], BUILD_TIMEOUT)
	if b is None or b.returncode != 0:
		say("sabotage: the rebuild after restoring FAILED -- do not trust any"
		    " binary in the tree")
		return 2

	if rc == 0:
		say("sabotage: %d sabotage(s), each reddened the check it names"
		    % len(spec))
	else:
		say("sabotage: FAILED -- see above")
	return rc


if __name__ == "__main__":
	sys.exit(main())
