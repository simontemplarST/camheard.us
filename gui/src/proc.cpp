#include "proc.h"

#include "util.h"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

bool Proc::Start(const std::vector<std::string> &argv, const std::string &cwd, std::string *err) {
	if (Running()) { *err = "already running"; return false; }
	Clear();
	pid = -1;
	fd = -1;
	finished = false;
	exit_code = -1;
	cmdline = util::Join(argv, " ");

	int pipefd[2];
	if (pipe(pipefd) != 0) { *err = std::string("pipe: ") + strerror(errno); return false; }

	pid_t p = fork();
	if (p < 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		*err = std::string("fork: ") + strerror(errno);
		return false;
	}
	if (p == 0) {
		// Child. Its own process group, so Stop() can take down the whole
		// tree with one kill().
		setpgid(0, 0);
		close(pipefd[0]);
		dup2(pipefd[1], STDOUT_FILENO);
		dup2(pipefd[1], STDERR_FILENO);
		close(pipefd[1]);
		int devnull = open("/dev/null", O_RDONLY);
		if (devnull >= 0) { dup2(devnull, STDIN_FILENO); close(devnull); }
		if (!cwd.empty() && chdir(cwd.c_str()) != 0) {
			fprintf(stderr, "cannot enter %s: %s\n", cwd.c_str(), strerror(errno));
			_exit(126);
		}
		std::vector<char *> c_argv;
		for (const auto &a : argv) c_argv.push_back(const_cast<char *>(a.c_str()));
		c_argv.push_back(nullptr);
		execvp(c_argv[0], c_argv.data());
		fprintf(stderr, "cannot run %s: %s\n", c_argv[0], strerror(errno));
		_exit(127);
	}

	close(pipefd[1]);
	fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
	fd = pipefd[0];
	pid = p;
	started = true;
	lines.push_back("$ " + cmdline);
	return true;
}

void Proc::Poll() {
	if (fd >= 0) {
		char buf[4096];
		for (;;) {
			ssize_t n = read(fd, buf, sizeof buf);
			if (n > 0) {
				partial.append(buf, (size_t)n);
				size_t nl;
				while ((nl = partial.find('\n')) != std::string::npos) {
					std::string line = partial.substr(0, nl);
					if (!line.empty() && line.back() == '\r') line.pop_back();
					lines.push_back(line);
					partial.erase(0, nl + 1);
				}
				// A single runaway line (hugo's progress output has none, but
				// a stray binary would) must not grow without bound.
				if (partial.size() > 64 * 1024) {
					lines.push_back(partial);
					partial.clear();
				}
				while (lines.size() > max_lines) lines.pop_front();
				continue;
			}
			if (n == 0) { // EOF: the child closed both ends
				close(fd);
				fd = -1;
				break;
			}
			if (errno == EAGAIN || errno == EWOULDBLOCK) break;
			if (errno == EINTR) continue;
			close(fd);
			fd = -1;
			break;
		}
	}
	if (pid > 0 && !finished) {
		int st = 0;
		pid_t r = waitpid(pid, &st, WNOHANG);
		if (r == pid) {
			// Don't declare the run over until the pipe has been drained,
			// or the last lines of a fast command are lost.
			if (fd >= 0) return;
			finished = true;
			exit_code = WIFEXITED(st) ? WEXITSTATUS(st) : (WIFSIGNALED(st) ? 128 + WTERMSIG(st) : -1);
			if (!partial.empty()) { lines.push_back(partial); partial.clear(); }
			lines.push_back(exit_code == 0 ? "[done]" : "[exit " + std::to_string(exit_code) + "]");
			while (lines.size() > max_lines) lines.pop_front();
			pid = -1;
		}
	}
}

void Proc::Stop() {
	if (pid <= 0) return;
	kill(-pid, SIGTERM);
	// Give it a moment; anything still alive after that gets SIGKILL. This
	// is the one place the UI blocks, and it is bounded at 500 ms.
	for (int i = 0; i < 50; i++) {
		int st = 0;
		if (waitpid(pid, &st, WNOHANG) == pid) {
			exit_code = WIFEXITED(st) ? WEXITSTATUS(st) : 130;
			finished = true;
			pid = -1;
			break;
		}
		usleep(10000);
	}
	if (pid > 0) {
		kill(-pid, SIGKILL);
		int st = 0;
		waitpid(pid, &st, 0);
		exit_code = 137;
		finished = true;
		pid = -1;
	}
	if (fd >= 0) {
		// Drain whatever the child managed to say before it died.
		char buf[4096];
		while (read(fd, buf, sizeof buf) > 0) {}
		close(fd);
		fd = -1;
	}
	lines.push_back("[stopped]");
}

void Proc::Clear() {
	lines.clear();
	partial.clear();
}

std::string Proc::Tail(size_t n) const {
	std::string out;
	size_t skip = lines.size() > n ? lines.size() - n : 0;
	size_t i = 0;
	for (const auto &l : lines) {
		if (i++ < skip) continue;
		out += l;
		out += "\n";
	}
	return out;
}

int RunCapture(const std::vector<std::string> &argv, const std::string &cwd, std::string *out) {
	int pipefd[2];
	if (pipe(pipefd) != 0) return -1;
	pid_t p = fork();
	if (p < 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		return -1;
	}
	if (p == 0) {
		close(pipefd[0]);
		dup2(pipefd[1], STDOUT_FILENO);
		dup2(pipefd[1], STDERR_FILENO);
		close(pipefd[1]);
		int devnull = open("/dev/null", O_RDONLY);
		if (devnull >= 0) { dup2(devnull, STDIN_FILENO); close(devnull); }
		if (!cwd.empty() && chdir(cwd.c_str()) != 0) _exit(126);
		std::vector<char *> c_argv;
		for (const auto &a : argv) c_argv.push_back(const_cast<char *>(a.c_str()));
		c_argv.push_back(nullptr);
		execvp(c_argv[0], c_argv.data());
		_exit(127);
	}
	close(pipefd[1]);
	char buf[4096];
	ssize_t n;
	out->clear();
	while ((n = read(pipefd[0], buf, sizeof buf)) > 0) out->append(buf, (size_t)n);
	close(pipefd[0]);
	int st = 0;
	waitpid(p, &st, 0);
	return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

void EnsureToolPath() {
	const char *cur = getenv("PATH");
	std::string path = cur ? cur : "/usr/bin:/bin";
	for (const char *extra : {"/home/linuxbrew/.linuxbrew/bin", "/opt/homebrew/bin", "/usr/local/bin"}) {
		if (!util::IsDir(extra)) continue;
		if (path.find(extra) != std::string::npos) continue;
		path += ":";
		path += extra;
	}
	setenv("PATH", path.c_str(), 1);
}

bool HaveTool(const std::string &name) {
	const char *path = getenv("PATH");
	if (!path) return false;
	std::string p(path), cur;
	for (size_t i = 0; i <= p.size(); i++) {
		if (i == p.size() || p[i] == ':') {
			if (!cur.empty() && access((cur + "/" + name).c_str(), X_OK) == 0) return true;
			cur.clear();
		} else cur += p[i];
	}
	return false;
}
