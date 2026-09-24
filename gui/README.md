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

**Design** — the site's own UI, edited against a drawing of it. Header,
menu, profile card, buttons, social icons, post-list cards and footer are
all rendered from `hugo.toml`; clicking a piece of the mock jumps the
inspector to the setting behind it. Writes go through a line-preserving TOML
editor, so the comments and the ordering in `hugo.toml` survive being
edited — that file is hand-written and full of notes worth keeping.

**Media** — what is in `static/` and `assets/`, with the reference each one
needs (`static/img/a.png` is `/img/a.png`; `assets/` has no direct URL).
Dropping a file onto the window copies it into `static/img/`, and inserts
the reference if a post is open.

**Publish** — `hugo server -D -E -F` for the local preview, a local build as
a pre-flight, `git` status/commit/push, and the GitHub Actions run that
publishes to Pages. A push to `main` is what deploys; this tab is the same
sequence `deploy.sh` does by hand.

## Layout

| file | what it is |
| --- | --- |
| `src/util.*` | strings, files, paths — no dependencies |
| `src/fm.*` | Hugo front matter: TOML (`+++`) and YAML (`---`), parse and serialise |
| `src/toml_edit.*` | line-preserving `hugo.toml` editing (not a parser + writer) |
| `src/site.*` | the content tree, permalinks, archetypes, file operations |
| `src/md.*` | the markdown model and the toolbar's text transforms |
| `src/md_render.*` | drawing that model with ImGui, in PaperMod's colours |
| `src/proc.*` | non-blocking subprocesses (hugo, git, gh) |
| `src/theme.*`, `src/widgets.*` | palette, fonts, shared UI pieces |
| `src/app_*.cpp` | one file per tab |
| `src/shot.*` | framebuffer → PNG, for `--screenshot` |
| `tests/core_test.cpp` | everything above that doesn't need a window |

`src/util.*` through `src/md.*` are deliberately ImGui-free, which is what
lets `make test` cover them offline. The two things a unit test cannot
reach — drawing, and the editor's toolbar path through an ImGui callback —
are covered by `--screenshot` and `--selftest` instead.

## Two things worth knowing if you change this

**Toolbar edits go through the InputText callback, not the button handler.**
ImGui re-applies a widget's own stored buffer on the frame after it loses
focus (`imgui_widgets.cpp`, "Handle reapplying final data on deactivation"),
so text changed straight from a button handler is silently reverted a frame
later. `EditAction` is queued and applied inside `EditorCallback`, which is
why it survives. `--selftest` exists to keep that true.

**`hugo.toml` is edited a line at a time on purpose.** Round-tripping it
through a real TOML parser would drop every comment in it the first time
something was saved. `toml_edit` only rewrites the line a setter targets,
and only handles single-line values — which is all this file has.
