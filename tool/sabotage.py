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
# Held for the length of a run, so that anything else asking this tree a
# question can find out that the answer would be about broken source.
LOCK = os.path.join(ROOT, "build", "sabotage.lock")

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
				# And make the file newer than anything built from the
				# broken version of it. The write above already does that
				# in the ordinary case; this is for the interrupted one,
				# where a compile that was in flight may have finished in
				# between. An object newer than its source is a rebuild
				# that will not happen.
				os.utime(path, None)
			except OSError as exc:
				say("sabotage: COULD NOT RESTORE %s: %s" % (path, exc))

	def verify(self):
		bad = []
		for path, data in self.saved.items():
			with open(path, "rb") as f:
				if f.read() != data:
					bad.append(path)
		return bad


# The build or suite this is waiting on, so that a signal can stop it before
# the sources go back. None between steps.
g_child = None


def kill_group(child, sig):
	"""Signal a child and everything it started, falling back to the one.

	The group id is the child's own pid, every child here being started in
	a new session. A group that has already gone leaves nothing to signal,
	which is not an error.
	"""
	try:
		os.killpg(os.getpgid(child.pid), sig)
	except (ProcessLookupError, PermissionError):
		try:
			child.send_signal(sig)
		except ProcessLookupError:
			pass


def stop_child():
	"""Kill whatever is running, and wait for it to actually be gone.

	The wait is the point. Restoring the sources while a compiler is still
	reading them is the race this exists to close, and a kill that is not
	waited for is a kill that has not happened yet.

	THE GROUP, not the process. Killing `make` leaves the compiler it
	started running: cc1 is make's child, not this one's, so it is
	reparented rather than stopped, finishes a second or two later, and
	writes an object compiled from the sabotaged source AFTER the restore
	has put the real source back -- the object newer than the file it came
	from, so the next `make` rebuilds nothing and the next suite run
	reports failures in code nobody has touched. Measured 2026-09-16, and
	the earlier mitigation did not reach it: the restore touches each file
	it puts back, and a compile still in flight simply lands after the
	touch. build/src/input_router.o came out 2.3 seconds newer than its
	source, and two checks failed against a clean tree.

	So every child is started in its own session (`start_new_session`) and
	the signal goes to the whole group.
	"""
	global g_child
	child, g_child = g_child, None
	if child is None or child.poll() is not None:
		return
	kill_group(child, signal.SIGTERM)
	try:
		child.wait(timeout=20)
	except subprocess.TimeoutExpired:
		kill_group(child, signal.SIGKILL)
		try:
			child.wait(timeout=10)
		except subprocess.TimeoutExpired:
			say("sabotage: a build would not die; the tree is restored but")
			say("          the build directory may hold objects from broken")
			say("          source. Run `make` again before believing it.")


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
		#
		# Popen rather than run(), so that a signal can reach the CHILD.
		# Stopping this harness with SIGTERM used to leave whatever build
		# was in flight running: it finished a few seconds later, wrote an
		# object compiled from the sabotaged source, and did it AFTER the
		# restore had put the real source back. The tree then looked clean,
		# `make` saw an object newer than its source and rebuilt nothing,
		# and the next suite run reported a failure in code nobody had
		# touched. Measured 2026-09-15: a stopped run left
		# build/src/cell_paint.o two seconds newer than the file it was
		# built from, and "a transparent pen draws no rule" failed against
		# a clean tree.
		#
		# The handle is held LOCALLY as well, because the signal handler
		# clears the global one. Reading the global in the `finally` was
		# how stopping a run ended in a traceback rather than in the exit
		# this file designed: SIGTERM inside communicate() runs the
		# handler, stop_child() sets g_child to None, sys.exit(130) then
		# unwinds through a `finally` that asked None for its return code
		# -- and the AttributeError REPLACED the SystemExit, so a clean
		# stop printed like a crash. The sources were restored throughout,
		# atexit not caring how the process ends, but nothing in the
		# output said so, and a stop that looks like a crash is one nobody
		# will trust the tree after.
		global g_child
		child = subprocess.Popen(cmd, cwd=cwd, env=env,
		                         stdout=subprocess.PIPE,
		                         stderr=subprocess.PIPE,
		                         text=True, errors="replace",
		                         start_new_session=True)
		g_child = child
		try:
			out, err = child.communicate(timeout=timeout)
		finally:
			rc = child.returncode
			g_child = None
		return subprocess.CompletedProcess(cmd, rc, out, err)
	except subprocess.TimeoutExpired:
		stop_child()
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


