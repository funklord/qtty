<!-- Copied from ~/.claude/guidelines/code-style.md by tool/sync.py.
     Edit the source, not this part: this project's own rules follow
     the end-of-copy line below. -->

# code-style.md

Code style for private projects. This file is the **source**. Every private
project except the one holding it carries a copy at its repo root, written
by `tool/sync.py` -- see *Keeping the copies in sync* at the end.

Vendored submodules, generated sources and attic/historical material are
exempt: they keep whatever their upstream or generator produces. Each
project names its own exempt paths in its part of the file.

## The three rules

1. **`snake_case`, not `camelCase`,** for identifiers this project defines.
2. **Tabs for indentation, spaces for alignment.**
3. **Lowercase filenames,** unless a tool demands otherwise.

Everything below is these three rules in detail, plus the exceptions that
are already settled. An exception not listed here is not yet settled: raise
it rather than deciding in passing.

## 1. Naming

`snake_case` for functions, variables, type names and fields.

This holds **even inside a toolkit whose own API is `camelCase`**. Call the
foreign API exactly as it is spelled (`setParent`, `addWidget`) -- that is
the API's name, not a violation. But names *you* introduce stay
`snake_case`; do not let the surrounding convention pull them across.

- **Name the thing, not its category**: `plan`, not `plan_struct` or
  `plan_result`.
- **No abbreviations that are not already vocabulary.** `observed`, not
  `obs`; `interface`, not `iff`. This matters most where an internal name
  escapes into something you cannot rename later -- a wire format, a config
  key, a CLI output, an on-disk path.
- **One word per concept, everywhere** -- the same word in the type name,
  the file path, the subcommand and the documentation. A synonym introduced
  for variety reads as a second concept.

### Prefixes, and visibility

Prefixes keep this project's symbols from colliding with a library's, so
they follow **visibility**, and the choice is judgement rather than a
mechanical rule:

- **Anything with more than small visibility carries the project prefix**
  -- the public API, and anything a linker or importer outside its own
  module can reach.
- **Module-private symbols are left unprefixed**, so that the absence of a
  prefix reads as "this does not leave the module."

The middle case decides itself on link safety, not taste. A symbol internal
by intent but still reaching the linker -- cross-file within a library, not
`static`, not part of the API -- is *not* private for this purpose: prefix
it. A deliberate parallel copy of a function in two libraries needs a
**distinct** name; the assumption that nothing will ever link both sides
fails later, at a call site that changed nothing, and names files you did
not touch.

Where a language enforces its own scheme, accept it, and say in the
project's part of this file that the toolchain is doing it:

- **Rust** -- `non_snake_case` and `non_camel_case_types` are on by
  default, so types are `PascalCase` and constants `SCREAMING_SNAKE_CASE`
  -- the toolchain's, not a choice. Package systems that demand kebab-case
  (Cargo crates, Debian packages) read back with their own spelling; do not
  invent a third by naming the directory differently from the package.
- **Python** -- a leading underscore (`_name`) is the language's private
  marker and stands in for "unprefixed" above.

## 2. Indentation and alignment

Indent structural nesting with **tab** characters, one tab per level. When
lining up tokens *within* a line -- continuation parameters under an open
paren, a block comment's `*` column, an aligned trailing comment -- use
**spaces**, after the indent tabs. Alignment is then relative to the shared
leading tabs, so it survives at any tab width. No tab width is prescribed;
the viewer decides.

```c
int thing_do(thing_t *thing, const char *name, size_t name_len,
              uint8_t *out, size_t out_cap) {
>---if (!thing) return ERR_MALFORMED;
>---return thing_write(thing, name, name_len, out, out_cap,
>---                    THING_DEFAULT_FLAGS);
}
```

(`>---` marks a tab; everything lining up under `(` is spaces.)

Never mix tabs and spaces *within* the indent itself. Tabs come first and
spaces after; the reverse, or an alternation, breaks at a different tab
width -- and in Python it is a syntax error.

