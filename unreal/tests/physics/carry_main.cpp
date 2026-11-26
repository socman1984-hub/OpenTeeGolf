// The game's ball flight against OpenFairway's reference launches (uopengolf's Docs/Accuracy.md): each shot
// on a flat fairway with no wind, in its reference's air, with its carry, apex, landing angle, roll and total.
// Built and run by carry_check.mjs with MSVC; the physics is the game's own OTGBallPhysics.cpp.

#include "Physics/OTGBallPhysics.h"

#include <cmath>
#include <cstdio>

using namespace otg;

struct Flat : Course
{
	double HeightAt(double, double) const override { return 0; }
	V3 NormalAt(double, double) const override { return V3{ 0, 1, 0 }; }
	Surface SurfaceAt(double, double) const override { return Surface::Fairway; }
};

struct Ref { const char* src; double mph, vla, rpm, carryYd, apexFt; double tempC, humidity, pressureHpa; };

int main()
{
	// fs: a trajectory calculator, 77 F, 50% humidity, 14.69 psi. tm: tour averages (75 F at sea level, dry: the
	// plugin's default air). lm: a launch monitor session (75 F at sea level).
	const Ref refs[] = {
		{ "fs", 175, 10.0, 2200, 299.7, 96.8, 25, 0.5, 1012.8 },
		{ "fs", 167, 10.9, 2686, 283.1, 106.5, 25, 0.5, 1012.8 },
		{ "fs", 150, 12.5, 2335, 249.7, 85.5, 25, 0.5, 1012.8 },
		{ "fs", 109.3, 16.7, 5072, 154.7, 71.6, 25, 0.5, 1012.8 },
		{ "tm", 167, 10.9, 2686, 275, 96, 23.9, 0, 0 },
		{ "tm", 140, 13.2, 2611, 218, 75, 23.9, 0, 0 },
		{ "tm", 132, 12.1, 5361, 194, 93, 23.9, 0, 0 },
		{ "lm", 109.3, 16.7, 5072, 157.0, 73.3, 23.9, 0, 0 },
		// (no reference: a wedge, a chip, a putt-speed roll and a topped shot, for their roll)
		{ "--", 75, 30, 9000, 0, 0, 23.9, 0, 0 },
		{ "--", 35, 35, 5000, 0, 0, 23.9, 0, 0 },
		{ "--", 60, -2, 3000, 0, 0, 23.9, 0, 0 },
	};
	Flat course;
	std::printf("%-3s %6s %5s %5s | %7s %7s %6s | %6s %6s %6s | %6s %6s\n", "src", "mph", "vla", "rpm", "carry", "ref", "diff", "apex", "ref", "diff", "roll", "total");
	for (const Ref& r : refs)
	{
		Shot s;
		s.speedMph = r.mph; s.vla = r.vla; s.backSpin = r.rpm;
		ShotOptions o;
		o.tempC = r.tempC; o.humidity = r.humidity; o.pressureHpa = r.pressureHpa;
		const ShotResult res = SimulateShot(course, V3{ 0, 0, 0 }, s, o);
		const double yd = 1.0936133, ft = 3.2808399;
		const double carry = res.carry * yd, apex = res.apex * ft, total = res.total * yd;
		if (r.carryYd > 0)
			std::printf("%-3s %6.1f %5.1f %5.0f | %7.1f %7.1f %+6.1f | %6.1f %6.1f %+6.1f | %6.1f %6.1f\n", r.src, r.mph, r.vla, r.rpm,
				carry, r.carryYd, carry - r.carryYd, apex, r.apexFt, apex - r.apexFt, total - carry, total);
		else
			std::printf("%-3s %6.1f %5.1f %5.0f | %7.1f %7s %6s | %6.1f %6s %6s | %6.1f %6.1f\n", r.src, r.mph, r.vla, r.rpm, carry, "", "", apex, "", "", total - carry, total);
	}
	return 0;
}