# What the docstring says, reachable from the program -- and the arguments,
# which were documented only beside the code that parses them. This tool
# already had the defect its own tree records for the four binaries: every
# flag explained in a comment nobody running it could see, and an unknown
# one answered by doing the ordinary work. Here the ordinary work edits the
# sources and takes an hour, so `sabotage.py --help` was the worst version
# of that trap available.
USAGE = """\
sabotage.py -- break the code on purpose and confirm the suite says so.

usage: sabotage.py [--validate] [--only TEXT] [--from N] [--dirty-ok]

Runs every entry in tool/sabotage.toml: applies it, builds, runs the
suite, and requires the check the entry names to go red. Restores the
tree under every exit path, including a signal.

  --validate     check that every anchor still matches its source, and
                 nothing else. Milliseconds rather than an hour, so this
                 is what `make check` runs.
  --only TEXT    run the entries whose name contains TEXT. Refuses when
                 it matches none, rather than reporting a green run over
                 an empty list.
  --from N       start at the Nth entry of the spec, numbered as the
                 per-entry line prints it. For resuming a run that was
                 interrupted: `sabotage 212/233` means `--from 212`.
  --dirty-ok     run although src/, test/ or include/ has uncommitted
                 changes. The refusal exists because more than one
                 session works these trees.
"""


def known_arguments(argv):
	"""Refuse a flag this does not understand, rather than ignoring it.

	Ignoring one means `--halp` runs the whole spec, and so did `--help`
	until this was written. A tool that edits the tree owes its caller a
	refusal it can read instead of an hour of work it did not ask for.
	"""
	takes_value = {"--only", "--from"}
	flags = takes_value | {"--validate", "--dirty-ok", "--help", "-h"}
	i = 0
	while i < len(argv):
		a = argv[i]
		if a not in flags:
			return a
		i += 2 if a in takes_value else 1
	return None


def lock_holder():
	"""The pid of a sabotage run in flight, or None.

	A run edits the sources under everything else in this tree, so a
	reading taken while one is going describes the sabotage rather than
	the code. `--validate` met this exactly: run during a sabotage of
	compositor.cpp it reported the entry anchored there as unappliable and
	"the check it names is undefended" -- alarming, and false.

	A stale lock is not a lock. The pid is checked rather than trusted,
	because a run that was killed hard leaves the file behind and a lock
	nobody can clear is worse than none.
	"""
	try:
		with open(LOCK) as f:
			pid = int(f.read().split()[0])
	except (OSError, ValueError, IndexError):
		return None
	try:
		os.kill(pid, 0)
	except ProcessLookupError:
		return None
	except PermissionError:
		return pid                      # alive and somebody else's
	return pid


def take_lock():
	"""Claim the tree, and give it back however this process ends."""
	os.makedirs(os.path.dirname(LOCK), exist_ok=True)
	with open(LOCK, "w") as f:
		f.write("%d\n" % os.getpid())
	atexit.register(drop_lock)


def drop_lock():
	if lock_holder() == os.getpid():
		try:
			os.remove(LOCK)
		except OSError:
			pass