### Settled exceptions

Divergence needs a technical reason. These are accepted:

- **Makefiles** -- a recipe line requires a literal tab, and the body of an
  `ifeq`/`ifdef` outside a recipe may be space-indented, since after a rule
  a tab-led line is read as a recipe. Both are correct in one file, so the
  gate does not apply the tab rule to Makefiles.
- **YAML** -- the spec forbids tabs for indentation. Use spaces.
- **Markdown** -- list continuation and code fences are space-indented by
  specification. Exempt.
- **Debian packaging files** -- exempt, for two reasons measured against
  dpkg. `debian/changelog` has a fixed layout with no tab in it:
  `dpkg-parsechangelog` calls a tab-indented change line "unrecognized"
  and loses the trailer if a tab precedes `--`. A deb822 continuation in
  `control` or `copyright` may lead with a space *or* a tab, but that
  whitespace is field syntax rather than indentation, and everything past
  it is alignment.
- **Go** -- `gofmt` emits tabs natively. Compliant already.
- **Vendored, generated and attic sources** -- exempt, per the header.

**Python**: PEP 8 prefers spaces, but the language's only hard rule is that
indentation must not be *ambiguous*, and tabs-then-spaces is unambiguous at
every tab width. Continuation lines inside brackets are not
indentation-significant at all. Never a space *before* a tab in leading
whitespace -- that is what raises `TabError`.

Anything else that seems to need spaces: signal it to the list in
`claude-guidelines`' `project.md`, follow the rule meanwhile, and it is
settled here in a pass rather than in whichever project met it first.

## 3. Filenames

**Lowercase, always**, for everything the project names itself:
`main_window.cpp`, not `MainWindow.cpp`.

**The separator follows what the name binds to**, which is a technical
difference rather than taste:

- **`snake_case` where the filename becomes an identifier** -- a source
  file, a header, a module. `desired_state.rs` *is* the module
  `desired_state`, and `desired-state.rs` cannot be a module at all, a
  hyphen not being legal in a Rust path; Python imports are the same. That
  is the language's requirement, and not negotiable where it applies.
- **`kebab-case` for prose** -- documentation, design notes, decision
  records. Nothing imports `code-style.md`, so no identifier is at stake,
  and kebab-case is what markdown and URLs settled on. (When this rule said
  `snake_case` for documentation too, 174 of 197 markdown names across the
  workspace were already kebab-case.)

Settled exceptions:

- **Names a tool will not accept lowercased** -- `Makefile`,
  `CMakeLists.txt`, `AndroidManifest.xml`, `Dockerfile`, `Cargo.toml`.
- **Root files with an established convention** -- `README.md`, `LICENSE`,
  `CHANGELOG.md`, `AUTHORS`, `VERSION`. The last is this workspace's own: a
  project that packages or ships a version tracks one, and the build reads
  it for the package version and for what the program prints, so the
  number lives in one place. `claude-guidelines` packages nothing and has
  none.
- **Package-system spellings** -- kebab-case where Cargo or Debian require
  it, which is the spelling prose uses, so a crate directory and the design
  note beside it agree by construction.

### Singular, unless somebody else standardised the plural

**Prefer the singular for a directory this project names itself**:
`helper/`, `doc/`, `fixture/`. The name says what kind of thing lives
there, not how many, and should not have to change when the count does.

Two exceptions, and they are not equal -- the same shape as the lowercase
rule, which yields first to `Makefile` because make will read nothing else,
and only then to `README.md` because the world settled it.

