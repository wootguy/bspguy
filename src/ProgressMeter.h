#pragma once
#include <chrono>
#include <ctime>

class ProgressMeter {
public:
	bool simpleMode = false;
	bool hide = false;

	ProgressMeter();

	// set a new title for the progress meter and set the number of ticks needed to reach 100%
	void update(const char* newTitle, int totalProgressTicks);

	// increment progress counter and print current status
	void tick();

	// backspace the progress meter until the line is blank
	void clear();

private:
	std::chrono::system_clock::time_point last_progress;
	const char* progress_title;
	const char* last_progress_title;
	int progress;
	int progress_total;
};