def main():
	if "--help" in sys.argv or "-h" in sys.argv:
		sys.stdout.write(USAGE)
		return 0
	unknown = known_arguments(sys.argv[1:])
	if unknown is not None:
		say("sabotage: unknown argument %r. Nothing was run." % unknown)
		say("          sabotage.py --help says what it takes.")
		return 2

	if not os.path.isfile(SPEC):
		say("sabotage: no spec at %s" % SPEC)
		return 2

	# Read-only, so it runs BEFORE the dirty-tree refusal below rather than
	# after it. Validation writes nothing and restores nothing, and a gate
	# that declines while the tree has uncommitted source is a gate that is
	# off during exactly the work that breaks anchors.
	if "--validate" in sys.argv:
		held = lock_holder()
		if held:
			say("sabotage: a run is in flight (pid %d), so the sources are"
			    " not the" % held)
			say("          tree's own right now and every anchor read here"
			    " would be")
			say("          a fact about a sabotage. Nothing was checked.")
			return 2
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
	# Resume, because a long run is interrupted more often than it is
	# finished in one sitting: 90 minutes for 233 entries is an outer
	# `timeout` away from stopping at 212, which is what happened the day
	# this was added. Without it the remaining 21 cost either a whole run
	# again or 21 invocations, each paying for its own baseline.
	#
	# The index is the spec's own order, 1-based, as the per-entry line
	# prints it -- so `--from 213` starts where "sabotage 212/233" left off.
	start = 1
	for i, a in enumerate(sys.argv):
		if a == "--from" and i + 1 < len(sys.argv):
			try:
				start = max(1, int(sys.argv[i + 1]))
			except ValueError:
				say("sabotage: --from wants a number")
				return 2
	if start > 1:
		if start > len(spec):
			say("sabotage: --from %d is past the end of a %d-entry spec"
			    % (start, len(spec)))
			return 2
		say("sabotage: --from %d, so %d of %d entries are being skipped"
		    % (start, start - 1, len(spec)))
		spec = spec[start - 1:]

	# Number the entries as the WHOLE spec numbers them, so that the line a
	# reader copies into the next --from is the one they just saw. Only
	# where the set has not also been filtered: an --only selects a sparse
	# handful, and numbering those against the full spec would invent an
	# order they do not have.
	first, total = (start, start - 1 + len(spec)) if only is None else (1, 0)

	if only is not None:
		spec = [e for e in spec if only in e["name"]]
		if not spec:
			say("sabotage: --only %r matched no entry" % only)
			return 2
		say("sabotage: --only %r selected %d of the spec" % (only, len(spec)))
		total = len(spec)

	if not spec:
		say("sabotage: the spec is empty, so this run proves nothing")
		return 2

	# One run at a time, and everything else told which tree it is looking
	# at. Two runs in one tree would restore each other's sources from
	# their own copies, and the second's baseline would be built from the
	# first's sabotage -- which reads as a suite that has started failing.
	held = lock_holder()
	if held:
		say("sabotage: another run holds this tree (pid %d). Two at once"
		    " would" % held)
		say("          restore each other's sources, so this one is"
		    " declining.")
		return 2
	take_lock()

	restorer = Restorer()
	atexit.register(restorer.restore)
	def on_signal(*_):
		# The child first, then the sources: see stop_child(). The restore
		# must not race a compiler that is still reading what it is putting
		# back.
		#
		# And it SAYS so, rather than leaving atexit to do it quietly. A
		# stopped run edits this tree and the operator's next question is
		# whether it put the file back; silence answers that exactly as
		# loudly as a failure would. atexit still holds the guarantee --
		# restore() runs once however the process ends -- so this line is
		# the report and not the mechanism.
		stop_child()
		restorer.restore()
		if restorer.saved:
			say("sabotage: stopped -- %d file(s) put back, and touched so a"
			    % len(restorer.saved))
			say("          build in flight cannot leave an object newer"
			    " than its source")
		else:
			say("sabotage: stopped before any source was edited")
		sys.exit(130)

	for sig in (signal.SIGINT, signal.SIGTERM):
		signal.signal(sig, on_signal)

	rc = 0
	# Declared out here, not beside its first use: the summary below runs
	# after the `finally`, and a failure on the way to the baseline would
	# otherwise reach it with the name unbound.
	reddened = set()
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

		# Every check any entry reddens, kept so the run can say what the
		# SET defends rather than only that each entry did its own job. An
		# entry names one check and usually takes others down with it, and
		# the count of those is printed per entry and then lost -- so the
		# question a reader actually has, how much of the suite this spec
		# is able to move, had no answer anywhere.

		for i, item in enumerate(spec, first):
			name = item["name"]
			path = os.path.join(ROOT, item["file"])
			find, into = item["find"], item["into"]
			want = item.get("count", 1)
			check = item["check"]

			say("")
			say("sabotage %d/%d: %s" % (i, total, name))

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

			# A sabotage whose honest outcome is a DEAD SUITE rather than a
			# red line, declared per entry. A lifetime guard is the case:
			# remove it and the next event lands on freed memory, so the
			# suite does not report a failure -- it stops. Measured on the
			# first of these: dropping the hover guard took the run out at
			# 250 checks with no summary line, which the default branch
			# below calls INCONCLUSIVE and is right to, because for every
			# other entry that is what a hang looks like.
			#
			# Kept narrow deliberately. It accepts only a run that produced
			# output and no summary; a run that timed out never reaches here
			# (ok is None above), so "expect = crash" cannot quietly pass an
			# entry that hung. And a suite that RUNS TO THE END under this
			# expectation is a failure: the guard was removed and nothing
			# happened, which means the check does not defend what it says.
			if item.get("expect") == "crash":
				if not suite_finished(out):
					say("  ok -- the suite could not finish, which is what"
					    " this entry expects")
					say("     (%d check(s) ran before it stopped)"
					    % len(passing_checks(out)))
				else:
					say("  FAILED: the guard was removed and the suite ran"
					    " to the end.")
					say("          check: %s" % check)
					say("          An entry declaring `expect = \"crash\"`"
					    " says the code cannot")
					say("          survive without it. It survived, so one"
					    " of the two is wrong.")
					rc = 1
				continue

			red = [c for c in failing_checks(out) if check in c]
			if red:
				say("  ok -- reddened: %s" % red[0])
				reddened.update(failing_checks(out))
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
	#
	# `make tests-build` rather than `make`, and the difference is the whole
	# of the trap rather than a detail. This tree's default target does not
	# build tests -- build-and-commit.md requires that, so a plain build
	# stays fast -- so `make` relinked the library and left
	# build-test/qtty-tests linked against the LAST SABOTAGE's library.
	# Measured 2026-09-16, straight after a run that printed "restored 1
	# file(s); rebuilding to the real source": the binary reported 1395
	# passes and two failures, both of them the checks that entry had just
	# broken, in a tree whose source was whole. The mitigation had been
	# written for a build system where one target covers everything, and
	# this is not one.
	say("")
	say("sabotage: restored %d file(s); rebuilding the library and the suite"
	    % len(restorer.saved))
	b = run(["make", "tests-build"], BUILD_TIMEOUT)
	if b is None or b.returncode != 0:
		say("sabotage: the rebuild after restoring FAILED -- do not trust any"
		    " binary in the tree")
		return 2

	# The set's reach, which is a different fact from every entry passing:
	# 233 entries that all redden the same twenty checks would report the
	# same line as 233 that redden four hundred. Entries declaring
	# `expect = "crash"` contribute nothing here, a stopped suite printing
	# no FAIL line to count.
	if reddened:
		say("sabotage: between them the entries redden %d distinct check(s)"
		    " of the %d the suite runs" % (len(reddened), len(green)))
	if rc == 0:
		say("sabotage: %d sabotage(s), each reddened the check it names"
		    % len(spec))
	else:
		say("sabotage: FAILED -- see above")
	return rc


if __name__ == "__main__":
	sys.exit(main())
