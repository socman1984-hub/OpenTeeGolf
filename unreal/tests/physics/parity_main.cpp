// The C++ side of the physics parity test (parity.mjs): plays every shot in shots.txt on the same
// formula courses as parity.mjs, with the same seeded random numbers, and prints the results.
// Built by parity.mjs with MSVC; the physics is the game's own Source/.../OTGBallPhysics.cpp.

#include "Physics/OTGBallPhysics.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace otg;

// ---- the test courses (keep in step with parity.mjs)
struct TestCourse : Course
{
	bool hills = false;
	double Height(double x, double z) const { return hills ? 3 * std::sin(x / 37) + 2 * std::cos(z / 23) + 0.04 * z : 0; }
	double HeightAt(double x, double z) const override
	{
		if (std::abs(x) > 400 || z < -50 || z > 500) return std::nan("");
		return Height(x, z);
	}
	V3 NormalAt(double x, double z) const override
	{
		if (!hills) return V3{ 0, 1, 0 };
		const double dx = 3 * std::cos(x / 37) / 37, dz = -2 * std::sin(z / 23) / 23 + 0.04;
		const double l = std::sqrt(dx * dx + 1 + dz * dz);
		return V3{ -dx / l, 1 / l, -dz / l };
	}
	Surface SurfaceAt(double x, double z) const override
	{
		if (std::abs(x) > 400 || z < -50 || z > 500) return Surface::None;
		if (z >= 180 && z <= 195 && x >= -400 && x <= -25) return Surface::Water;
		if (std::hypot(x - 15, z - 240) < 6) return Surface::Sand;
		const double g = std::hypot(x, z - 260);
		if (g < 16) return Surface::Green;
		if (g < 18) return Surface::Fringe;
		if (z < 5 && std::abs(x) < 4) return Surface::Tee;
		if (std::abs(x) < 22 && z < 250) return Surface::Fairway;
		return Surface::Rough;
	}
};

// ---- three trees (keep in step with parity.mjs)
struct TestTrees : Trees
{
	std::vector<Tree> list;
	explicit TestTrees(const TestCourse& c)
	{
		auto g = [&c](double x, double z) { return c.Height(x, z); };
		Tree a; a.x = 12; a.z = 120; a.trunkTop = g(12, 120) + 3; a.trunkR = 0.25; a.rh = 5; a.rv = 4; a.cy = g(12, 120) + 7; a.kind = "oak";
		Tree b; b.x = -10; b.z = 90; b.trunkTop = g(-10, 90) + 2; b.trunkR = 0.2; b.cone = true; b.coneBase = g(-10, 90) + 2; b.coneH = 12; b.coneR = 3.5; b.kind = "spruce";
		Tree p; p.x = 3; p.z = 60; p.trunkTop = g(3, 60) + 9; p.trunkR = 0.18; p.rh = 3; p.rv = 1.5; p.cy = g(3, 60) + 10; p.kind = "palm";
		list = { a, b, p };
	}
	void Near(double x, double z, std::vector<const Tree*>& out) const override
	{
		for (const Tree& t : list) if (std::hypot(t.x - x, t.z - z) < 10) out.push_back(&t);
	}
};

// ---- the same random numbers as parity.mjs (a 32-bit LCG)
struct Lcg
{
	uint32_t s;
	double operator()() { s = s * 1664525u + 1013904223u; return s / 4294967296.0; }
};

int main(int argc, char** argv)
{
	std::ifstream in(argc > 1 ? argv[1] : "shots.txt");
	std::string line;
	TestCourse flat, hills;
	hills.hills = true;
	TestTrees flatTrees(flat), hillTrees(hills);
	while (std::getline(in, line))
	{
		if (line.empty() || line[0] == '#') continue;
		std::istringstream ss(line);
		int id, courseKind, hasPin, withTrees = 0;
		unsigned seed;
		Shot shot;
		ShotOptions o;
		V3 origin;
		ss >> id >> courseKind >> origin.x >> origin.y >> origin.z >> shot.speedMph >> shot.vla >> shot.hla >> shot.backSpin >> shot.sideSpin
			>> o.heading >> o.windMps >> o.windFromDeg >> o.wet >> o.stimp >> o.altitudeM >> o.tempC >> hasPin >> o.pin.x >> o.pin.y >> o.pin.z >> seed >> withTrees;
		o.hasPin = hasPin != 0;
		Lcg lcg{ seed };
		o.random = [&lcg]() { return lcg(); };
		if (withTrees) o.trees = courseKind ? &hillTrees : &flatTrees;
		const Course& c = courseKind ? static_cast<const Course&>(hills) : static_cast<const Course&>(flat);
		const ShotResult r = SimulateShot(c, origin, shot, o);
		// (a few points along the way: a quarter, half and three quarters through)
		const size_t n = r.points.size();
		std::printf("%d %s %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %zu", id, r.result.c_str(), r.rest.x, r.rest.y, r.rest.z,
			r.carry, r.total, r.apex, r.flightTime, r.duration, n);
		for (int k = 1; k <= 3; k++)
		{
			const PathPoint& q = r.points[n * k / 4];
			std::printf(" %.6f %.6f %.6f", q.x, q.y, q.z);
		}
		std::string hits;
		for (const std::string& h : r.treeHits) hits += (hits.empty() ? "" : ",") + h;
		std::printf(" %s\n", hits.empty() ? "-" : hits.c_str());
	}
	return 0;
}