**First: a name a tool requires is not a name we choose.** It outranks the
singular as it outranks lowercase, needs no measurement, and the test is
whether something breaks when the name changes. It is open-ended: a tool
met tomorrow that demands a name gets the same answer, whether the name is
plural, singular, capitalised or none of those. Present here: **Cargo**
looks for `tests/`, `examples/` and `benches/`, and `cargo-fuzz` for
`fuzz_targets/`; **GitHub** requires `.github/workflows/`; **git** keeps
`hooks/`, hence `tool/hooks/`. **A foreign package's directory names are
the same exception one layer out**: the Android SDK's `platform-tools/` and
`build-tools/` are the SDK's spelling. A word-boundary rewrite of `tools/`
once reached into `platform-tools/`, the hyphen being a boundary, and every
adb target in four projects pointed at a binary that was not there -- so a
sweep skips compound names it does not own. Package-system spellings (Cargo
crates, Debian packages) under *Filenames* above are this same carve-out.

**Second: a plural an ecosystem has settled** -- a convention rather than a
requirement: nothing breaks, but a reader would be surprised. Cargo
workspaces keep members in `crates/`. **These need measuring**, and the
project's part of this file names what it measured against, so the next
reader does not reopen it.

Say which kind is being claimed: confusing them lands on whoever renames a
directory that looked like a convention and finds the build broken. And an
inventory name in `harmonization.md` moves only when its owner says so, in
a deliberate pass with each project's gates run against the result -- as
`tools/`, `docs/` and `docs/decisions/` became `tool/`, `doc/` and
`doc/decision/` -- never as a side effect of a style rule.

## ASCII in source

Source and comments are ASCII. Write `--` where prose would use an em dash,
and "section" for a section sign.

This governs **the text the repository writes about itself**, not the data
the software handles. Three exceptions, which are the rule's shape rather
than holes in it:

- **Documentation.** Markdown may use typographic punctuation.
- **User-facing text in UI software.** A tick a program prints is output,
  not prose -- `GREEN('gpg ')` is correct as it stands.
- **Anything that genuinely requires Unicode**: a fixture for a UTF-8
  parser, a terminal emulator's character tables, a font tool.

Where a project needs the rule enforced, `ascii_only` in `.style-gate.toml`
turns it on. In Python and in C/C++ it enforces exactly that shape -- ASCII
outside string literals, Unicode allowed inside -- reading Python with
`tokenize` and C and C++ with a scanner written for the purpose. Every
other language gets a whole-file byte check, and so does a file in those
two that will not lex: a file nobody can parse is not a file that has been
cleared.

**An exception wider than its reason is how a rule stops being enforced.**
The check was once whole-file for everyone, so a project printing two
status ticks switched it off, and an em dash arrived in a comment; in a Qt
tree that kept it off for its toolbar glyphs, 854 of 1114 non-ASCII
characters turned out to be prose in comments.

## Formatters

A formatter is allowed **only if it can be configured to honour the three
rules completely**. Configuration gaps are disqualifying: a formatter that
gets indentation right and alignment wrong rewrites the tree on somebody's
next save. So the decision is per tool, per project, and a real evaluation:

- If it can be made to comply, use it, and commit the config with a comment
  saying which setting is load-bearing and what happens without it.
- If it cannot, do not run it -- **not even ad hoc on a single file**. The
  failure is a silent conversion of files that were already correct,
  found later as a reverted commit rather than an error.
- If no existing tool fits and the rule is worth mechanising, write our
  own. A checker that only gates indentation is worth more than a formatter
  that reflows everything.

**Record the decision and the finding that produced it** in the project's
part of this file -- which tool, what failed, what would change the answer.
A verdict without its evidence gets re-litigated, and a tool that improves
later is never reconsidered because nobody remembers what was wrong with
it.

Naming and filename rules are review items, not automated ones.

## Precedence

Three layers, and they are not equals:

1. **The global guidelines** (`~/.claude/CLAUDE.md` and the files it
   imports) -- the source, and they win.
2. **The project's `project.md`** -- project-specific design and
   conventions.
3. **The project's `code-style.md`** -- this file, copied, and the
   project's own part after it.

A project file that disagrees with the source is **drift, not an
override**: fix it. A project that genuinely needs to diverge needs a
technical reason, and that is not a decision to make while working on
something else -- signal it to the list in `claude-guidelines`'
`project.md` and keep following the source meanwhile.

