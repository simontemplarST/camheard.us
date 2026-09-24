// Publish tab -- build it, look at it, commit it, push it.
//
// This deliberately mirrors what deploy.sh does by hand (build locally as a
// pre-flight, commit, push, then watch the Pages workflow), because that is
// the path this site actually deploys through: a push to main is what makes
// GitHub Actions rebuild and publish.
#include "app.h"
#include "util.h"
#include "widgets.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <ctime>

namespace {

void RefreshGit(App &a) {
	if (!a.have_git) return;
	std::string out;
	RunCapture({"git", "status", "--porcelain"}, a.site.root, &out);
	a.git_status = out;
	a.git_status_at = ImGui::GetTime();
}

int CountChanges(const std::string &porcelain) {
	int n = 0;
	for (const std::string &l : util::SplitLines(porcelain))
		if (!util::Trim(l).empty()) n++;
	return n;
}

} // namespace

void DrawPublishTab(App &a) {
	// git status is cheap and local; refreshing it a few times a minute keeps
	// the list honest without hammering the disk.
	if (a.git_status_at == 0 || ImGui::GetTime() - a.git_status_at > 5.0) RefreshGit(a);

	float log_w = 520;
	ImVec2 avail = ImGui::GetContentRegionAvail();
	ImGui::BeginChild("actions", ImVec2(avail.x - log_w - 10, 0), ImGuiChildFlags_None);

	// ---- local preview -----------------------------------------------------
	W::SectionHeader("Local preview");
	ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
	ImGui::TextWrapped("`hugo server -D -E -F`: drafts, expired and future-dated pages all render, and the "
	                   "page reloads as you save.");
	ImGui::PopStyleColor();
	ImGui::BeginDisabled(!a.have_hugo);
	if (a.server.Running()) {
		if (W::DangerButton("Stop server", ImVec2(150, 0))) a.StartServer();
	} else {
		if (W::PrimaryButton("Start server", ImVec2(150, 0))) a.StartServer();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::SetNextItemWidth(110);
	ImGui::BeginDisabled(a.server.Running());
	ImGui::InputInt("port", &a.server_port);
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!a.server.Running());
	if (ImGui::Button("Open in browser")) util::OpenURL("http://localhost:" + std::to_string(a.server_port) + "/");
	ImGui::EndDisabled();

	// ---- build -------------------------------------------------------------
	W::SectionHeader("Build");
	ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
	ImGui::TextWrapped("A local build is a pre-flight check: if it fails here it will fail in Actions. The "
	                   "output in public/ is git-ignored -- Pages builds its own copy from the push.");
	ImGui::PopStyleColor();
	ImGui::BeginDisabled(!a.have_hugo || a.task.Running());
	if (ImGui::Button("Build site", ImVec2(150, 0)))
		a.RunTask({"hugo", "--gc", "--minify", "--cleanDestinationDir"});
	ImGui::SameLine();
	if (ImGui::Button("Build with drafts", ImVec2(160, 0)))
		a.RunTask({"hugo", "--gc", "--minify", "-D", "--cleanDestinationDir"});
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("hugo version", ImVec2(130, 0))) a.RunTask({"hugo", "version"});

	// ---- git ---------------------------------------------------------------
	W::SectionHeader("Commit and push");
	if (!a.have_git) {
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::WARN));
		ImGui::TextWrapped("git is not on PATH, so this section is inert.");
		ImGui::PopStyleColor();
	} else {
		int n = CountChanges(a.git_status);
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(n ? Theme::TEXT : Theme::TEXT_FAINT));
		ImGui::Text("%d changed file%s", n, n == 1 ? "" : "s");
		ImGui::PopStyleColor();

		ImGui::PushFont(Theme::F_MONO, Theme::UI_PX * 0.82f);
		ImGui::BeginChild("gitstatus", ImVec2(0, 116), ImGuiChildFlags_Borders);
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_DIM));
		if (n == 0) ImGui::TextUnformatted("working tree clean");
		else ImGui::TextUnformatted(a.git_status.c_str());
		ImGui::PopStyleColor();
		ImGui::EndChild();
		ImGui::PopFont();

		ImGui::SetNextItemWidth(-1);
		ImGui::InputTextWithHint("##msg", "commit message", &a.commit_msg);

		ImGui::BeginDisabled(a.task.Running() || n == 0);
		if (W::PrimaryButton("Commit all", ImVec2(150, 0))) {
			std::string msg = util::Trim(a.commit_msg);
			if (msg.empty()) msg = "site update: " + util::PrettyEpoch(time(nullptr));
			// `git add -A` then commit, in one shell so the second only runs
			// if the first did -- the same order deploy.sh uses.
			a.RunTask({"sh", "-c", "git add -A && git commit -m " + std::string("\"") +
			                           util::Replace(msg, "\"", "\\\"") + "\""});
			a.commit_msg.clear();
			a.git_status_at = 0;
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(a.task.Running());
		if (ImGui::Button("Push", ImVec2(110, 0))) {
			a.RunTask({"git", "push"});
			a.git_status_at = 0;
		}
		ImGui::SetItemTooltip("a push to main is what triggers the Pages deploy");
		ImGui::SameLine();
		if (ImGui::Button("Pull", ImVec2(110, 0))) {
			a.RunTask({"git", "pull", "--ff-only"});
			a.git_status_at = 0;
		}
		ImGui::SameLine();
		if (ImGui::Button("Log", ImVec2(90, 0)))
			a.RunTask({"git", "log", "--oneline", "-12", "--decorate"});
		ImGui::SameLine();
		if (ImGui::Button("Diff", ImVec2(90, 0))) a.RunTask({"git", "diff", "--stat"});
		ImGui::EndDisabled();
	}

	// ---- deploy ------------------------------------------------------------
	W::SectionHeader("GitHub Pages");
	ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
	ImGui::TextWrapped("The `Build and deploy` workflow runs on every push to main and publishes to Pages.");
	ImGui::PopStyleColor();
	if (!a.have_gh) {
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
		ImGui::TextWrapped("The gh CLI is not on PATH, so run status has to be checked in a browser.");
		ImGui::PopStyleColor();
	} else {
		ImGui::BeginDisabled(a.task.Running());
		if (ImGui::Button("Check runs", ImVec2(150, 0)))
			a.RunTask({"gh", "run", "list", "--limit", "8"});
		ImGui::SameLine();
		if (ImGui::Button("Watch latest", ImVec2(150, 0)))
			a.RunTask({"sh", "-c", "gh run watch $(gh run list --limit 1 --json databaseId "
			                       "--jq '.[0].databaseId') --exit-status"});
		ImGui::SameLine();
		if (ImGui::Button("Re-run deploy", ImVec2(150, 0)))
			a.RunTask({"gh", "workflow", "run", "Build and deploy"});
		ImGui::SetItemTooltip("dispatches the workflow without needing a new commit");
		ImGui::EndDisabled();
	}
	std::string base = a.BaseURL();
	if (!base.empty()) {
		if (ImGui::Button("Open the live site", ImVec2(180, 0))) util::OpenURL(base);
		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_Text, Theme::V4(Theme::TEXT_FAINT));
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(base.c_str());
		ImGui::PopStyleColor();
	}

	ImGui::Dummy(ImVec2(0, 20));
	ImGui::EndChild();

	// ---- log ---------------------------------------------------------------
	ImGui::SameLine(0, 10);
	ImGui::BeginChild("logside", ImVec2(0, 0), ImGuiChildFlags_None);
	ImGui::PushFont(Theme::F_BOLD, Theme::UI_PX * 0.9f);
	ImGui::TextUnformatted(a.task.Running() ? "Running..." : "Last job");
	ImGui::PopFont();
	// SameLine only when there is really a button to put there -- calling it
	// unconditionally left the cursor mid-line and pushed the log well below
	// off to the right.
	if (a.task.Running()) {
		ImGui::SameLine();
		if (W::DangerButton("Stop")) a.task.Stop();
	} else if (!a.task.lines.empty()) {
		ImGui::SameLine();
		if (ImGui::SmallButton("clear")) a.task.Clear();
	}
	W::LogWell("tasklog", a.task.Tail(600), ImVec2(0, ImGui::GetContentRegionAvail().y * 0.55f));

	ImGui::PushFont(Theme::F_BOLD, Theme::UI_PX * 0.9f);
	ImGui::TextUnformatted(a.server.Running() ? "hugo server (running)" : "hugo server (stopped)");
	ImGui::PopFont();
	W::LogWell("serverlog", a.server.Tail(400), ImVec2(0, ImGui::GetContentRegionAvail().y - 6));
	ImGui::EndChild();
}
