#include "session_stats.h"

#include <godot_cpp/core/math.hpp>
#include <cmath>

using namespace godot;

static float series_peak(const Vector<float> &v) {
	if(v.is_empty()) {
		return 100.0f;
	}
	float r = v[0];
	for(int i = 1; i < v.size(); i++) {
		r = MAX(r, v[i]);
	}
	return r;
}

static float series_min(const Vector<float> &v) {
	if(v.is_empty()) {
		return 100.0f;
	}
	float r = v[0];
	for(int i = 1; i < v.size(); i++) {
		r = MIN(r, v[i]);
	}
	return r;
}

static float series_avg(const Vector<float> &v) {
	if(v.is_empty()) {
		return 0.0f;
	}
	double s = 0.0;
	for(int i = 0; i < v.size(); i++) {
		s += v[i];
	}
	return (float)(s / (double)v.size());
}

void SessionStats::reset() {
	total_interactions = 0;
	hits = 0;
	misses = 0;
	bare_shots = 0;
	hits_gray = 0;
	hits_white = 0;
	score_total = 0;
	score_gray = 0;
	score_white = 0;
	hold_deviations.clear();
	shot_times.clear();
	miss_records.clear();
	accuracy_series.clear();
	current_accuracy = 100.0f;
}

// One shot, one entry in the accuracy series. A shot into open space is a miss
// like any other: `miss_detail` only decides how it is described in the results
// list, never whether it counts.
void SessionStats::record_interaction(bool hit, const MissRecord *miss_detail) {
	total_interactions++;
	if(hit) {
		hits++;
	} else {
		misses++;
		if(miss_detail != nullptr) {
			miss_records.push_back(*miss_detail);
			if(miss_detail->nearest_distance == MissRecord::NO_TARGET) {
				bare_shots++;
			}
		}
	}
	current_accuracy = total_interactions > 0 ? (100.0f * (float)hits / (float)total_interactions) : 100.0f;
	accuracy_series.push_back(current_accuracy);
}

void SessionStats::record_hold(const HoldDeviation &d) {
	hold_deviations.push_back(d);
}

void SessionStats::record_miss(const MissRecord &m) {
	miss_records.push_back(m);
}

// Inter-shot intervals. The first shot has no predecessor, so it contributes no
// interval; every later shot adds one sample.
static void shot_intervals(const Vector<float> &v, Vector<float> &out) {
	out.clear();
	for(int i = 1; i < v.size(); i++) {
		const float gap = v[i] - v[i - 1];
		if(gap > 0.0f) {
			out.push_back(gap);
		}
	}
}

float SessionStats::shot_interval_avg() const {
	Vector<float> iv;
	shot_intervals(shot_times, iv);
	return series_avg(iv);
}

float SessionStats::shot_interval_min() const {
	Vector<float> iv;
	shot_intervals(shot_times, iv);
	return series_min(iv);
}

float SessionStats::shot_interval_max() const {
	Vector<float> iv;
	shot_intervals(shot_times, iv);
	return series_peak(iv);
}

float SessionStats::shot_interval_std() const {
	Vector<float> iv;
	shot_intervals(shot_times, iv);
	if(iv.is_empty()) {
		return 0.0f;
	}
	const float avg = series_avg(iv);
	double acc = 0.0;
	for(int i = 0; i < iv.size(); i++) {
		const double d = (double)iv[i] - (double)avg;
		acc += d * d;
	}
	return (float)std::sqrt(acc / (double)iv.size());
}

float SessionStats::accuracy_peak() const { return series_peak(accuracy_series); }
float SessionStats::accuracy_average() const { return series_avg(accuracy_series); }
float SessionStats::accuracy_min() const { return series_min(accuracy_series); }

// Absolute deviation from the required hold, so early and late releases are both
// reported as positive numbers.
float SessionStats::hold_dev_max() const {
	if(hold_deviations.is_empty()) {
		return 0.0f;
	}
	float r = -1e9f;
	for(int i = 0; i < hold_deviations.size(); i++) {
		r = MAX(r, std::fabs(hold_deviations[i].deviation));
	}
	return r;
}

float SessionStats::hold_dev_avg() const {
	if(hold_deviations.is_empty()) {
		return 0.0f;
	}
	double s = 0.0;
	for(int i = 0; i < hold_deviations.size(); i++) {
		s += std::fabs(hold_deviations[i].deviation);
	}
	return (float)(s / (double)hold_deviations.size());
}

float SessionStats::hold_dev_min() const {
	if(hold_deviations.is_empty()) {
		return 0.0f;
	}
	float r = 1e9f;
	for(int i = 0; i < hold_deviations.size(); i++) {
		r = MIN(r, std::fabs(hold_deviations[i].deviation));
	}
	return r;
}