**When a conflict between layers actually comes up, stop and ask.** Do not
silently pick a winner, even the global one.

This precedence rule lives here and in the global guidelines only. It does
not belong in a `project.md`.

## Keeping the copies in sync

Each private project keeps `code-style.md` at its root, in two parts: this
source, verbatim, under a header naming it; then, after a line marking the
end of the copy, **the project's own part**, holding only what is genuinely
its own -- its exempt paths, formatter verdicts, language-specific notes
and tooling commands, under its own headings. `claude-guidelines` is the
exception: it holds the source at `guidelines/code-style.md`, and its root
file points here.

**`tool/sync.py` writes the copied part and never touches the project's
part**, the same way it spreads the gate. So a copy cannot drift, and a
project that wants a rule to read differently changes the source, through
the signal list, rather than its copy. Until 2026-11-20 copies were adapted
by hand and audited only for their headings, and most had drifted in prose
no audit could see.

**Keep a census out of this document.** A count written here is copied
into every tree and goes stale in all of them at once; state the rule and
the criterion that decides it, and keep the count where it can be
recounted once.

**This source is plain ASCII**, so that it is valid in a tree whose own
rules restrict the characters its files may contain. Keep it so.

**Re-read this source before writing a project's own part, or anything
about the rules**, rather than working from what was loaded at the start of
the session: text written against a stale source is drift being written.
**A copied part that differs from the source** -- a tree the spread has not
reached, a hand edit -- is put right with `sync.py --commit <tree>`, never
by editing the copy; `sync.py --check` lists any that differ. A project's
`project.md` may state the three rules in brief and point here; it does not
restate the precedence rule.

<!-- End of the copied part. What follows is this project's own. -->

## This project

### Exempt paths

`exclude` in `.style-gate.toml` names them: `build`, `build-android`,
`spike`, `__pycache__`, `.claude`. The build trees and `__pycache__` are
generated and exempt by the header's rule. `spike/` is the one that needed
a reason written down.

**`spike/` holds the Phase-0 record** -- the four spikes and the focus
probe exactly as they were run. That is what makes them evidence for the
measurements `doc/design.md` section 16 cites, so reindenting them would
edit the record rather than tidy it. They are historical material, which
the header exempts, and they are standalone: nothing in the library builds
them.

Note that `exclude` **replaces** the gate's shipped defaults rather than
extending them, so that list restates all of them. Adding a path there
means adding it to a list, not to a default.

### The conversion, and the indent ladder

The tree was 4-space indented throughout and was converted to tabs on
2026-08-26 by `tool/style_gate.py fix`: **2372 violations across 48
files**. The fixer carries its own proof -- expanding leading tabs back to
`indent_width` columns must reproduce the original file, and only leading
whitespace is ever rewritten -- so a conversion it cannot prove it refuses
to write.

**The ladder was measured before converting, not assumed.** Of 3277
leading space runs in the tree on conversion day, 1819 were 4 columns, 897
were 8 and 282 were 12, which makes it unambiguously 4. That is why
`.style-gate.toml` says `indent_width = 4`, and it is migration
information only: checking never reads it, because a tab carries the level
and the viewer decides how wide it looks.

### Qt, and the naming rule

Qt's own API is `camelCase` and stays that way at the call site:
`paintEvent`, `sizeHint`, `drawPrimitive`, `setProperty`. That is the API's
spelling, not a violation. **A reimplemented Qt virtual keeps Qt's name**,
because the signature is theirs -- `void paintEvent(QPaintEvent *event)
override`.

Names qtty introduces are `snake_case`.

The project prefix is expressed four ways, one per namespace the language
does not cover, and `doc/design.md` section 10.1 is authoritative for all
of them:

- **C++ namespace** `Qtty::`. Everything public lives in it; nothing at
  global scope.
- **Macros** `QTTY_`-prefixed, and there are no public macros beyond
  include guards. Macros are the one C++ clash no namespace can fix.
