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
		return subprocess.run(cmd, cwd=cwd, env=env, timeout=timeout,
		                      stdout=subprocess.PIPE,
		                      stderr=subprocess.STDOUT,
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
		return None, "the build failed:\n" + b.stdout[-2000:]
	t = run(["make", "test"], TEST_TIMEOUT)
	if t is None:
		return None, "the suite timed out"
	return t.returncode == 0, t.stdout


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
			else:
				say("  FAILED: the suite did not report the named check.")
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
