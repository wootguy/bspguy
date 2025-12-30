#include "Widget.h"

void Widget::tooltip(const char* text, float hoverDelay) {
	if (ImGui::IsItemHovered() && g.HoveredIdTimer > hoverDelay) {
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(min(ImGui::GetFontSize() * 35.0f, (float)g_app->windowWidth));
		ImGui::TextUnformatted(text);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}