- **Environment variables** `QTTY_*`.
- **String namespaces** wherever a string acts as an identifier: dynamic
  properties `"qtty.cells"` and `"qtty.glyph"`, `Q_DECLARE_INTERFACE` ids
  `"org.qtty.*"`, settings keys.

The file-system spelling is lowercase `qtty` -- repo, library, include
path, package and binary names -- which is the KDE repo/namespace pattern
(`kio` and `KIO::`) and is recorded in `README.md`.

### Filenames

Compound C++ basenames were renamed to `snake_case` in the same pass as
the tab conversion:

    cellbuffer.cpp        -> cell_buffer.cpp
    gridstyle.cpp         -> grid_style.cpp
    cellpaint.cpp         -> cell_paint.cpp
    inputrouter.cpp       -> input_router.cpp
    ansibackend.{cpp,h}   -> ansi_backend.{cpp,h}
    nullbackend.h         -> null_backend.h

The run-together spelling is Qt's convention, not this workspace's. Every
sibling here is already `snake_case` -- hydra has `address_input.cpp`,
bbq-predictor `main_window.cpp` -- so this was drift being fixed rather
than a local rule being invented.

`qtty.pro` and `qtty.pri` are package-system spellings: qmake wants the
project file named for the target.

### Directory names

Renamed in the same pass, and the two kinds are worth keeping apart:

    docs/             -> doc/
    tools/            -> tool/
    tests/            -> test/
    examples/         -> example/
    spikes/           -> spike/
    src/backends/     -> src/backend/
    src/widgets/      -> src/widget/
    test/snapshots/   -> test/snapshot/

`doc/` and `tool/` are **the settled inventory** in
`~/.claude/guidelines/harmonization.md` -- canonical names, not this
project's choice. The rest is the singular rule above, applied to
directories this project names itself. Nothing here is a tool-required
plural: this is qmake and CTest, neither of which looks for a directory by
name the way Cargo looks for `tests/`.

**`spike/` was already what `doc/design.md` called it**, so that rename
closed a design-versus-code gap rather than opening one.

### ASCII in source: the split, and why `ascii_only` is not yet on

qtty is a terminal library, so it legitimately emits non-ASCII characters
as **output**. Measured on 2026-08-26, its C and C++ sources hold **340
non-ASCII characters**, and they fall into exactly the two groups the
source's rule distinguishes:

- **Prose that collected in comments** -- 115 section signs and 79 em
  dashes, which are the two characters the rule names by example, plus a
  scattering of arrows. These are the repository writing about itself, and
  the rule says they should be `--` and "section". (The 80th em dash is
  not one of them: it sits inside a `setPlaceholderText` string in the
  chat example, which is user-facing text and stays.)
- **Genuine output in string literals** -- box-drawing characters (the
  light horizontal and vertical, and the four light corners), block and
  shade characters (full block, the three shades, upper and lower half
  block), arrow and marker glyphs (the small triangles, black circle,
  star, smiley), plus CJK and accented characters in the wide-cluster and
  grapheme test fixtures. A terminal emulator's character tables are named
  in the source as an explicit exception, and these are that.

This is precisely the split `ascii_only` was built for: the gate lexes C
and C++, so a glyph inside a string literal is output and stays, while an
em dash in a comment is prose and does not.

**`ascii_only` is not currently set in `.style-gate.toml`, and the key
being absent is the honest state**: the comment prose has not yet been
converted. Turning the flag on today would report that prose and nothing
else -- no fixture, no box-drawing glyph, no placeholder string.
Converting it is what closes this, and it is a mechanical bulk edit that
carries a proof rather than a sampled diff. The flag goes on with that
change, not before it, and claiming the rule is enforced while the key is
absent would be exactly the vacuous pass the guidelines warn about.

### Settled: PascalCase type names inside `namespace Qtty`

