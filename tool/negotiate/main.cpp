// qtty-negotiate -- what qtty makes of the terminal it is run inside.
//
// Run it in a terminal and it prints the negotiated capabilities. That is
// worth a tool rather than a debugging printf because the negotiation is the
// one part of qtty whose answer depends on somebody else's program: a suite
// can only check it against replies the suite itself wrote, which is one
// witness. Pointed at a real terminal emulator it is two.
//
// Writes to a FILE rather than to stdout when QTTY_NEGOTIATE_OUT is set,
// because stdout is the terminal under test -- and under a terminal launched
// with `-e` there may be nowhere for the output to be read from afterwards.
#include <qtty/qtty.h>
#include <qtty/version.h>
#include "src/backend/ansi/ansi_backend.h"
#include "src/backend/ansi/term_caps.h"

#include <QApplication>
#include <QtGlobal>
#include <cstdio>
#include <cstdlib>
#include <termios.h>
#include <unistd.h>

using namespace Qtty;

int main(int argc, char **argv) {
	// Before anything else, and before QApplication: --version must work in a
	// pipe, on a machine with no terminal to negotiate with, and without the
	// startup query this tool otherwise sends.
	for (int i = 1; i < argc; ++i)
		if (!qstrcmp(argv[i], "--version") || !qstrcmp(argv[i], "-V")) {
			printf("qtty-negotiate %s\n%s\n", version_string, copyright);
			return 0;
		}
	// --probes reports what the terminal ANSWERED, one probe at a time, and
	// distinguishes silence from a definite no. The ordinary output cannot:
	// it reports what qtty negotiated, which is downstream of both and treats
	// them alike on purpose -- under the asymmetry rule an unverifiable
	// signal may only say yes, so "no reply" and "reply saying no" reach the
	// same conclusion here and must not for a terminal implementer checking
	// that a disabled feature is silent rather than answering-but-inert.
	bool probes = false;
	for (int i = 1; i < argc; ++i)
		if (!qstrcmp(argv[i], "--probes")) probes = true;

	prepare_environment();
	QApplication app(argc, argv);
	setup(app);

	// The probe report asks for itself rather than reading the backend's
	// answers, because the backend keeps the parse and not the bytes -- and
	// the bytes are the whole question. Same query, same parser.
	TermCaps probed;
	QByteArray raw;
	if (probes) {
		// Raw mode, for the length of the probe only. Without it the reply
		// sits in the line discipline waiting for a newline that a terminal
		// answering a query never sends, and every probe reports silence --
		// which is what the first version of this did, while the ordinary
		// output in the same run negotiated Kitty. A report that says
		// "silent" when the terminal answered is worse than no report.
		// Only when BOTH ends are a terminal, which is the rule AnsiBackend
		// states and this tool was breaking: "down a pipe there is nobody to
		// answer, and the query would be written into whatever is reading".
		// Measured -- `qtty-negotiate --probes | cat` put the whole query
		// into the pipe and then waited 200 ms for a reply that cannot come.
		//
		// The intended use has stdout on a terminal (doc/beerssh.md runs it
		// with QTTY_NEGOTIATE_OUT so the report goes elsewhere), but
		// `--probes > report.txt` is the obvious thing to type and it
		// corrupted the report with control bytes.
		//
		// Said out loud rather than skipped quietly: a probe report that
		// silently shows every probe as silent is the failure this tool's own
		// comment calls worse than no report.
		// BOTH, and the first version of this guard read only stdout --
		// which is the same defect one end along. Measured with stdout on a
		// pty and stdin on /dev/null: 45 escape sequences went to the user's
		// terminal, nothing could answer because the replies arrive on a
		// descriptor this process is not reading, and they are left in the
		// terminal's own input for whatever runs next.
		if (!isatty(1)) {
			fprintf(stderr, "qtty-negotiate: stdout is not a terminal, so the"
			                " probes were not sent.\n");
			fprintf(stderr, "                Nothing can answer them down a"
			                " pipe, and asking would put the query in it.\n");
			probes = false;
		}
	}
	termios saved{};
	// Raw is part of the condition, not a preparation for it: AnsiBackend
	// probes on `raw_ok_ && tty_out_`, and a cooked stdin holds every reply
	// until a newline that is never coming.
	const bool tty_in = probes && isatty(0) && tcgetattr(0, &saved) == 0;
	if (probes && !tty_in) {
		fprintf(stderr, "qtty-negotiate: stdin is not a terminal in raw mode,"
		                " so the probes were not sent.\n");
		fprintf(stderr, "                The query would go to the terminal"
		                " and its replies to somebody else.\n");
		probes = false;
	}
	if (probes) {
		termios t = saved;
		t.c_lflag &= ~(ICANON | ECHO);
		t.c_cc[VMIN] = 0;
		t.c_cc[VTIME] = 0;
		tcsetattr(0, TCSANOW, &t);
		// The wait is overridable, because 200 ms is a local number. A
		// terminal that has not finished starting answers nothing within
		// it -- measured against kitty under Xvfb, where every probe read
		// SILENT until the terminal had been up three seconds -- and so
		// does one at the end of the 50 ms link section 11 names, where a
		// round trip plus the terminal's own handling can spend most of
		// the budget. Both produce a report identical to a terminal that
		// ignores the questions.
		int wait_ms = 200;
		if (const char *env = getenv("QTTY_PROBE_MS")) {
			const int v = atoi(env);
			if (v > 0 && v <= 60000) wait_ms = v;
		}
		probed = collect_caps(0, 1, wait_ms, &raw);
		tcsetattr(0, TCSANOW, &saved);
	}

	Capabilities c;
	{
		AnsiBackend backend;                      // asks on construction
		c = backend.capabilities();
	}

	static const char *graphics[] = {"NoGraphics", "Halfblocks", "Sixel",
		                                 "ITerm2", "Kitty", "KittyAlpha"};
	static const char *depth[] = {"Mono", "Ansi16", "Xterm256", "TrueColor"};

	const char *path = getenv("QTTY_NEGOTIATE_OUT");
	FILE *out = path ? fopen(path, "w") : stdout;
	if (!out) return 1;
	fprintf(out, "graphics             %s\n", graphics[c.graphics]);
	fprintf(out, "colour               %s\n", depth[c.color]);
	fprintf(out, "unicode placements   %s\n", c.unicode_placements ? "yes" : "no");
	fprintf(out, "cell size            %s\n",
	        c.cell_px.isValid()
	            ? qPrintable(QStringLiteral("%1x%2").arg(c.cell_px.width())
	                             .arg(c.cell_px.height()))
	            : "not reported");
	fprintf(out, "background           %s\n",
	        c.background_known ? qPrintable(c.background.name()) : "not reported");
	// The palette is not on Capabilities: it is consumed by the colour model
	// rather than reported to an application, so it is read from there.
	const QVector<QRgb> pal = terminal_palette();
	fprintf(out, "palette              %s\n",
	        pal.isEmpty() ? "not reported (xterm table assumed)"
	                      : qPrintable(QStringLiteral("%1 low entries, 0=%2 1=%3")
	                            .arg(pal.size())
	                            .arg(QColor(pal[0]).name(), QColor(pal[1]).name())));
	fprintf(out, "mouse                %s\n", c.mouse ? "yes" : "no");
	fprintf(out, "bracketed paste      %s\n", c.bracketed_paste ? "yes" : "no");

	if (probes) {
		fprintf(out, "\n-- probes --\n");
		// The fence first: without the device-attributes reply nothing below
		// means anything, because a missing answer cannot be told from a slow
		// one until the terminal has answered SOMETHING.
		fprintf(out, "DA1 fence            %s\n",
		        probed.answered
		            ? "answered"
		            : "SILENT -- nothing below is conclusive; the terminal"
		              " may not have finished starting, so re-run or raise"
		              " QTTY_PROBE_MS");
		fprintf(out, "kitty a=q            %s\n", probed.kitty ? "OK" : "no OK reply");
		fprintf(out, "DA1 attribute 4      %s\n", probed.sixel ? "present" : "absent");
		fprintf(out, "XTGETTCAP RGB/Tc     %s\n", probed.truecolor ? "confirmed" : "not confirmed");
		fprintf(out, "OSC 11 background    %s\n", probed.bg_known ? "answered" : "silent");
		fprintf(out, "CSI 16t cell size    %s\n", probed.cell_px.isValid() ? "answered" : "silent");
		fprintf(out, "CSI 14t text size    %s\n", probed.text_px.isValid() ? "answered" : "silent");
		fprintf(out, "OSC 4 palette        %s\n",
		        probed.palette16.isEmpty() ? "silent" : "answered");
		// The tri-state, which is the point of the mode: -1 is silence and 0
		// is a terminal saying it does not recognise the mode. They are
		// different facts and qtty keeps them apart everywhere but here.
		// The modes are DERIVED from the query rather than listed again here.
		// The first version carried its own list and included 1002, which
		// caps_query() does not ask about -- so the report said "silent" about
		// a question nobody sent, and silence was the only answer possible.
		// That was one edit away from being sent to the terminal's author as
		// their defect. Two lists of the same thing drift, and this one drifted
		// before it was ever run.
		for (int mode : queried_modes()) {
			const int v = dec_mode(probed, mode);
			fprintf(out, "DECRQM %-14d%s\n", mode,
			        v < 0 ? "silent" : v == 0 ? "0 (not recognised)"
			              : v == 1 ? "1 (set)" : v == 2 ? "2 (reset)"
			              : "3/4 (permanent)");
		}
		QByteArray shown = raw;
		shown.replace('\033', "<ESC>");
		fprintf(out, "raw reply            %d bytes: %s\n",
		        int(raw.size()), shown.isEmpty() ? "(none)" : shown.constData());
	}
	if (out != stdout) fclose(out);
	return 0;
}
