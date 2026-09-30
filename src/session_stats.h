#pragma once

#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/templates/vector.hpp>

namespace godot {

struct HoldDeviation {
	float required = 0.0f;
	float actual = 0.0f;
	float deviation = 0.0f; // actual - required, seconds
};

struct MissRecord {
	Vector2 error_vector;
	float nearest_distance = 0.0f;
	float target_radius = 0.0f;
	float deviation_pct = 0.0f; // gap as % of target size
	// Sentinels for a miss with nothing near it: the shot went into empty space,
	// so there is no target to measure the error against.
	static constexpr float NO_TARGET = -1.0f;
	bool overshoot = false; // true = overshoot (cursor past the target), false = undershoot
	bool had_target_nearby = false;
};

class SessionStats {
public:
	// Every shot fired. Accuracy is hits over this, so a shot into open space
	// costs accuracy exactly like a shot that missed a target.
	int total_interactions = 0;
	int hits = 0;
	int misses = 0;
	// Shots that had nothing on the beam at all. They are a subset of `misses`;
	// kept separate only so the results screen can label them.
	int bare_shots = 0;
	int hits_gray = 0;
	int hits_white = 0;
	int score_total = 0;
	int score_gray = 0;
	int score_white = 0;

	Vector<HoldDeviation> hold_deviations;
	// Every shot, tagged with a target-local timestamp, for inter-shot timing.
	Vector<float> shot_times;
	Vector<MissRecord> miss_records;
	Vector<float> accuracy_series;

	float current_accuracy = 100.0f;

	// Records one shot. `miss_detail` is optional extra information for the miss
	// list (a bare shot passes a record tagged with NO_TARGET).
	void record_interaction(bool hit, const MissRecord *miss_detail = nullptr);
	void record_hold(const HoldDeviation &d);
	void record_miss(const MissRecord &m);
	void reset();

	float accuracy_peak() const;
	float accuracy_average() const;
	float accuracy_min() const;

	float hold_dev_max() const;
	float hold_dev_avg() const;
	float hold_dev_min() const;

	// Spread of the intervals between shots (seconds).
	float shot_interval_avg() const;
	float shot_interval_min() const;
	float shot_interval_max() const;
	float shot_interval_std() const;
};

} // namespace godot