**Decided by the copyright holder, 2026-08-26.** Types inside
`namespace Qtty` are `PascalCase`; everything else this project introduces
-- functions, methods, variables, struct fields -- stays `snake_case`. This
is a divergence from the source's "snake_case for functions, variables,
type names and fields", and it is recorded here with its reason rather
than left to be re-derived.

The reason is that qtty's entire public surface is Qt's: the types are
`QProxyStyle`, `QPaintDevice` and `QPaintEngine` subclasses, and a caller
writes `Qtty::GridStyle` in the same expression as
`QStyle::PE_FrameWindow`. Spelling them `qtty::grid_style` would make this
one library read unlike every other library it appears beside, and unlike
the toolkit it exists to serve.

The namespace itself was capitalised on the same reasoning, deliberately:
commit `e099862` ("Namespace: qtty:: -> Qtty:: (Qt-ecosystem convention)")
moved it to match `Qt::`, `KIO::` and `QXlsx::`, following the KDE pattern
of a lowercase repository `kio` with a namespace `KIO`. `README.md`
documents that choice along with the lowercase file-system spelling that
goes with it, and `project.md` records it as a decision.

**The carve-out is exactly type names, and nothing wider.** A method is
`put_cluster`, not `putCluster`; a field is `cell_rect`, not `cellRect`.
Where the two rules meet in one declaration, the type takes Qt's spelling
and the member takes this workspace's:

```cpp
namespace Qtty {
class CellBuffer {
public:
	void put_cluster(int col, int row, const QString &ch);
	QRegion diff(const CellBuffer &prev) const;
};
}
```

The exception does not extend to the rest of the workspace: it is qtty's,
argued from qtty's position inside the Qt ecosystem, and a project without
that position does not inherit it.

### Formatter: clang-format is deliberately NOT used

There is no `.clang-format` here, and adding one would be a mistake rather
than an oversight. Two findings, both established in the sibling Qt trees
rather than re-derived here:

- **With no config present, `clang-format` falls back to LLVM defaults,
  which are spaces.** Running it silently converts a tab-indented tree
  that was already correct -- which has actually happened in
  `fuzzypickles` and `beerssh`, and was discovered as a reverted commit
  rather than as an error. That is the exact failure mode the *Formatters*
  section names.
- **A config does not fix it.** This project's continuation parameters sit
  one column past the open paren, and `AlignAfterOpenBracket` cannot
  express that -- every mode it has aligns *to* the paren. So any config
  would reflow nearly every multi-line signature in the tree.

**Do not run it, not even ad hoc on a single file.**

What would change the answer: an `AlignAfterOpenBracket` mode that offsets
from the paren rather than aligning to it, together with tab indentation
that survives a missing config. If both arrive, this verdict is worth
re-taking; until then it is settled and the evidence is above so that it
does not get re-litigated.

The gate is `tool/style_gate.py`, spread verbatim from
`~/.claude/tool/style_gate.py`, and its per-tree configuration is
`.style-gate.toml`. The `floor` there makes the gate fail rather than pass
when the file list collapses, which is what stops a vacuous green.

### Tooling

    make style                          both gates below
    make style-source                   tabs, trailing whitespace, final newline
    make style-docs                     project.md held against the tree
    python3 tool/style_gate.py check    the source gate directly
    python3 tool/style_gate.py fix PATH convert a path, with its own proof
    python3 tool/style_gate.py list     what would be checked, and why

`list` is the one to run when a gate passes suspiciously fast: it prints
the file list and the reason each path is in or out, which is how a
vacuous pass is told from a real one.

### See also

- **`project.md`** -- design and intent, authoritative over the code. It
  wins over this file where the two disagree.
- **`doc/design.md` section 10.1** -- the namespace and global-state
  contract: `namespace Qtty`, no public macros, the `"qtty.*"` and
  `"org.qtty.*"` string namespaces, and which Qt singletons the library
  owns in TUI mode. The naming rules above are this file's restatement of
  it; that section is authoritative.
