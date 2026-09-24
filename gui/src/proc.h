// proc -- run hugo, git and gh without blocking the frame loop.
//
// Every long-running thing this GUI does is someone else's binary, so there
// is exactly one way to run them: fork, exec, read the merged stdout/stderr
// from a non-blocking pipe once per frame, keep the last N lines. Nothing
// here ever waits on a child while a frame is in flight -- a `hugo server`
// that runs for an hour and a `git push` that takes four seconds use the
// same code path.
#pragma once

#include <deque>
#include <string>
#include <sys/types.h>
#include <vector>

struct Proc {
	pid_t pid = -1;
	int fd = -1;
	std::deque<std::string> lines;
	size_t max_lines = 1500;
	int exit_code = -1;
	bool finished = false;   // ran and reaped
	bool started = false;
	std::string cmdline;     // for the log header
	std::string partial;     // bytes read that aren't a whole line yet

	// argv[0] is looked up on PATH. `cwd` may be empty for "don't chdir".
	// Returns false (and fills `err`) only when fork/pipe itself fails --
	// a missing binary surfaces as an exit code, with the reason on the log.
	bool Start(const std::vector<std::string> &argv, const std::string &cwd, std::string *err);
	// Drains whatever is readable and reaps the child if it has exited.
	// Cheap enough to call unconditionally every frame.
	void Poll();
	bool Running() const { return pid > 0 && !finished; }
	// SIGTERM to the whole process group, then SIGKILL if it is still there.
	// The group matters: `hugo server` spawns children, and killing only the
	// leader leaves the port bound.
	void Stop();
	void Clear();
	std::string Tail(size_t n) const;
};

// Synchronous, for the short ones (git status, gh run list). Returns the exit
// code, or -1 if the process could not be started; output is merged.
int RunCapture(const std::vector<std::string> &argv, const std::string &cwd, std::string *out);

// Adds Homebrew/Linuxbrew's bin to PATH for this process if it isn't already
// there -- hugo and gh live there on an rpm-ostree box, and a GUI launched
// from a desktop file does not inherit a login shell's PATH.
void EnsureToolPath();

// PATH lookup, so the UI can say "hugo isn't installed" up front rather than
// after a failed run.
bool HaveTool(const std::string &name);
