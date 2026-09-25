# hugofe — a desktop front end for this site

A single native binary that manages the whole Hugo site: the pages, the
writing, the site's own UI, the files it ships, and the build/commit/push
that publishes it. Dear ImGui over SDL2 + OpenGL; ImGui is vendored, so the
only outside dependencies are SDL2 and libGL.

```sh
make            # build ./hugofe
make run        # build and launch it against the site above this directory
make test       # the parsing/editing layer — no window needed
./hugofe --selftest      # drives the editor through a real frame loop
./hugofe --screenshot D  # writes a PNG of every tab into D, then exits
```

With no argument it walks up from the working directory looking for a
`hugo.toml` next to a `content/` directory, so it can be started from here or
from the site root. Pass a directory to point it elsewhere.

## The tabs

**Content** — every page under `content/`, sortable and filterable, with
word counts and draft state. New post (from the archetype, so the front
matter is whatever `archetypes/default.md` says), duplicate, rename —
renaming is renaming the URL, and the dialog says so — publish/unpublish,
delete, open in the local preview.

**Editor** — front matter as a form on top (title, draft, date, summary,
tags; everything else behind *More fields*, where nothing is invented and
nothing is dropped), then the markdown source beside a live preview painted
in PaperMod's own colours. The toolbar's buttons and `Ctrl+B`/`Ctrl+I`/
`Ctrl+K` are toggles: applying bold to already-bold text removes it. The
preview is a likeness, not a second Goldmark — *Preview in browser* saves
and opens the page in `hugo server`, which is the authority.

*find* (`Ctrl+F`) opens find-and-replace over the open post: match count,
wrap-around next/previous, case toggle, replace one or all. *Replace* only
fires on the match the caret is sitting on, so it can never quietly eat
different text than the one on screen. *outline* opens a rail of the post's
headings; clicking one moves the caret, and the rail says which heading the
caret is under. Both drive the text box from outside it and so go through
the same queue the toolbar does — see below.

**Design** — the site's own UI, edited against a drawing of it. Header,
menu, profile card, buttons, social icons, post-list cards and footer are
all rendered from `hugo.toml`; clicking a piece of the mock jumps the
inspector to the setting behind it. Writes go through a line-preserving TOML
editor, so the comments and the ordering in `hugo.toml` survive being
edited — that file is hand-written and full of notes worth keeping.

**Media** — what is in `static/` and `assets/`, with the reference each one
needs (`static/img/a.png` is `/img/a.png`; `assets/` has no direct URL).
Dropping a file onto the window copies it into `static/img/`, and inserts
the reference if a post is open. The *Used* column comes from the Check tab's
pass: `nothing` means no page and no setting in `hugo.toml` refers to it, and
*only unreferenced* narrows the list to those.

**Check** — the things a Hugo build is perfectly happy about: internal links
with no page behind them, image references with no file behind them, files
in `static/` and `assets/` that nothing points at any more, and pages
missing a date, a summary or tags. `hugo` exits 0 on every one of these, so
none of it surfaces until someone clicks the wrong thing on the live site.
Errors sort to the top and every row opens the file it is about. The same
pass feeds the Media tab's *Used* column, which is why a file referenced
only from `hugo.toml` (the avatar, the favicons) counts as referenced.

**Publish** — `hugo server -D -E -F` for the local preview, a local build as
a pre-flight, `git` status/commit/push, and the GitHub Actions run that
publishes to Pages. A push to `main` is what deploys; this tab is the same
sequence `deploy.sh` does by hand.

## Quick open

`Ctrl+P` is one box over every page and every command: fuzzy-matched, pages
first, matched letters picked out. `Enter` goes, arrows move, `Esc` closes.
`F1` lists the keys it can't teach you by being typed at.

| key | |
| --- | --- |
| `Ctrl+P` | quick open: any page, any command |
| `Ctrl+N` | new post |
| `Ctrl+S` | save the open post and `hugo.toml` |
| `Ctrl+R` | rescan `content/` from disk |
| `Ctrl+F` | find and replace in the open post |
| `Ctrl+B` / `Ctrl+I` / `Ctrl+K` | bold / italic / link, all toggles |
| `F1` | the same table, in the app |

## Layout

| file | what it is |
| --- | --- |
| `src/util.*` | strings, files, paths — no dependencies |
| `src/fm.*` | Hugo front matter: TOML (`+++`) and YAML (`---`), parse and serialise |
| `src/toml_edit.*` | line-preserving `hugo.toml` editing (not a parser + writer) |
| `src/site.*` | the content tree, permalinks, archetypes, file operations |
| `src/md.*` | the markdown model, the toolbar's text transforms, find/replace and the outline |
| `src/fuzzy.*` | subsequence matching with a score, for quick open |
| `src/audit.*` | the site check: links, images, orphaned files, front matter |
| `src/md_render.*` | drawing that model with ImGui, in PaperMod's colours |
| `src/proc.*` | non-blocking subprocesses (hugo, git, gh) |
| `src/theme.*`, `src/widgets.*` | palette, fonts, shared UI pieces |
| `src/app_*.cpp` | one file per tab, plus `app_palette.cpp` for quick open and the shortcut sheet |
| `src/shot.*` | framebuffer → PNG, for `--screenshot` |
| `tests/core_test.cpp` | everything above that doesn't need a window |

`src/util.*` through `src/md.*`, plus `fuzzy` and `audit`, are deliberately
ImGui-free, which is what lets `make test` cover them offline — `audit` is
tested against a whole throwaway site built in `$TMPDIR`. The two things a
unit test cannot reach — drawing, and the editor's path through an ImGui
callback — are covered by `--screenshot` and `--selftest` instead.

## Two things worth knowing if you change this

**Toolbar edits go through the InputText callback, not the button handler.**
ImGui re-applies a widget's own stored buffer on the frame after it loses
focus (`imgui_widgets.cpp`, "Handle reapplying final data on deactivation"),
so text changed straight from a button handler is silently reverted a frame
later. `EditAction` is queued and applied inside `EditorCallback`, which is
why it survives. The same applies to the caret: jumping to a search match or
a heading is `EditAction::Select`, queued like any edit, and moving
`CursorPos` from in there is also what scrolls the box to it. The find bar
borrows focus for the frame the action lands on and takes it straight back,
or the next keystroke would go into the post.

`--selftest` exists to keep all of that true. Its steps wait for the queue to
drain rather than counting frames — how long ImGui takes to hand over focus
is ImGui's business, and counting frames made the run flaky on a cold start,
which is the last thing a test of the focus plumbing should be.

**`hugo.toml` is edited a line at a time on purpose.** Round-tripping it
through a real TOML parser would drop every comment in it the first time
something was saved. `toml_edit` only rewrites the line a setter targets,
and only handles single-line values — which is all this file has.
