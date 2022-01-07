#include "ProgressMeter.h"
#include <string>
#include <stdio.h> 
#include "util.h"

ProgressMeter::ProgressMeter() {
	progress_total = progress = 0;
	last_progress_title = progress_title = "";
}

void ProgressMeter::update(const char* newTitle, int totalProgressTicks) {
	progress_title = newTitle;
	progress = 0;
	progress_total = totalProgressTicks;
	if (simpleMode && !hide) {
		logf((std::string(newTitle) + "\n").c_str());
	}
}

void ProgressMeter::tick() {
	if (progress_title[0] == '\0' || simpleMode || hide) {
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

	float percent = (progress / (float)progress_total) * 100;

	for (int i = 0; i < 12; i++) logf("\b\b\b\b");
	logf("        %-32s %.0f%%", progress_title, percent);
}

void ProgressMeter::clear() {
	if (simpleMode || hide) {
		return;
	}
	// 50 chars
	for (int i = 0; i < 6; i++) logf("\b\b\b\b\b\b\b\b\b\b");
	for (int i = 0; i < 6; i++) logf("          ");
	for (int i = 0; i < 6; i++) logf("\b\b\b\b\b\b\b\b\b\b");
}