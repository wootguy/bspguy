#include "Widget.h"

void AboutWidget::draw() {
	ImGui::InputText("Version", (char*)g_version_string, strlen(g_version_string) + 1, ImGuiInputTextFlags_ReadOnly);

	static char* author = "w00tguy";
	ImGui::InputText("Author", author, strlen(author) + 1, ImGuiInputTextFlags_ReadOnly);

	static char* url = "https://github.com/wootguy/bspguy";
	ImGui::InputText("Contact", url, strlen(url) + 1, ImGuiInputTextFlags_ReadOnly);
}