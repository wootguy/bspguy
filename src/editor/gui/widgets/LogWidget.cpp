#include "Widget.h"

void LogWidget::draw() {
	g_log_mutex.lock();
	for (int i = 0; i < g_log_buffer.size(); i++) {
		addLog(g_log_buffer[i]);
	}
	g_log_buffer.clear();
	g_log_mutex.unlock();

	static int i = 0;

	ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

	bool copy = false;
	bool toggledAutoScroll = false;
	if (ImGui::BeginPopupContextWindow())
	{
		if (ImGui::MenuItem("Copy")) {
			copy = true;
		}
		if (ImGui::MenuItem("Clear")) {
			clearLog();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Auto-scroll", NULL, &AutoScroll)) {
			toggledAutoScroll = true;
		}
		if (ImGui::MenuItem("Verbose Logging", "", g_verbose)) {
			g_verbose = !g_verbose;
		}
		ImGui::EndPopup();
	}

	ImGui::PushFont(gui->consoleFont, g_font_scale_base * 0.9f);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
	const char* buf = Buf.begin();
	const char* buf_end = Buf.end();

	if (copy) {
		ImGui::LogBegin(ImGuiLogFlags_OutputClipboard, 0);
		ImGui::LogText("%s", Buf.begin());
		ImGui::LogFinish();
	}

	ImGuiListClipper clipper;
	clipper.Begin(LineOffsets.Size);
	while (clipper.Step())
	{
		for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++)
		{
			const char* line_start = buf + LineOffsets[line_no];
			const char* line_end = (line_no + 1 < LineOffsets.Size) ? (buf + LineOffsets[line_no + 1] - 1) : buf_end;
			
			ImGui::PushStyleColor(ImGuiCol_Text, LineColors[line_no]);
			ImGui::TextUnformatted(line_start, line_end);
			ImGui::PopStyleColor();
		}
	}
	clipper.End();

	ImGui::PopFont();
	ImGui::PopStyleVar();

	if (AutoScroll && (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() || toggledAutoScroll))
		ImGui::SetScrollHereY(1.0f);

	ImGui::EndChild();
}

void LogWidget::clearLog()
{
	Buf.clear();
	LineOffsets.clear();
	LineOffsets.push_back(0);

	LineColors.clear();
	LineColors.push_back(ImVec4(1,1,1,1));

	g_log_mutex.lock();
	g_log_buffer.clear();
	g_log_mutex.unlock();
}

void LogWidget::addLog(LogEntry& entry)
{
	ImVec4 color = ImVec4(1, 1, 1, 1);
	switch (entry.type) {
	case LOG_LEVEL_DEBUG:
		color = ImVec4(0, 0.5f, 0, 1);
		break;
	case LOG_LEVEL_WARN:
		color = ImVec4(1.0f, 1.05, 0, 1);
		break;
	case LOG_LEVEL_ERROR:
		color = ImVec4(1.0f, 0, 0, 1);
		break;
	default:
		break;
	}

	if (!LineColors.empty())
		LineColors.back() = color;
	
	int old_size = Buf.size();
	Buf.append(entry.msg.c_str());

	for (int new_size = Buf.size(); old_size < new_size; old_size++) {
		if (Buf[old_size] == '\n') {
			LineOffsets.push_back(old_size + 1);
			LineColors.push_back(color);
		}
	}
}

int LogWidget::calcMemoryUsage() {
	return sizeof(LogWidget) + Buf.size()
		+ LineOffsets.size()*sizeof(int) + LineColors.size()*sizeof(ImVec4);
}