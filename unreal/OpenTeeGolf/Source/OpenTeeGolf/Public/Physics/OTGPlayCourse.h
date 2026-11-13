// A course to play, from the play data the web version exports for Unreal (public/js/export-unreal.js):
// the ground from the export's heightmap (the ground Unreal draws), the surfaces as the game's own
// surfaceAt has them, every half metre, and the trees' shapes for the ball. Plain C++ like
// OTGBallPhysics: the game fills it from a course pack, the tests from the export's files.
//
// Course space as in the game: x east, z north, y up, metres.

#pragma once

#include "Physics/OTGBallPhysics.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace otg
{
	struct PlayCourse : Course
	{
		// The export's heightmap: N x N samples, sample (i, j) at x0 + i * step, z1 - j * step, value v
		// at base + v / 65535 * range metres
		int N = 0;
		double x0 = 0, z1 = 0, step = 1, base = 0, range = 1;
		std::vector<uint16_t> heights;
		// The surfaces: w x h codes (otg::Surface), cell (i, j) from minX + i * res, maxZ - j * res
		int w = 0, h = 0;
		double sMinX = 0, sMaxZ = 0, res = 0.5;
		std::vector<uint8_t> surfaces;
		// Where the course is: outside, there is no ground (the ball is out)
		double minX = 0, maxX = 0, minZ = 0, maxZ = 0;

		double HeightAt(double x, double z) const override;
		V3 NormalAt(double x, double z) const override;
		Surface SurfaceAt(double x, double z) const override;
	};

	// A building or parked car: its footprint (x, z) and height above the ground
	struct Solid
	{
		double h = 0;
		std::string kind;    // building (or another physics OBSTACLES kind)
		std::vector<std::pair<double, double>> pts;
	};

	// What the ball can hit, as the web game's collision world (collision.js, Jolt) has it: the ground
	// (the heightmap), tree trunks (cylinders reaching a metre into the ground), buildings and cars
	// (prisms on their footprint's convex outline) and the flagstick, every one grown by the ball's
	// radius so a ray along the ball's centre acts as the ball. Obstacles are bucketed on a 10 m grid.
	// (OTGPlayCollider.cpp)
	struct PlayCollider : Collider
	{
		void Build(const PlayCourse& course, const std::vector<Tree>& trees, const std::vector<Solid>& solids);
		void SetPin(const V3* pin) override;
		bool Cast(const V3& from, const V3& to, bool obstacleOnly, Hit& out) const override;
		size_t Count() const { return obstacles.size(); }

	private:
		struct Obstacle
		{
			std::string kind;
			bool cylinder = true;
			double x = 0, z = 0, r = 0;                      // (cylinder)
			std::vector<std::pair<double, double>> hull;     // (prism: convex outline, counter-clockwise)
			double y0 = 0, y1 = 0;
			double minX = 0, minZ = 0, maxX = 0, maxZ = 0;
		};
		bool CastOne(const Obstacle& o, const V3& a, const V3& b, double& t, V3& n) const;
		bool CastGround(const V3& a, const V3& b, double& t) const;
		void Add(const Obstacle& o);

		const PlayCourse* course = nullptr;
		std::vector<Obstacle> obstacles;
		std::unordered_map<int64_t, std::vector<int>> grid;
		double cell = 10;
		bool hasPole = false;
		Obstacle pole;
		static int64_t Key(int64_t i, int64_t j) { return i * 100003 + j; }
	};

	// The course's trees, bucketed on a 10 m grid (as the game's TreeIndex: each tree in every cell
	// its crown reaches)
	struct PlayTrees : Trees
	{
		std::vector<Tree> all;
		void Build(double cell = 10);
		void Near(double x, double z, std::vector<const Tree*>& out) const override;

	private:
		double cell = 10;
		std::unordered_map<int64_t, std::vector<int>> map;
		static int64_t Key(int64_t i, int64_t j) { return i * 100003 + j; }
	};
}
