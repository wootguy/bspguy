#include "ProgressMeter.h"
#include <string.h>

ProgressMeter::ProgressMeter() {
	progress_total = progress = 0;
	last_progress_title = progress_title = "";
}

void ProgressMeter::update(const char* newTitle, int totalProgressTicks) {
	progress_title = newTitle;
	progress = 0;
	progress_total = totalProgressTicks;
}

void ProgressMeter::tick() {
	if (progress_title[0] == '\0') {
		return;
	}
	if (progress++ > 0) {
		auto now = std::chrono::system_clock::now();
		std::chrono::duration<double> delta = now - last_progress;
		if (delta.count() < 0.016) {
			return;
		}
		last_progress = now;
	}

	int percent = (progress / (float)progress_total) * 100;

	for (int i = 0; i < 12; i++) printf("\b\b\b\b");
	printf("        %-32s %2d%%", progress_title, percent);
}

void ProgressMeter::clear() {
	// 50 chars
	for (int i = 0; i < 5; i++) printf("\b\b\b\b\b\b\b\b\b\b");
	for (int i = 0; i < 5; i++) printf("          ");
	for (int i = 0; i < 5; i++) printf("\b\b\b\b\b\b\b\b\b\b");
}