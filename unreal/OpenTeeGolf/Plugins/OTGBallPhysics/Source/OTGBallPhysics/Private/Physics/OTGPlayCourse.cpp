// See OTGPlayCourse.h.

#include "Physics/OTGPlayCourse.h"

#include <algorithm>
#include <cmath>

namespace otg
{
	bool PlayCourse::Deck::TopAt(double x, double z, double& y) const
	{
		const double dx = bx - ax, dz = bz - az, l2 = dx * dx + dz * dz;
		if (l2 < 1e-6) return false;
		const double t = ((x - ax) * dx + (z - az) * dz) / l2;
		if (t < 0 || t > 1) return false;
		const double side = std::fabs((x - ax) * dz - (z - az) * dx) / std::sqrt(l2);
		if (side > halfW) return false;
		y = ay + (by - ay) * t + rise * 4 * t * (1 - t);
		return true;
	}

	double PlayCourse::HeightAt(double x, double z) const
	{
		double g = GroundAt(x, z);
		if (std::isnan(g)) return g;
		for (const Deck& d : decks)
		{
			double y;
			if (d.TopAt(x, z, y) && y > g) g = y;
		}
		return g;
	}

	double PlayCourse::GroundAt(double x, double z) const
	{
		if (x < minX || x > maxX || z < minZ || z > maxZ || N < 2) return std::nan("");
		const double fi = (x - x0) / step, fj = (z1 - z) / step;
		if (!(fi >= 0 && fj >= 0 && fi <= N - 1 && fj <= N - 1)) return std::nan("");
		const int i = std::min(N - 2, static_cast<int>(std::floor(fi))), j = std::min(N - 2, static_cast<int>(std::floor(fj)));
		const double u = fi - i, v = fj - j;
		const double a = heights[j * N + i], b = heights[j * N + i + 1], c = heights[(j + 1) * N + i], d = heights[(j + 1) * N + i + 1];
		return base + (((a * (1 - u) + b * u) * (1 - v) + (c * (1 - u) + d * u) * v) / 65535.0) * range;
	}

	// (as the game's course.js normalAt: the heights a metre either way)
	V3 PlayCourse::NormalAt(double x, double z) const
	{
		const double e = normalSpan;
		const double hl = HeightAt(x - e, z), hr = HeightAt(x + e, z), hd = HeightAt(x, z - e), hu = HeightAt(x, z + e);
		if (std::isnan(hl) || std::isnan(hr) || std::isnan(hd) || std::isnan(hu)) return V3{ 0, 1, 0 };
		const double nx = hl - hr, nz = hd - hu, ny = 2 * e;
		const double l = std::sqrt(nx * nx + ny * ny + nz * nz);
		return V3{ nx / l, ny / l, nz / l };
	}

	Surface PlayCourse::SurfaceAt(double x, double z) const
	{
		if (x < minX || x > maxX || z < minZ || z > maxZ) return Surface::None;
		// (on a bridge's deck: a path, whatever's under it)
		if (!decks.empty())
		{
			const double g = GroundAt(x, z);
			for (const Deck& d : decks)
			{
				double y;
				if (d.TopAt(x, z, y) && !(y < g - 0.02)) return Surface::Path;
			}
		}
		const int i = static_cast<int>(std::floor((x - sMinX) / res)), j = static_cast<int>(std::floor((sMaxZ - z) / res));
		if (i < 0 || j < 0 || i >= w || j >= h) return Surface::None;
		const uint8_t c = surfaces[static_cast<size_t>(j) * w + i];
		return c <= static_cast<uint8_t>(Surface::Oob) ? static_cast<Surface>(c) : Surface::Rough;
	}

	void PlayTrees::Build(double cellM)
	{
		cell = cellM;
		map.clear();
		for (int k = 0; k < static_cast<int>(all.size()); k++)
		{
			const Tree& t = all[k];
			const double reach = std::max({ t.cone ? t.coneR : t.rh, t.trunkR, 0.0 });
			const int64_t i0 = static_cast<int64_t>(std::floor((t.x - reach) / cell)), i1 = static_cast<int64_t>(std::floor((t.x + reach) / cell));
			const int64_t j0 = static_cast<int64_t>(std::floor((t.z - reach) / cell)), j1 = static_cast<int64_t>(std::floor((t.z + reach) / cell));
			for (int64_t i = i0; i <= i1; i++)
				for (int64_t j = j0; j <= j1; j++) map[Key(i, j)].push_back(k);
		}
	}

	void PlayTrees::Near(double x, double z, std::vector<const Tree*>& out) const
	{
		auto it = map.find(Key(static_cast<int64_t>(std::floor(x / cell)), static_cast<int64_t>(std::floor(z / cell))));
		if (it == map.end()) return;
		for (int k : it->second) out.push_back(&all[k]);
	}
}
