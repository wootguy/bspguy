#include "Widget.h"

void HelpWidget::draw() {
	if (ImGui::BeginTabBar("##tabs"))
	{
		if (ImGui::BeginTabItem("UI Controls")) {
			ImGui::Dummy(ImVec2(0, 10));

			// user guide from the demo
			ImGuiIO& io = ImGui::GetIO();
			ImGui::BulletText("Click and drag on lower corner to resize window\n(double-click to auto fit window to its contents).");
			ImGui::BulletText("While adjusting numeric inputs:\n");
			ImGui::Indent();
			ImGui::BulletText("Hold SHIFT/ALT for faster/slower edit.");
			ImGui::BulletText("Double-click or CTRL+click to input value.");
			ImGui::Unindent();
			ImGui::BulletText("While inputing text:\n");
			ImGui::Indent();
			ImGui::BulletText("CTRL+A or double-click to select all.");
			ImGui::BulletText("CTRL+X/C/V to use clipboard cut/copy/paste.");
			ImGui::BulletText("CTRL+Z,CTRL+Y to undo/redo.");
			ImGui::BulletText("You can apply arithmetic operators +,*,/ on numerical values.\nUse +- to subtract.");
			ImGui::Unindent();

			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("3D Controls")) {
			ImGui::Dummy(ImVec2(0, 10));

			ImGuiIO& io = ImGui::GetIO();
			ImGui::BulletText("WASD to move (hold SHIFT/CTRL for faster/slower movement).");
			ImGui::BulletText("Hold right mouse button to rotate view.");
			ImGui::BulletText("Left click to select objects/entities. Right click for options.");
			ImGui::BulletText("Press Z to toggle mouse capture for camera rotation.");
			ImGui::BulletText("Press R to enable Preview Mode. This temporarily enables various \"View\"\n"
				"options to give you a better idea of how the map will look in-game.");
			ImGui::BulletText("While grabbing an entity (G):\n");
			ImGui::Indent();
			ImGui::BulletText("Mouse wheel to push/pull (hold SHIFT/CTRL for faster/slower).");
			ImGui::BulletText("Click outside of the entity or press G to let go.");
			ImGui::Unindent();
			ImGui::BulletText("While grabbing 3D transform axes:\n");
			ImGui::Indent();
			ImGui::BulletText("Hold SHIFT/CTRL for faster/slower adjustments");
			ImGui::Unindent();

			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Vertex Manipulation")) {
			ImGui::Dummy(ImVec2(0, 10));

			ImGuiIO& io = ImGui::GetIO();
			ImGui::BulletText("Press F to split a face while 2 edges are selected.");
			ImGui::Unindent();

			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Optimizing BSP data")) {
			ImGui::Dummy(ImVec2(0, 10));

			ImGuiIO& io = ImGui::GetIO();
			ImGui::TextWrapped("Optimizing BSP data is essential for merging and porting maps. "
				"The Optimize Tool tries to reduce every data type, but you may need to make manual edits "
				"if it doesn't remove enough. Below are tips on how to reduce the most problematic data types.\n\n");

			ImGui::BulletText("AllocBlock\n");
			ImGui::Indent();
			ImGui::BulletText("Downscale textures\n");
			ImGui::BulletText("Scale up textures\n");
			ImGui::Unindent();
			ImGui::BulletText("Clipnodes\n");
			ImGui::Indent();
			ImGui::BulletText("Redirect Hull 2 --> Hull 1\n");
			ImGui::Indent();
			ImGui::BulletText("You will need to address problems with large monster/pushables\n");
			ImGui::BulletText("Selectively simplify hulls per model (right click solid entities)\n");
			ImGui::Unindent();
			ImGui::Unindent();
			ImGui::BulletText("Models\n");
			ImGui::Indent();
			ImGui::BulletText("Deduplicate Models Tool\n");
			ImGui::BulletText("Merge BSP Models (select 2 solid entities)\n");
			ImGui::Unindent();
			ImGui::BulletText("Lightstyles\n");
			ImGui::Indent();
			ImGui::BulletText("Delete light entities which don't need to be toggled.\n");
			ImGui::Unindent();

			ImGui::TextWrapped("\nIn most cases you need to run the Clean command to remove data "
				"after a manual edit. The Map Limits widget has tabs for finding which "
				"entities/faces are contributing the most toward map limits.");

			ImGui::EndTabItem();
		}
	}
	ImGui::EndTabBar();
}
