// See OTGPlayCourse.h (PlayCollider): the web game's collision world (public/js/collision.js) without
// Jolt - the same shapes, cast analytically.

#include "Physics/OTGPlayCourse.h"

#include <algorithm>
#include <cmath>

namespace otg
{
	namespace
	{
		constexpr double POLE_RADIUS = 0.0125, POLE_HEIGHT = 2.3;

		using P2 = std::pair<double, double>;

		// Convex hull (monotone chain), counter-clockwise in (x, z)
		std::vector<P2> Hull(std::vector<P2> p)
		{
			std::sort(p.begin(), p.end());
			p.erase(std::unique(p.begin(), p.end()), p.end());
			if (p.size() < 3) return p;
			auto cross = [](const P2& o, const P2& a, const P2& b) {
				return (a.first - o.first) * (b.second - o.second) - (a.second - o.second) * (b.first - o.first);
			};
			std::vector<P2> h(2 * p.size());
			size_t k = 0;
			for (size_t i = 0; i < p.size(); i++)
			{
				while (k >= 2 && cross(h[k - 2], h[k - 1], p[i]) <= 0) k--;
				h[k++] = p[i];
			}
			for (size_t i = p.size() - 1, t = k + 1; i > 0; i--)
			{
				while (k >= t && cross(h[k - 2], h[k - 1], p[i - 1]) <= 0) k--;
				h[k++] = p[i - 1];
			}
			h.resize(k - 1);
			return h;
		}
	}

	void PlayCollider::Add(const Obstacle& o)
	{
		const int idx = static_cast<int>(obstacles.size());
		obstacles.push_back(o);
		const int64_t i0 = static_cast<int64_t>(std::floor(o.minX / cell)), i1 = static_cast<int64_t>(std::floor(o.maxX / cell));
		const int64_t j0 = static_cast<int64_t>(std::floor(o.minZ / cell)), j1 = static_cast<int64_t>(std::floor(o.maxZ / cell));
		for (int64_t i = i0; i <= i1; i++)
			for (int64_t j = j0; j <= j1; j++) grid[Key(i, j)].push_back(idx);
	}

	void PlayCollider::Build(const PlayCourse& c, const std::vector<Tree>& trees, const std::vector<Solid>& solids)
	{
		course = &c;
		obstacles.clear();
		grid.clear();
		// Trunks reach a metre into the ground so a rolling ball meets them too
		for (const Tree& t : trees)
		{
			const double g = c.HeightAt(t.x, t.z);
			if (std::isnan(g) || t.trunkR <= 0) continue;
			Obstacle o;
			o.kind = "trunk";
			o.x = t.x; o.z = t.z; o.r = t.trunkR + BALL_RADIUS;
			o.y0 = g - 1; o.y1 = t.trunkTop;
			if (o.y1 <= o.y0) continue;
			o.minX = o.x - o.r; o.maxX = o.x + o.r; o.minZ = o.z - o.r; o.maxZ = o.z + o.r;
			Add(o);
		}
		// Buildings: the convex outline of the footprint, from half a metre below the lowest ground
		// under it to its height above that (grown by the ball's radius up and down, as the game's)
		for (const Solid& s : solids)
		{
			if (s.pts.size() < 3) continue;
			double ground = 1e300;
			for (const P2& q : s.pts)
			{
				const double y = c.HeightAt(q.first, q.second);
				if (!std::isnan(y)) ground = std::min(ground, y);
			}
			if (ground > 1e299) continue;
			Obstacle o;
			o.kind = s.kind.empty() ? "building" : s.kind;
			o.cylinder = false;
			o.hull = Hull(s.pts);
			if (o.hull.size() < 3) continue;
			o.y0 = ground - 0.5 - BALL_RADIUS;
			o.y1 = ground - 0.5 + s.h + BALL_RADIUS;
			o.minX = o.minZ = 1e300;
			o.maxX = o.maxZ = -1e300;
			for (const P2& q : o.hull)
			{
				o.minX = std::min(o.minX, q.first); o.maxX = std::max(o.maxX, q.first);
				o.minZ = std::min(o.minZ, q.second); o.maxZ = std::max(o.maxZ, q.second);
			}
			Add(o);
		}
	}

	// The flagstick on this hole's pin (or none)
	void PlayCollider::SetPin(const V3* pin)
	{
		hasPole = pin != nullptr;
		if (!pin) return;
		pole.kind = "pole";
		pole.cylinder = true;
		pole.x = pin->x; pole.z = pin->z; pole.r = POLE_RADIUS + BALL_RADIUS;
		pole.y0 = pin->y - 0.1;
		pole.y1 = pin->y - 0.1 + POLE_HEIGHT;
	}

