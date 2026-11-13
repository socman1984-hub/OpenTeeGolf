// The C++ side of the real-course check (course_check.mjs): the course from an export's play data
// (as course_check.mjs writes it out: course.bin, trees.txt) and the shots in shots.txt, through the
// game's PlayCourse and ball physics; prints each result.

#include "Physics/OTGBallPhysics.h"
#include "Physics/OTGPlayCourse.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace otg;

struct Lcg
{
	uint32_t s;
	double operator()() { s = s * 1664525u + 1013904223u; return s / 4294967296.0; }
};

int main(int argc, char** argv)
{
	const std::string dir = argc > 1 ? argv[1] : ".";
	PlayCourse c;
	{
		std::ifstream f(dir + "/course.bin", std::ios::binary);
		double head[15];
		f.read(reinterpret_cast<char*>(head), sizeof head);
		c.N = static_cast<int>(head[0]); c.x0 = head[1]; c.z1 = head[2]; c.step = head[3]; c.base = head[4]; c.range = head[5];
		c.w = static_cast<int>(head[6]); c.h = static_cast<int>(head[7]); c.sMinX = head[8]; c.sMaxZ = head[9]; c.res = head[10];
		c.minX = head[11]; c.maxX = head[12]; c.minZ = head[13]; c.maxZ = head[14];
		c.heights.resize(static_cast<size_t>(c.N) * c.N);
		f.read(reinterpret_cast<char*>(c.heights.data()), c.heights.size() * 2);
		c.surfaces.resize(static_cast<size_t>(c.w) * c.h);
		f.read(reinterpret_cast<char*>(c.surfaces.data()), c.surfaces.size());
	}
	PlayTrees trees;
	{
		std::ifstream f(dir + "/trees.txt");
		Tree t;
		int cone;
		while (f >> t.x >> t.z >> t.trunkTop >> t.trunkR >> t.kind >> cone)
		{
			t.cone = cone != 0;
			if (t.cone) f >> t.coneBase >> t.coneH >> t.coneR; else f >> t.cy >> t.rh >> t.rv;
			if (t.kind == "-") t.kind.clear();
			trees.all.push_back(t);
		}
		trees.Build();
	}
	std::ifstream in(dir + "/shots.txt");
	std::string line;
	while (std::getline(in, line))
	{
		if (line.empty()) continue;
		std::istringstream ss(line);
		int id, hasPin, withTrees;
		unsigned seed;
		Shot shot;
		ShotOptions o;
		V3 origin;
		ss >> id >> origin.x >> origin.y >> origin.z >> shot.speedMph >> shot.vla >> shot.hla >> shot.backSpin >> shot.sideSpin
			>> o.heading >> o.stimp >> o.altitudeM >> hasPin >> o.pin.x >> o.pin.y >> o.pin.z >> seed >> withTrees;
		o.hasPin = hasPin != 0;
		Lcg lcg{ seed };
		o.random = [&lcg]() { return lcg(); };
		if (withTrees) o.trees = &trees;
		const ShotResult r = SimulateShot(c, origin, shot, o);
		std::string hits;
		for (const std::string& hh : r.treeHits) hits += (hits.empty() ? "" : ",") + hh;
		std::printf("%d %s %.6f %.6f %.6f %.6f %.6f %s\n", id, r.result.c_str(), r.rest.x, r.rest.y, r.rest.z, r.carry, r.total, hits.empty() ? "-" : hits.c_str());
	}
	return 0;
}
