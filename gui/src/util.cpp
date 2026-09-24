#include "util.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace util {

std::string Trim(const std::string &s) {
	size_t a = s.find_first_not_of(" \t\r\n");
	if (a == std::string::npos) return "";
	size_t b = s.find_last_not_of(" \t\r\n");
	return s.substr(a, b - a + 1);
}

std::string TrimRight(const std::string &s) {
	size_t b = s.find_last_not_of(" \t\r\n");
	if (b == std::string::npos) return "";
	return s.substr(0, b + 1);
}

bool StartsWith(const std::string &s, const std::string &pre) {
	return s.size() >= pre.size() && s.compare(0, pre.size(), pre) == 0;
}

bool EndsWith(const std::string &s, const std::string &suf) {
	return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

std::string Lower(const std::string &s) {
	std::string o = s;
	std::transform(o.begin(), o.end(), o.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return o;
}

bool IContains(const std::string &hay, const std::string &needle) {
	if (needle.empty()) return true;
	return Lower(hay).find(Lower(needle)) != std::string::npos;
}

std::vector<std::string> SplitLines(const std::string &s) {
	std::vector<std::string> out;
	std::string cur;
	for (char c : s) {
		if (c == '\n') {
			if (!cur.empty() && cur.back() == '\r') cur.pop_back();
			out.push_back(cur);
			cur.clear();
		} else {
			cur += c;
		}
	}
	// A trailing newline means "no final partial line" -- don't invent one,
	// otherwise every save grows the file by a blank line.
	if (!cur.empty()) {
		if (!cur.empty() && cur.back() == '\r') cur.pop_back();
		out.push_back(cur);
	}
	return out;
}

std::string Join(const std::vector<std::string> &v, const std::string &sep) {
	std::string o;
	for (size_t i = 0; i < v.size(); i++) {
		if (i) o += sep;
		o += v[i];
	}
	return o;
}

std::string Replace(std::string s, const std::string &from, const std::string &to) {
	if (from.empty()) return s;
	size_t p = 0;
	while ((p = s.find(from, p)) != std::string::npos) {
		s.replace(p, from.size(), to);
		p += to.size();
	}
	return s;
}

std::string Slugify(const std::string &s) {
	std::string o;
	bool dash = false;
	for (unsigned char c : s) {
		if (std::isalnum(c)) {
			o += (char)std::tolower(c);
			dash = false;
		} else if (!dash && !o.empty()) {
			o += '-';
			dash = true;
		}
	}
	while (!o.empty() && o.back() == '-') o.pop_back();
	return o;
}

bool ReadFile(const std::string &path, std::string *out) {
	FILE *f = fopen(path.c_str(), "rb");
	if (!f) return false;
	std::string data;
	char buf[8192];
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0) data.append(buf, n);
	fclose(f);
	*out = data;
	return true;
}

bool WriteFileAtomic(const std::string &path, const std::string &data) {
	std::string tmp = path + ".hugofe-tmp";
	FILE *f = fopen(tmp.c_str(), "wb");
	if (!f) return false;
	bool ok = data.empty() || fwrite(data.data(), 1, data.size(), f) == data.size();
	if (fflush(f) != 0) ok = false;
	if (fsync(fileno(f)) != 0) ok = false;
	fclose(f);
	if (!ok) {
		unlink(tmp.c_str());
		return false;
	}
	if (rename(tmp.c_str(), path.c_str()) != 0) {
		unlink(tmp.c_str());
		return false;
	}
	return true;
}

std::string DirName(const std::string &path) {
	size_t p = path.find_last_of('/');
	if (p == std::string::npos) return ".";
	if (p == 0) return "/";
	return path.substr(0, p);
}

std::string BaseName(const std::string &path) {
	size_t p = path.find_last_of('/');
	return p == std::string::npos ? path : path.substr(p + 1);
}

std::string StripExt(const std::string &s) {
	size_t p = s.find_last_of('.');
	size_t sl = s.find_last_of('/');
	if (p == std::string::npos || (sl != std::string::npos && p < sl)) return s;
	return s.substr(0, p);
}

bool IsDir(const std::string &path) {
	struct stat st;
	return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool Exists(const std::string &path) {
	struct stat st;
	return stat(path.c_str(), &st) == 0;
}

bool MakeDirs(const std::string &path) {
	if (path.empty() || path == "/" || path == ".") return true;
	if (IsDir(path)) return true;
	if (!MakeDirs(DirName(path))) return false;
	return mkdir(path.c_str(), 0755) == 0 || IsDir(path);
}

long long MTime(const std::string &path) {
	struct stat st;
	if (stat(path.c_str(), &st) != 0) return 0;
	return (long long)st.st_mtime;
}

long long FileSize(const std::string &path) {
	struct stat st;
	if (stat(path.c_str(), &st) != 0) return 0;
	return (long long)st.st_size;
}

std::string NowRFC3339() {
	time_t t = time(nullptr);
	struct tm lt;
	localtime_r(&t, &lt);
	char buf[64];
	strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S%z", &lt);
	// strftime gives -0500; RFC3339 wants -05:00.
	std::string s(buf);
	if (s.size() >= 5) s.insert(s.size() - 2, ":");
	return s;
}

std::string PrettyDate(const std::string &rfc3339) {
	if (rfc3339.size() < 10) return rfc3339;
	std::string d = rfc3339.substr(0, 10);
	if (rfc3339.size() >= 16 && rfc3339[10] == 'T') d += " " + rfc3339.substr(11, 5);
	return d;
}

std::string PrettyEpoch(long long epoch) {
	if (epoch <= 0) return "—";
	time_t t = (time_t)epoch;
	struct tm lt;
	localtime_r(&t, &lt);
	char buf[32];
	strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &lt);
	return buf;
}

std::string HumanSize(long long b) {
	char buf[64];
	if (b < 1024) snprintf(buf, sizeof buf, "%lld B", b);
	else if (b < 1024 * 1024) snprintf(buf, sizeof buf, "%.1f KB", b / 1024.0);
	else snprintf(buf, sizeof buf, "%.1f MB", b / (1024.0 * 1024.0));
	return buf;
}

void OpenURL(const std::string &url) {
	// Double-fork: the grandchild is reparented to init, so it leaves no
	// zombie for us to reap. A plain fork would, and we deliberately do NOT
	// set SIGCHLD to SIG_IGN here -- Proc::Poll() needs waitpid() to work.
	pid_t pid = fork();
	if (pid == 0) {
		if (fork() == 0) {
			setsid();
			// Detach stdio so a chatty xdg-open can't scribble on our stderr.
			if (!freopen("/dev/null", "r", stdin)) _exit(127);
			if (!freopen("/dev/null", "w", stdout)) _exit(127);
			if (!freopen("/dev/null", "w", stderr)) _exit(127);
			execlp("xdg-open", "xdg-open", url.c_str(), (char *)nullptr);
			_exit(127);
		}
		_exit(0);
	}
	if (pid > 0) {
		int st = 0;
		waitpid(pid, &st, 0);
	}
}

} // namespace util
