#pragma once

// Peppy's silhouette: a round gray head with two tall ears, a cream muzzle and
// buck teeth. Units of D (the head's width) from the box's bottom-left, y up -
// the mascot's layer space. The outline (head + both ears at rest, without the
// seams where they overlap) is what the ripples that radiate from Peppy trace.

#include <cmath>
#include <vector>

namespace peppy {

// Head
const double kHeadCX = 0.5, kHeadCY = 0.31, kHeadRX = 0.44, kHeadRY = 0.31;
// Eyes: 0.5 +/- kEyeX, kEyeY
const double kEyeX = 0.16, kEyeY = 0.40;
// Ears: each pivots on its base (0.5 +/- kEarX, kEarY); the ear is an ellipse
// kEarRX x kEarRY whose center sits kEarReach up its own axis from the base.
const double kEarX = 0.15, kEarY = 0.5, kEarRX = 0.095, kEarRY = 0.27, kEarReach = 0.25;
const double kInnerRX = 0.05, kInnerRY = 0.2, kInnerReach = 0.27;
const double kEarRest = 0.16;   // radians each ear leans outward when awake
// Muzzle, nose, teeth
const double kMuzzleCX = 0.5, kMuzzleCY = 0.19, kMuzzleRX = 0.25, kMuzzleRY = 0.14;
const double kNoseCY = 0.29, kNoseRX = 0.05, kNoseRY = 0.032;
const double kToothW = 0.042, kToothTop = 0.17, kToothBottom = 0.095, kToothGap = 0.006;

struct RabbitPoint { double x, y; };

struct Oval {
	double cx, cy, rx, ry, angle;
	RabbitPoint At(double t) const
	{
		double x = rx * std::cos(t), y = ry * std::sin(t), c = std::cos(angle), s = std::sin(angle);
		return {cx + x * c - y * s, cy + x * s + y * c};
	}
	bool Inside(RabbitPoint p) const
	{
		double dx = p.x - cx, dy = p.y - cy, c = std::cos(-angle), s = std::sin(-angle);
		double u = (dx * c - dy * s) / rx, v = (dx * s + dy * c) / ry;
		return u * u + v * v < 1;
	}
};

// side: -1 left ear, +1 right. lean: radians outward.
inline Oval EarOval(double side, double lean = kEarRest)
{
	double a = -side * lean;   // the left ear leans left: counterclockwise
	return {0.5 + side * kEarX - std::sin(a) * kEarReach, kEarY + std::cos(a) * kEarReach, kEarRX, kEarRY, a};
}

inline std::vector<RabbitPoint> RabbitOutline()
{
	const double kTwoPi = 6.28318530717958647692;
	std::vector<Oval> shapes = {{kHeadCX, kHeadCY, kHeadRX, kHeadRY, 0}, EarOval(-1), EarOval(1)};

	// Each shape's edge, counterclockwise, split into the runs outside the others.
	std::vector<std::vector<RabbitPoint>> runs;
	for (size_t k = 0; k < shapes.size(); k++) {
		const int n = 720;
		std::vector<RabbitPoint> ring(n);
		std::vector<bool> keep(n);
		for (int i = 0; i < n; i++) {
			ring[i] = shapes[k].At(kTwoPi * i / n);
			keep[i] = true;
			for (size_t j = 0; j < shapes.size(); j++)
				if (j != k && shapes[j].Inside(ring[i])) keep[i] = false;
		}
		int start = -1;
		for (int i = 0; i < n; i++) if (!keep[i] && keep[(i + 1) % n]) { start = (i + 1) % n; break; }
		if (start < 0) { if (keep[0]) runs.push_back(ring); continue; }
		for (int m = 0; m < n; m++) {
			int i = (start + m) % n;
			if (!keep[i]) continue;
			if (m == 0 || !keep[(i + n - 1) % n]) runs.push_back({});
			runs.back().push_back(ring[i]);
		}
	}

	// Chain the runs end to start into one loop.
	std::vector<RabbitPoint> out;
	if (runs.empty()) return out;
	std::vector<bool> used(runs.size());
	size_t cur = 0;
	for (size_t n = 0; n < runs.size(); n++) {
		used[cur] = true;
		out.insert(out.end(), runs[cur].begin(), runs[cur].end());
		RabbitPoint end = out.back();
		double best = 1e9;
		size_t next = cur;
		for (size_t j = 0; j < runs.size(); j++) {
			if (used[j]) continue;
			double d = std::hypot(runs[j].front().x - end.x, runs[j].front().y - end.y);
			if (d < best) { best = d; next = j; }
		}
		if (next == cur) break;
		cur = next;
	}
	return out;
}

// Where the ripples grow from: the middle of the silhouette.
const double kRabbitMidX = 0.5, kRabbitMidY = 0.48;
const double kRabbitHeight = 1.02;   // ear tips, in D

} // namespace peppy