	// Where the segment a -> b first enters the obstacle (t in 0..1), and the surface's normal there
	bool PlayCollider::CastOne(const Obstacle& o, const V3& a, const V3& b, double& t, V3& n) const
	{
		const double dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
		if (o.cylinder)
		{
			const double mx = a.x - o.x, mz = a.z - o.z;
			const double A = dx * dx + dz * dz, B = 2 * (mx * dx + mz * dz), C = mx * mx + mz * mz - o.r * o.r;
			// (starting inside it - as Jolt's solid shapes: a hit at once, out sideways)
			if (C <= 0 && a.y >= o.y0 && a.y <= o.y1)
			{
				const double l = std::sqrt(mx * mx + mz * mz);
				t = 0;
				n = l > 1e-9 ? V3{ mx / l, 0, mz / l } : V3{ 1, 0, 0 };
				return true;
			}
			bool found = false;
			if (A > 1e-12)
			{
				const double disc = B * B - 4 * A * C;
				if (disc >= 0)
				{
					const double tt = (-B - std::sqrt(disc)) / (2 * A);
					const double y = a.y + dy * tt;
					if (tt >= 0 && tt <= 1 && y >= o.y0 && y <= o.y1)
					{
						const double hx = mx + dx * tt, hz = mz + dz * tt, l = std::sqrt(hx * hx + hz * hz);
						t = tt;
						n = V3{ hx / l, 0, hz / l };
						found = true;
					}
				}
			}
			// its top, coming down onto it
			if (a.y >= o.y1 && b.y < o.y1)
			{
				const double tt = (a.y - o.y1) / (a.y - b.y);
				const double hx = mx + dx * tt, hz = mz + dz * tt;
				if (hx * hx + hz * hz <= o.r * o.r && (!found || tt < t))
				{
					t = tt;
					n = V3{ 0, 1, 0 };
					found = true;
				}
			}
			return found;
		}
		// A convex prism: the segment clipped by each side, the top and the bottom (Cyrus-Beck).
		// Each plane: inside where Dist0 + Rate * t <= 0
		double tIn = 0, tOut = 1;
		V3 nIn{ 0, 1, 0 };
		auto clip = [&](double dist0, double rate, const V3& nn) {
			if (std::fabs(rate) < 1e-15) return dist0 <= 0;
			const double tt = -dist0 / rate;
			if (rate < 0) { if (tt > tIn) { tIn = tt; nIn = nn; } }
			else if (tt < tOut) tOut = tt;
			return tIn <= tOut;
		};
		const size_t N = o.hull.size();
		for (size_t i = 0; i < N; i++)
		{
			const P2& p0 = o.hull[i];
			const P2& p1 = o.hull[(i + 1) % N];
			// the outward normal of a counter-clockwise edge in (x, z)
			double nx = p1.second - p0.second, nz = -(p1.first - p0.first);
			const double l = std::sqrt(nx * nx + nz * nz);
			if (l < 1e-12) continue;
			nx /= l;
			nz /= l;
			if (!clip(nx * (a.x - p0.first) + nz * (a.z - p0.second), nx * dx + nz * dz, V3{ nx, 0, nz })) return false;
		}
		if (!clip(a.y - o.y1, dy, V3{ 0, 1, 0 })) return false;
		if (!clip(o.y0 - a.y, -dy, V3{ 0, -1, 0 })) return false;
		if (tIn > 1 || tOut < 0 || tIn > tOut) return false;
		t = tIn;
		n = nIn;
		return true;
	}

	// Where the segment first goes into the ground (the heightmap): sampled along it, then bisected
	bool PlayCollider::CastGround(const V3& a, const V3& b, double& t) const
	{
		auto above = [&](double s) {
			const double x = a.x + (b.x - a.x) * s, z = a.z + (b.z - a.z) * s;
			const double g = course->HeightAt(x, z);
			return std::isnan(g) ? 1.0 : (a.y + (b.y - a.y) * s) - g;
		};
		if (above(0) < 0) return false; // (already under it: from inside a height field, no hit)
		const int samples = 4;
		for (int k = 1; k <= samples; k++)
		{
			const double s1 = static_cast<double>(k) / samples;
			if (above(s1) < 0)
			{
				double lo = static_cast<double>(k - 1) / samples, hi = s1;
				for (int it = 0; it < 14; it++)
				{
					const double mid = (lo + hi) / 2;
					if (above(mid) < 0) hi = mid;
					else lo = mid;
				}
				t = hi;
				return true;
			}
		}
		return false;
	}

	bool PlayCollider::Cast(const V3& a, const V3& b, bool obstacleOnly, Hit& out) const
	{
		if (!course || (a.x == b.x && a.y == b.y && a.z == b.z)) return false;
		double best = 2;
		bool found = false;
		auto at = [&](double t) { return V3{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t }; };
		if (!obstacleOnly)
		{
			double t;
			if (CastGround(a, b, t))
			{
				best = t;
				found = true;
				out.kind = "terrain";
				out.point = at(t);
				out.normal = course->NormalAt(out.point.x, out.point.z);
			}
		}
		auto consider = [&](const Obstacle& o) {
			double t;
			V3 n;
			if (CastOne(o, a, b, t, n) && t < best)
			{
				best = t;
				found = true;
				out.kind = o.kind;
				out.point = at(t);
				out.normal = n;
			}
		};
		if (hasPole) consider(pole);
		const int64_t i0 = static_cast<int64_t>(std::floor(std::min(a.x, b.x) / cell)), i1 = static_cast<int64_t>(std::floor(std::max(a.x, b.x) / cell));
		const int64_t j0 = static_cast<int64_t>(std::floor(std::min(a.z, b.z) / cell)), j1 = static_cast<int64_t>(std::floor(std::max(a.z, b.z) / cell));
		std::vector<int> seen;
		for (int64_t i = i0; i <= i1; i++)
			for (int64_t j = j0; j <= j1; j++)
			{
				auto it = grid.find(Key(i, j));
				if (it == grid.end()) continue;
				for (int k : it->second)
				{
					if (std::find(seen.begin(), seen.end(), k) != seen.end()) continue;
					seen.push_back(k);
					consider(obstacles[k]);
				}
			}
		return found;
	}
}
