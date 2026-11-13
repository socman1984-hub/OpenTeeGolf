// OpenTeeGolf ball physics in C++: public/js/openfairway.js and public/js/physics.js, kept line for
// line (the same names, the same order of operations), so the parity test can hold them to the
// same results. When either JS file changes, change this to match. See OTGBallPhysics.h.

#include "Physics/OTGBallPhysics.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <unordered_map>

namespace otg
{
	namespace
	{
		const double kPi = 3.14159265358979323846;
		inline V3 vec(double x, double y, double z) { return V3{ x, y, z }; }
		inline double dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
		inline V3 cross(const V3& a, const V3& b) { return vec(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
		inline double hypot3(double a, double b, double c) { return std::sqrt(a * a + b * b + c * c); }
		inline double len(const V3& a) { return hypot3(a.x, a.y, a.z); }
		inline V3 add(const V3& a, const V3& b, double k = 1) { return vec(a.x + b.x * k, a.y + b.y * k, a.z + b.z * k); }
		inline V3 scale(const V3& a, double k) { return vec(a.x * k, a.y * k, a.z * k); }
		inline V3 tangent(const V3& a, const V3& n) { return add(a, n, -dot(a, n)); }
		inline double lerp(double a, double b, double t) { return a + (b - a) * t; }
		inline double clamp(double x, double lo, double hi) { return std::min(hi, std::max(lo, x)); }
		inline double smooth01(double t) { const double c = clamp(t, 0, 1); return c * c * (3 - 2 * c); }
		inline double smoothRange(double x, double a, double b) { return std::abs(b - a) < 1e-6 ? (x >= b ? 1 : 0) : smooth01((x - a) / (b - a)); }
		inline double rampUp(double x, double a, double b) { return b <= a ? (x >= a ? 1 : 0) : smooth01((x - a) / (b - a)); }
		inline double sign(double x) { return x > 0 ? 1 : x < 0 ? -1 : 0; }
		inline V3 validNormal(const V3& n)
		{
			const double l = len(n);
			return std::isfinite(l) && l > 1e-3 ? scale(n, 1 / l) : vec(0, 1, 0);
		}
	}

	// ================================================================== OpenFairway (openfairway.js)
	namespace of
	{
		const double MASS = 0.04592623;
		const double RADIUS = 0.021335;
		const double G = 9.81;
		const double RAD_PER_RPM = 0.10472;
		const double DT = 1.0 / 120;

		namespace
		{
			const double AREA = kPi * RADIUS * RADIUS;
			const double INERTIA = 0.4 * MASS * RADIUS * RADIUS;

			// ---- profiles (FLIGHT, BOUNCE, ROLLOUT, PROFILE)
			namespace P
			{
				const double spinDecayCoefficient = 2.0e-5;
				const double cdPoly[4] = { 1.1948, -0.0000209661, 1.42472e-10, -3.14383e-16 };
				const double highReCdCap = 0.2, highReCdBlendStart = 175000, lowReCdFloor = 0.38, lowReBlendStart = 30000, cdAt50k = 0.4632;
				const double clMaxBase = 0.268, clMaxHighSpin = 0.32, clMaxSrTransitionStart = 0.35, clMaxSrTransitionEnd = 0.5;
				const double spinDragMultiplierCoeff = 4.0, spinDragMultiplierMax = 1.2, spinDragMultiplierHighSpinMax = 1.03, spinDragMultiplierUltraHighSpinMax = 1.21;
				const double highSpinDragSrStart = 0.3, highSpinDragSrEnd = 0.48, highSpinDragReliefReFullMax = 90000, highSpinDragReliefReZero = 105000;
				const double ultraHighSpinDragSrStart = 0.57, ultraHighSpinDragSrEnd = 0.77;
				const bool bridgeLowReLift = true;
				const double highReStart = 75000, highReMidSpinGain = 16.0, highReSpinGain = 16.0;
				const double highReGainReductionStart = 0.1, highReGainReductionEnd = 0.18, highReGainRecoveryStart = 0.26, highReGainRecoveryEnd = 0.4;
				const double highSpinClAttenuationStart = 0.45, highSpinClAttenuationEnd = 0.55, highSpinClAttenuationMax = 0.09;
				const double ultraHighSpinClAttenuationStart = 0.58, ultraHighSpinClAttenuationEnd = 0.85, ultraHighSpinClAttenuationMax = 0.1;
				const double lowReHighSpinClAttenuationMax = 0.1, lowReUltraHighSpinClAttenuationMax = 0.06;
				const double lowLaunchLiftRecoveryMax = 1.0, lowLaunchVlaFullDeg = 6.5, lowLaunchVlaZeroDeg = 9.5, lowLaunchReStart = 85000, lowLaunchReEnd = 110000;
				const double lowLaunchSpinRatioFull = 0.18, lowLaunchSpinRatioMax = 0.22;
				const double spinDragProgressiveCapSrStart = 0.33, spinDragProgressiveCapSrEnd = 0.5, spinDragProgressiveCapBoostMax = 0.25;
				const double midSpinClBoostSrStart = 0.17, midSpinClBoostSrEnd = 0.31, midSpinClBoostMax = 0.45;
				const double highLaunchDragBoostMax = 1.24, highLaunchDragVlaStartDeg = 24.5, highLaunchDragVlaFullDeg = 31.5, highLaunchDragSrStart = 0.5, highLaunchDragSrEnd = 0.7;
			}
			namespace B
			{
				const double corBaseA = 0.45, corBaseB = -0.01, corBaseC = 0.0002, corHighSpeedCap = 0.25, corHighSpeedThreshold = 20.0, corKillThreshold = 2.0;
				const double flightTangentialRetentionBase = 0.55, flightSpinFactorMin = 0.4, flightSpinFactorDivisor = 8000;
				const double flightRetentionRampStartMps = 0.75, flightRetentionRampEndMps = 2.0;
				const double rolloutLowSpinRetention = 0.85, rolloutHighSpinRetention = 0.7, rolloutSpinRatioThreshold = 0.2;
				const double spinCorLowSpinThreshold = 1500, spinCorLowSpinMaxReduction = 0.3, spinCorHighSpinRangeRpm = 1500, spinCorHighSpinAdditionalReduction = 0.4;
				const double corVelocityLowThreshold = 12.0, corVelocityMidThreshold = 25.0, corVelocityLowScale = 0.5;
				const double rolloutBounceCorKillThreshold = 4.0, rolloutBounceCorScale = 0.5;
			}
			namespace R
			{
				const double chipSpeedThreshold = 20.0, pitchSpeedThreshold = 35.0, chipVelocityScaleMin = 0.6, chipVelocityScaleMax = 0.87;
				const double lowSpinThreshold = 1250, midSpinThreshold = 1750, lowSpinMultiplierMax = 1.3, midSpinMultiplierMax = 2.25, highSpinMultiplierMax = 2.5, highSpinRampRange = 1000;
				const double tangentVelocityThreshold = 0.05, slopeResistanceRatio = 1.6;
			}
			const double PROFILE_dragScale = 1.01, PROFILE_liftScale = 1.0, PROFILE_sandPlowRatio = 2.5, PROFILE_greenGrassViscosity = 0.0009;

			std::map<std::string, SurfaceParams> MakePresets()
			{
				std::map<std::string, SurfaceParams> p;
				p["fairway"] = MakeSurface(0.5, 0.05, 0.0017, 0.29, 0.78);
				p["fairwaySoft"] = MakeSurface(0.56, 0.07, 0.0024, 0.32, 0.92);
				p["rough"] = MakeSurface(0.62, 0.095, 0.0032, 0.35, 0.7);
				p["firm"] = MakeSurface(0.3, 0.03, 0.001, 0.25, 0.6);
				p["fringe"] = MakeSurface(0.56, 0.0725, 0.00245, 0.29, 0.78);
				SurfaceParams green = MakeSurface(0.58, std::numeric_limits<double>::quiet_NaN(), PROFILE_greenGrassViscosity, 0.36, 1.12);
				green.spinbackThetaBoostMax = 0.12; green.spinbackSpinStartRpm = 3500; green.spinbackSpinEndRpm = 5500;
				green.spinbackSpeedStartMps = 8; green.spinbackSpeedEndMps = 20; green.rollingSpinScale = 0;
				p["green"] = green;
				SurfaceParams sand = MakeSurface(0.65, 0.3, 0.012, 0.45, 1.0);
				sand.restitutionScale = 0.6; sand.impactRetentionScale = 0.25; sand.impactPlowRatio = PROFILE_sandPlowRatio;
				sand.hasBounce = true; sand.firstImpactMinimumSpeed = 0.15; sand.laterImpactMinimumSpeed = 0.15; sand.laterImpactScale = 0.85; sand.minimumBounceHeight = 0.001;
				p["sand"] = sand;
				return p;
			}
		}

		SurfaceParams MakeSurface(double kineticFriction, double rollingFriction, double grassViscosity, double criticalAngle, double spinbackResponseScale)
		{
			SurfaceParams s;
			s.kineticFriction = kineticFriction; s.rollingFriction = rollingFriction; s.grassViscosity = grassViscosity;
			s.criticalAngle = criticalAngle; s.spinbackResponseScale = spinbackResponseScale;
			return s;
		}

		const SurfaceParams& Preset(const char* name)
		{
			static const std::map<std::string, SurfaceParams> presets = MakePresets();
			auto it = presets.find(name);
			return it != presets.end() ? it->second : presets.at("rough");
		}

		// ---- green speed
		namespace
		{
			const double STIMPMETER_MPS = 1.83;
			double speedDragPerSecond(double viscosity) { return (10 * kPi * RADIUS * viscosity) / MASS; }
			double rollMeters(double speed, double a0, double k)
			{
				return k <= 0 ? (speed * speed) / (2 * a0) : speed / k - (a0 / (k * k)) * std::log(1 + (k * speed) / a0);
			}
		}

		double GreenRollingFriction(double stimpFt, double viscosity)
		{
			if (viscosity < 0) viscosity = PROFILE_greenGrassViscosity;
			const double k = speedDragPerSecond(viscosity);
			const double d = clamp(std::isfinite(stimpFt) ? stimpFt : 10, 6, 15) * 0.3048;
			double lo = 0, hi = (STIMPMETER_MPS * STIMPMETER_MPS) / (2 * d);
			for (int i = 0; i < 32; i++)
			{
				const double mid = 0.5 * (lo + hi);
				if (rollMeters(STIMPMETER_MPS, mid, k) > d) lo = mid; else hi = mid;
			}
			return (7 * 0.5 * (lo + hi)) / (5 * G);
		}

		double PuttSpeed(double distanceM, double stimpFt, double viscosity)
		{
			if (viscosity < 0) viscosity = PROFILE_greenGrassViscosity;
			const double a0 = (5 * G * GreenRollingFriction(stimpFt, viscosity)) / 7, k = speedDragPerSecond(viscosity);
			double lo = 0, hi = 30;
			for (int i = 0; i < 40; i++)
			{
				const double mid = 0.5 * (lo + hi);
				if (rollMeters(mid, a0, k) > distanceM) hi = mid; else lo = mid;
			}
			return 0.5 * (lo + hi);
		}

		// ---- air
		double AirDensity(double altitudeM, double tempC, double humidity, double stationPressurePa)
		{
			const double T = std::max(tempC + 273.15, 1.0);
			const double rh = std::isnan(humidity) ? 0 : clamp(humidity, 0, 1);
			const double p = stationPressurePa > 0 ? stationPressurePa : 101325 * std::exp((-9.80665 * 0.0289644 * altitudeM) / (8.314462618 * T));
			if (rh <= 0) return p / (287.058 * T);
			const double pv = rh * 610.78 * std::pow(10.0, (7.5 * tempC) / (tempC + 237.3));
			return (std::max(p - pv, 0.0) * 0.0289644 + pv * 0.018016) / (8.314462618 * T);
		}

		double AirViscosity(double tempC)
		{
			const double T = std::max(tempC + 273.15, 1.0);
			return (1.716e-5 * std::pow(T / 273.15, 1.5) * (273.15 + 110.4)) / (T + 110.4);
		}

		double WindFactor(double heightM, double referenceM, double roughnessM)
		{
			if (!std::isfinite(heightM) || heightM <= roughnessM) return 0;
			const double lz = std::log(roughnessM);
			return (std::log(std::min(heightM, 100.0)) - lz) / (std::log(referenceM) - lz);
		}

		// ---- aerodynamics
		namespace
		{
			const double RE_SAMPLES[5] = { 50000, 60000, 65000, 70000, 75000 };
			double clRe50k(double s) { return 0.0472121 + 2.84795 * s - 23.4342 * s * s + 45.4849 * s * s * s; }
			double clRe60k(double s) { return 0.320524 - 4.7032 * s + 14.0613 * s * s; }
			double clRe65k(double s) { return 0.266667 - 4.0 * s + 13.3333 * s * s; }
			double clRe70k(double s) { return 0.0496189 + 0.00211396 * s + 2.34201 * s * s; }

			double clMax(double s)
			{
				if (s <= P::clMaxSrTransitionStart) return P::clMaxBase;
				if (s >= P::clMaxSrTransitionEnd) return P::clMaxHighSpin;
				return lerp(P::clMaxBase, P::clMaxHighSpin, smoothRange(s, P::clMaxSrTransitionStart, P::clMaxSrTransitionEnd));
			}
			double highReSpinGain(double s)
			{
				if (s <= P::highReGainReductionStart) return P::highReSpinGain;
				if (s < P::highReGainReductionEnd) return lerp(P::highReSpinGain, P::highReMidSpinGain, smoothRange(s, P::highReGainReductionStart, P::highReGainReductionEnd));
				if (s <= P::highReGainRecoveryStart) return P::highReMidSpinGain;
				if (s < P::highReGainRecoveryEnd) return lerp(P::highReMidSpinGain, P::highReSpinGain, smoothRange(s, P::highReGainRecoveryStart, P::highReGainRecoveryEnd));
				return P::highReSpinGain;
			}
			double clHighRe(double s) { const double g = highReSpinGain(s); return (clMax(s) * s * g) / (1 + s * g); }
			double clAtSample(int i, double s)
			{
				switch (i) { case 0: return clRe50k(s); case 1: return clRe60k(s); case 2: return clRe65k(s); case 3: return clRe70k(s); default: return clHighRe(s); }
			}
			double highSpinAttenuation(double s, double cl, double max, double ultraMax)
			{
				return cl * (1 - max * smoothRange(s, P::highSpinClAttenuationStart, P::highSpinClAttenuationEnd)) *
					(1 - ultraMax * smoothRange(s, P::ultraHighSpinClAttenuationStart, P::ultraHighSpinClAttenuationEnd));
			}
			double spinDragMultiplier(double s, double re)
			{
				if (s <= 0) return 1;
				const double highSpin = smoothRange(s, P::highSpinDragSrStart, P::highSpinDragSrEnd);
				const double relief = 1 - smoothRange(re, P::highSpinDragReliefReFullMax, P::highSpinDragReliefReZero);
				double cap = lerp(P::spinDragMultiplierMax, P::spinDragMultiplierHighSpinMax, highSpin * relief);
				cap += P::spinDragProgressiveCapBoostMax * smoothRange(s, P::spinDragProgressiveCapSrStart, P::spinDragProgressiveCapSrEnd);
				cap = lerp(cap, P::spinDragMultiplierUltraHighSpinMax, smoothRange(s, P::ultraHighSpinDragSrStart, P::ultraHighSpinDragSrEnd));
				return std::min(1 + P::spinDragMultiplierCoeff * s * s, cap);
			}
			double lowLaunchLiftScale(double vlaDeg, double s, double re)
			{
				const double launch = smoothRange(P::lowLaunchVlaZeroDeg - vlaDeg, 0, P::lowLaunchVlaZeroDeg - P::lowLaunchVlaFullDeg);
				if (launch <= 0) return 1;
				const double reF = smoothRange(re, P::lowLaunchReStart, P::lowLaunchReEnd);
				if (reF <= 0) return 1;
				const double spinF = 1 - smoothRange(s, P::lowLaunchSpinRatioFull, P::lowLaunchSpinRatioMax);
				if (spinF <= 0) return 1;
				return lerp(1, P::lowLaunchLiftRecoveryMax, launch * reF * spinF);
			}
			double highLaunchDragScale(double vlaDeg, double s)
			{
				const double launch = smoothRange(vlaDeg, P::highLaunchDragVlaStartDeg, P::highLaunchDragVlaFullDeg);
				if (launch <= 0) return 1;
				const double spinF = smoothRange(s, P::highLaunchDragSrStart, P::highLaunchDragSrEnd);
				if (spinF <= 0) return 1;
				return lerp(1, P::highLaunchDragBoostMax, launch * spinF);
			}
			double midSpinClBoost(double s)
			{
				if (P::midSpinClBoostMax <= 0) return 1;
				const double t = smoothRange(s, P::midSpinClBoostSrStart, P::midSpinClBoostSrEnd);
				return 1 + P::midSpinClBoostMax * t * (1 - t) * 4;
			}
			const double MIN_AERO_SPEED = 0.5;
		}

		double DragCoefficient(double re)
		{
			const double cap = 200000;
			if (re > cap) return P::highReCdCap;
			if (re >= 50000)
			{
				const double a = P::cdPoly[0], b = P::cdPoly[1], c = P::cdPoly[2], d = P::cdPoly[3];
				const double poly = a + b * re + c * re * re + d * re * re * re;
				if (P::highReCdBlendStart >= cap || re <= P::highReCdBlendStart) return poly;
				return lerp(poly, P::highReCdCap, smoothRange(re, P::highReCdBlendStart, cap));
			}
			if (re <= P::lowReBlendStart) return P::lowReCdFloor;
			return lerp(P::lowReCdFloor, P::cdAt50k, smoothRange(re, P::lowReBlendStart, 50000));
		}

		double LiftCoefficient(double re, double spinRatio)
		{
			const double s = std::max(0.0, spinRatio);
			if (s <= 0) return 0;
			const double max = clMax(s);
			if (re < 50000)
			{
				if (re <= 30000) return 0;
				const double t = smooth01((re - 30000) / 20000);
				return highSpinAttenuation(s, clamp(clRe50k(s), 0, max) * t, P::lowReHighSpinClAttenuationMax, P::lowReUltraHighSpinClAttenuationMax);
			}
			if (re >= P::highReStart) return highSpinAttenuation(s, clamp(clHighRe(s), 0, max), P::highSpinClAttenuationMax, P::ultraHighSpinClAttenuationMax);
			static const int bridged[3] = { 0, 3, 4 }, all[5] = { 0, 1, 2, 3, 4 };
			const int* samples = P::bridgeLowReLift ? bridged : all;
			const int count = P::bridgeLowReLift ? 3 : 5;
			int hiSlot = count - 1;
			for (int k = 0; k < count; k++) if (re <= RE_SAMPLES[samples[k]]) { hiSlot = k; break; }
			const int hi = samples[hiSlot], lo = samples[std::max(hiSlot - 1, 0)];
			const double clLo = std::max(0.0, clAtSample(lo, s)), clHi = std::max(0.0, clAtSample(hi, s));
			const double reLo = RE_SAMPLES[lo], reHi = RE_SAMPLES[hi];
			const double w = reHi != reLo ? (re - reLo) / (reHi - reLo) : 0;
			return highSpinAttenuation(s, clamp(lerp(clLo, clHi, w), 0, max), P::lowReHighSpinClAttenuationMax, P::lowReUltraHighSpinClAttenuationMax);
		}

		Aero AeroSample(const V3& air, const V3& spin, const Env& env)
		{
			Aero out;
			out.speed = len(air);
			out.spinDecay = (std::max(0.0, P::spinDecayCoefficient) * out.speed) / RADIUS;
			if (out.speed < MIN_AERO_SPEED) return out;
			out.spinRatio = (len(spin) * RADIUS) / out.speed;
			out.reynolds = (env.rho * out.speed * RADIUS * 2) / env.mu;
			out.cd = DragCoefficient(out.reynolds) * spinDragMultiplier(out.spinRatio, out.reynolds) * env.dragScale * highLaunchDragScale(env.launchDeg, out.spinRatio);
			out.cl = LiftCoefficient(out.reynolds, out.spinRatio) * env.liftScale * lowLaunchLiftScale(env.launchDeg, out.spinRatio, out.reynolds) * midSpinClBoost(out.spinRatio);
			return out;
		}

		Aero StepFlight(V3& v, V3& spin, const Env& env, const V3& wind, double dt)
		{
			const V3 air = vec(v.x - wind.x, v.y - wind.y, v.z - wind.z);
			const Aero s = AeroSample(air, spin, env);
			V3 a = vec(0, -G, 0);
			if (s.speed >= MIN_AERO_SPEED)
			{
				const double k = (0.5 * env.rho * AREA * s.speed) / MASS;
				a = add(a, air, -k * s.cd);
				const double w = len(spin);
				if (w > 0.1) a = add(a, cross(spin, air), (k * s.cl) / w);
			}
			v = add(v, a, dt);
			spin = scale(spin, std::exp(-s.spinDecay * dt));
			return s;
		}

		// ---- launch calibration
		namespace
		{
			const char* family(double mph, double vla) { return mph < 60 ? "C" : mph > 110 && vla < 18 ? "D" : vla > 30 ? "W" : "I"; }
			const char* speedBin(double mph) { return mph < 60 ? "S0" : mph < 72 ? "S1a" : mph < 85 ? "S1b" : mph < 105 ? "S2" : mph < 120 ? "S3" : "S4"; }
			const char* launchBin(double vla) { return vla < 10 ? "V0" : vla < 18 ? "V1" : vla < 25 ? "V2" : vla < 33 ? "V3" : "V4"; }
			const char* spinBin(double rpm) { return rpm < 2500 ? "P0" : rpm < 4000 ? "P1" : rpm < 5500 ? "P2" : rpm < 7500 ? "P3" : "P4"; }

			struct DL { double drag, lift; };
			const std::unordered_map<std::string, DL>& Regimes()
			{
				static const std::unordered_map<std::string, DL> r = {
					{ "C-S0", { 0.7, 1.2 } }, { "C-S0-V1-P0", { 0.55, 1.15 } }, { "C-S0-V4-P3", { 0.65, 1.25 } },
					{ "I-S1a-V0-P1", { 0.94, 1.04 } }, { "I-S1a-V2-P1", { 0.8, 1.14 } }, { "I-S1a-V2-P2", { 0.82, 1.13 } }, { "I-S1a-V2-P3", { 0.82, 1.12 } },
					{ "I-S1a-V3-P2", { 0.79, 1.14 } }, { "I-S1a-V3-P3", { 0.94, 1.04 } }, { "I-S1a-V1-P2", { 0.92, 1.05 } },
					{ "I-S1b-V0-P0", { 0.97, 1.02 } }, { "I-S1b-V2-P2", { 0.94, 1.03 } }, { "I-S1b-V2-P3", { 0.97, 1.01 } }, { "I-S1b-V3-P2", { 0.88, 1.06 } },
					{ "I-S1b-V3-P3", { 0.97, 1.02 } }, { "I-S1b-V1-P2", { 0.98, 1.01 } },
					{ "W-S1a-V3-P3", { 0.83, 1.1 } },
					{ "I-S3-V2-P3", { 1.11, 0.94 } }, { "I-S3-V1-P2", { 1.04, 0.98 } }, { "I-S2-V2-P3", { 1.05, 1 } }, { "I-S2-V2-P4", { 1.06, 0.95 } },
					{ "I-S2-V1-P2", { 1.03, 0.99 } }, { "I-S2-V0-P2", { 1.06, 0.96 } }, { "I-S2-V1-P0", { 0.96, 1.03 } }, { "I-S2-V1-P1", { 0.97, 1.02 } },
					{ "D-S3-V1", { 1.04, 0.99 } }, { "D-S4-V0-P1", { 1.03, 0.98 } }, { "D-S4-V0-P2", { 1.09, 0.94 } }, { "D-S4-V1-P0", { 0.98, 1.02 } },
					{ "D-S4-V1-P1", { 1.04, 1 } }, { "D-S4-V1-P2", { 1.04, 1 } },
					{ "W-S2-V3-P4", { 1.06, 0.97 } },
				};
				return r;
			}
			DL lookupRegime(double mph, double vla, double rpm)
			{
				const std::string f = family(mph, vla), s = speedBin(mph), l = launchBin(vla);
				const std::string keys[4] = { f + "-" + s + "-" + l + "-" + spinBin(rpm), f + "-" + s + "-" + l, f + "-" + s, f };
				for (const std::string& k : keys)
				{
					auto it = Regimes().find(k);
					if (it != Regimes().end()) return it->second;
				}
				return { 1, 1 };
			}

			const std::vector<double> SPEED_ANCHORS = { 54, 66, 78.5, 95, 107.5, 115, 125 };
			const std::vector<double> LAUNCH_ANCHORS = { 5, 14, 21.5, 27.5, 31.5, 36.5 };
			const std::vector<double> SPIN_ANCHORS = { 1250, 3250, 4750, 6500, 8500 };
			struct AW { double a, w; };
			std::vector<AW> bracket(const std::vector<double>& anchors, double x)
			{
				if (x <= anchors[0]) return { { anchors[0], 1 } };
				for (size_t i = 1; i < anchors.size(); i++)
				{
					if (x < anchors[i])
					{
						const double t = (x - anchors[i - 1]) / (anchors[i] - anchors[i - 1]);
						return { { anchors[i - 1], 1 - t }, { anchors[i], t } };
					}
				}
				return { { anchors.back(), 1 } };
			}
			struct LandingRow { double bite, spinback, rampStart, rampEnd; };
			LandingRow landingRow(const std::string& bucket)
			{
				if (bucket == "release") return { 0.5, 0, 8, 20 };
				if (bucket == "check") return { 0.85, 0, 8, 20 };
				if (bucket == "spinback") return { 1, 1.2, 8, 20 };
				return { 0, 0, 0, 0 };
			}
		}

		std::string RegimeKey(double mph, double vla, double rpm)
		{
			return std::string(family(mph, vla)) + "-" + speedBin(mph) + "-" + launchBin(vla) + "-" + spinBin(rpm);
		}

		std::string LandingBucket(double vla, double rpm)
		{
			const std::string p = spinBin(rpm);
			if (p == "P0" || p == "P1") return "run";
			if (p == "P2") return "release";
			if (p == "P3") return "check";
			const std::string v = launchBin(vla);
			return v == "V0" || v == "V1" ? "check" : "spinback";
		}

		Launch ResolveLaunch(double speedMph, double vlaDeg, double totalSpinRpm)
		{
			double drag = 0, lift = 0;
			for (const AW& s : bracket(SPEED_ANCHORS, speedMph))
				for (const AW& l : bracket(LAUNCH_ANCHORS, vlaDeg))
					for (const AW& p : bracket(SPIN_ANCHORS, totalSpinRpm))
					{
						const double w = s.w * l.w * p.w;
						if (w <= 0) continue;
						const DL r = lookupRegime(s.a, l.a, p.a);
						drag += r.drag * w; lift += r.lift * w;
					}
			Launch out;
			for (const AW& l : bracket(LAUNCH_ANCHORS, vlaDeg))
				for (const AW& p : bracket(SPIN_ANCHORS, totalSpinRpm))
				{
					const LandingRow r = landingRow(LandingBucket(l.a, p.a));
					const double w = l.w * p.w;
					out.landing.spinBiteScale += r.bite * w; out.landing.maxSpinbackMps += r.spinback * w;
					out.landing.speedRampStartMps += r.rampStart * w; out.landing.speedRampEndMps += r.rampEnd * w;
				}
			out.regime = RegimeKey(speedMph, vlaDeg, totalSpinRpm);
			out.bucket = LandingBucket(vlaDeg, totalSpinRpm);
			out.dragScale = PROFILE_dragScale * drag;
			out.liftScale = PROFILE_liftScale * lift;
			out.launchDeg = vlaDeg;
			return out;
		}

		// ---- impact
		namespace
		{
			double landingWeight(double vn) { return rampUp(vn, B::flightRetentionRampStartMps, B::flightRetentionRampEndMps); }
			double flightRetention(double vn, double rpm)
			{
				const double landing = B::flightTangentialRetentionBase * clamp(1 - rpm / B::flightSpinFactorDivisor, B::flightSpinFactorMin, 1);
				const double w = landingWeight(vn);
				return w >= 1 ? landing : lerp(1, landing, w);
			}
			double restitution(double vn, double rpm, double minimumSpeed)
			{
				const double base = vn < minimumSpeed ? 0 : vn > B::corHighSpeedThreshold ? B::corHighSpeedCap : B::corBaseA + B::corBaseB * vn + B::corBaseC * vn * vn;
				const double speedF = vn < B::corVelocityLowThreshold ? (B::corVelocityLowScale * vn) / B::corVelocityLowThreshold
					: vn < B::corVelocityMidThreshold ? lerp(B::corVelocityLowScale, 1, (vn - B::corVelocityLowThreshold) / (B::corVelocityMidThreshold - B::corVelocityLowThreshold))
					: 1;
				const double spinF = rpm < B::spinCorLowSpinThreshold ? (B::spinCorLowSpinMaxReduction * rpm) / B::spinCorLowSpinThreshold
					: B::spinCorLowSpinMaxReduction + B::spinCorHighSpinAdditionalReduction * std::min((rpm - B::spinCorLowSpinThreshold) / B::spinCorHighSpinRangeRpm, 1.0);
				return clamp(base * (1 - spinF * speedF), 0, 1);
			}
			double kineticEnergy(const V3& v, const V3& w) { return 0.5 * MASS * dot(v, v) + 0.5 * INERTIA * dot(w, w); }

			void landingBite(const V3& v0, const V3& w0, V3& v, V3& w, const V3& n, const SurfaceParams& surf, const Landing& resp)
			{
				const V3 inT = tangent(v0, n);
				const double inTL = len(inT);
				if (inTL < 0.01) return;
				const V3 dir = scale(inT, 1 / inTL);
				const double backspin = dot(w0, cross(dir, n));
				const double angle = std::atan2(-dot(v0, n), inTL);
				const double bite = resp.spinBiteScale * surf.spinbackResponseScale * (2.0 / 7) * RADIUS * backspin *
					clamp((angle - surf.criticalAngle) / 0.1, 0, 1) * rampUp(len(v0), resp.speedRampStartMps, resp.speedRampEndMps);
				if (bite <= 0) return;
				const double fwd = dot(tangent(v, n), dir);
				const double floor = resp.maxSpinbackMps > 0 && surf.spinbackResponseScale > 1 ? -resp.maxSpinbackMps : 0;
				const double bitten = std::max(fwd - bite, std::min(floor, fwd));
				if (bitten == fwd) return;
				const V3 change = scale(dir, bitten - fwd);
				const V3 v2 = add(v, change), w2 = add(w, cross(n, change), 1 / RADIUS);
				if (kineticEnergy(v2, w2) <= kineticEnergy(v0, w0)) { v = v2; w = w2; }
			}
		}

		void ResolveImpact(V3& v, V3& spin, const V3& nIn, const SurfaceParams& surf, bool firstImpact, const Landing* response)
		{
			const V3 n = validNormal(nIn);
			const double vn = dot(v, n);
			if (vn >= 0) return;
			const double incoming = -vn;
			const double rpm = len(spin) / RAD_PER_RPM;
			const double minimum = firstImpact ? (surf.hasBounce ? surf.firstImpactMinimumSpeed : B::corKillThreshold)
				: (surf.hasBounce ? surf.laterImpactMinimumSpeed : std::max(B::corKillThreshold, B::rolloutBounceCorKillThreshold));
			double e = restitution(incoming, rpm, minimum);
			if (!firstImpact) e *= surf.hasBounce ? surf.laterImpactScale : B::rolloutBounceCorScale;
			e *= surf.restitutionScale;
			const double jn = MASS * (1 + e) * incoming;
			const V3 arm = scale(n, -RADIUS);
			const V3 contact = add(v, cross(spin, arm));
			const V3 slip = tangent(contact, n);
			V3 jt = scale(slip, (-2 * MASS) / 7);
			double spinback = surf.spinbackResponseScale;
			if (firstImpact && spinback > 1)
			{
				const double spinT = smoothRange(rpm, surf.spinbackSpinStartRpm, surf.spinbackSpinEndRpm);
				const double speedT = smoothRange(len(v), surf.spinbackSpeedStartMps, surf.spinbackSpeedEndMps);
				const double effective = surf.criticalAngle + surf.spinbackThetaBoostMax * spinT * speedT;
				const double angle = std::atan2(incoming, len(tangent(v, n)));
				spinback = 1 + (spinback - 1) * clamp((angle - effective) / 0.1, 0, 1);
			}
			const double maxJt = surf.kineticFriction * std::max(0.0, spinback) * jn;
			const double jtl = len(jt);
			if (jtl > maxJt && jtl > 0) jt = scale(jt, maxJt / jtl);

			V3 nv = add(add(v, n, jn / MASS), jt, 1 / MASS);
			V3 nw = add(spin, cross(arm, jt), 1 / INERTIA);
			const double speed = len(v);
			double keep = firstImpact ? flightRetention(incoming, rpm)
				: speed > 0.1 && B::rolloutSpinRatioThreshold > 0
				? lerp(B::rolloutLowSpinRetention, B::rolloutHighSpinRetention, std::min((len(spin) * RADIUS) / speed / B::rolloutSpinRatioThreshold, 1.0))
				: B::rolloutHighSpinRetention;
			const double normalShare = clamp((incoming * incoming) / std::max(dot(v, v), 1e-12), 0, 1);
			keep = clamp(keep * lerp(1, surf.impactRetentionScale, normalShare), 0, 1);
			auto keepT = [&n](const V3& a, double k) { return add(scale(n, dot(a, n)), tangent(a, n), k); };
			nv = keepT(nv, keep);
			nw = keepT(nw, keep);
			if (surf.impactPlowRatio > 0)
			{
				const double tl = len(tangent(nv, n));
				const double k = tl > 0 ? std::max(0.0, tl - surf.impactPlowRatio * incoming) / tl : 1;
				nv = keepT(nv, k);
				nw = keepT(nw, k);
			}
			if (firstImpact && response && response->spinBiteScale > 0) landingBite(v, spin, nv, nw, n, surf, *response);
			v = nv;
			spin = nw;
		}

		bool HasRebound(const V3& v, const V3& nIn, const SurfaceParams& surf)
		{
			const V3 n = validNormal(nIn);
			const double s = dot(v, n);
			const double h = surf.hasBounce ? surf.minimumBounceHeight : 0.25 / (2 * G);
			return s > 0 && s * s >= 2 * G * std::max(n.y, 0.0) * h;
		}

		V3 LaunchSpin(const V3& v, const V3& measured, const V3& nIn)
		{
			if (dot(measured, measured) > 0) return measured;
			const V3 n = validNormal(nIn);
			const double vn = dot(v, n);
			const V3 t = tangent(v, n);
			const double rolling = (1 - landingWeight(std::abs(vn))) * (1 - rampUp(len(t), 12, 18));
			return rolling <= 0 ? vec(0, 0, 0) : scale(cross(n, t), rolling / RADIUS);
		}

		// ---- ground
		double SpinFrictionMultiplier(double rpm, double speed)
		{
			const double speedScale = speed < R::chipSpeedThreshold ? lerp(R::chipVelocityScaleMin, R::chipVelocityScaleMax, speed / R::chipSpeedThreshold)
				: speed < R::pitchSpeedThreshold ? lerp(R::chipVelocityScaleMax, 1, (speed - R::chipSpeedThreshold) / (R::pitchSpeedThreshold - R::chipSpeedThreshold))
				: 1;
			const double spinScale = rpm < R::lowSpinThreshold ? lerp(1, R::lowSpinMultiplierMax, rpm / R::lowSpinThreshold)
				: rpm < R::midSpinThreshold ? lerp(R::lowSpinMultiplierMax, R::midSpinMultiplierMax, (rpm - R::lowSpinThreshold) / (R::midSpinThreshold - R::lowSpinThreshold))
				: lerp(R::midSpinMultiplierMax, R::highSpinMultiplierMax, std::min((rpm - R::midSpinThreshold) / R::highSpinRampRange, 1.0));
			return 1 + (spinScale - 1) * speedScale;
		}

		namespace
		{
			V3 dampenSpin(const V3& w, double viscosity, double dt)
			{
				return scale(w, std::exp((-8 * kPi * viscosity * std::pow(RADIUS, 3) / INERTIA) * dt));
			}
			V3 resistContactSpin(const V3& w, const V3& n, double rollingFriction, double load, double dt)
			{
				const double ns = dot(w, n);
				const double remaining = std::max(0.0, std::abs(ns) - (rollingFriction * load * RADIUS / INERTIA) * dt);
				return add(w, n, sign(ns) * remaining - ns);
			}
		}

		void StepGround(V3& v, V3& spin, const V3& nIn, const SurfaceParams& surf, double impactRpm, double dt)
		{
			const V3 n = validNormal(nIn);
			V3 vel = tangent(v, n);
			const V3 slip = tangent(add(vel, cross(spin, scale(n, -RADIUS))), n);
			const double load = MASS * G * std::max(0.0, n.y);
			const V3 slope = add(vec(0, -MASS * G, 0), n, load);
			const double slopeL = len(slope);
			const double staticCap = surf.rollingFriction * R::slopeResistanceRatio * load;
			if (dot(vel, vel) < 0.05 * 0.05 && dot(spin, spin) < 8 * 8 && slopeL <= staticCap)
			{
				v = vec(0, 0, 0); spin = vec(0, 0, 0);
				return;
			}
			if (dot(vel, vel) < 1e-4 && dot(slip, slip) < 1e-4 && slopeL <= staticCap)
			{
				v = vec(0, 0, 0);
				spin = resistContactSpin(dampenSpin(spin, surf.grassViscosity, dt), n, surf.rollingFriction, load, dt);
				return;
			}
			const double mult = SpinFrictionMultiplier(std::max(impactRpm, len(spin) / RAD_PER_RPM), len(vel));
			V3 w = spin;
			if (len(slip) >= R::tangentVelocityThreshold && dot(slip, slip) > 1e-7)
			{
				V3 j = scale(slip, (-2 * MASS) / 7);
				const double maxJ = surf.kineticFriction * mult * load * dt, jl = len(j);
				if (jl > maxJ) j = scale(j, maxJ / jl);
				vel = add(vel, j, 1 / MASS);
				w = add(w, cross(scale(n, -RADIUS), j), 1 / INERTIA);
			}
			V3 force = add(slope, vel, -6 * kPi * RADIUS * surf.grassViscosity);
			if (dot(vel, vel) > 1e-7)
			{
				const double rollMult = surf.rollingSpinScale == 1 ? mult : 1 + (mult - 1) * surf.rollingSpinScale;
				force = add(force, vel, (-surf.rollingFriction * rollMult * load) / len(vel));
			}
			V3 nv = add(vel, force, dt / MASS);
			if (dot(vel, nv) < 0 && slopeL <= staticCap) nv = vec(0, 0, 0);
			w = resistContactSpin(dampenSpin(w, surf.grassViscosity, dt), n, surf.rollingFriction, load, dt);
			v = nv;
			spin = w;
		}

		bool CanRest(const V3& v, const V3& spin, const V3& nIn, const SurfaceParams& surf)
		{
			if (dot(v, v) >= 0.05 * 0.05 || dot(spin, spin) >= 0.5 * 0.5) return false;
			const V3 n = validNormal(nIn);
			const V3 g = vec(0, -MASS * G, 0);
			return len(tangent(g, n)) <= surf.rollingFriction * R::slopeResistanceRatio * MASS * G * std::max(0.0, n.y);
		}
	}

	// ================================================================== the course (physics.js)

	const double BALL_RADIUS = of::RADIUS;
	const double CUP_RADIUS = 0.054;

	namespace
	{
		const double MPH = 0.44704;

		struct Obstacle { double e, eVar, keep, spin; };
		Obstacle ObstacleFor(const std::string& kind)
		{
			if (kind == "trunk") return { 0.3, 0.15, 0.6, 0.3 };
			if (kind == "pole") return { 0.2, 0.1, 0.5, 0.5 };
			if (kind == "bumper") return { 0.62, 0.06, 0.86, 0.6 };
			if (kind == "blade") return { 0.35, 0.1, 0.7, 0.5 };
			return { 0.45, 0.1, 0.75, 0.5 }; // building
		}

		of::SurfaceParams SurfaceFor(Surface s)
		{
			switch (s)
			{
			case Surface::Green: return of::Preset("green");
			case Surface::Fringe: return of::Preset("fringe");
			case Surface::Fairway: case Surface::Tee: return of::Preset("fairway");
			case Surface::FirstCut: return of::MakeSurface(0.56, 0.072, 0.00245, 0.32, 0.74);
			case Surface::DeepRough: { of::SurfaceParams p = of::MakeSurface(0.7, 0.18, 0.006, 0.4, 0.6); p.restitutionScale = 0.6; p.impactRetentionScale = 0.6; return p; }
			case Surface::Native: { of::SurfaceParams p = of::MakeSurface(0.72, 0.2, 0.0065, 0.42, 0.6); p.restitutionScale = 0.55; p.impactRetentionScale = 0.55; return p; }
			case Surface::Path: { of::SurfaceParams p = of::MakeSurface(0.3, 0.02, 0.0003, 0.25, 0.6); p.restitutionScale = 2.0; return p; }
			case Surface::Sand: return of::Preset("sand");
			default: return of::Preset("rough"); // rough, oob, anything else
			}
		}

		// (a palm's head is open fronds and desert trees are airy; moss and cypress are thick)
		double SparseFor(const std::string& kind)
		{
			static const std::map<std::string, double> sparse = { { "palm", 0.6 }, { "mesquite", 0.55 }, { "paloverde", 0.45 }, { "saguaro", 0.15 }, { "liveoak", 1.2 }, { "cypress", 1.4 }, { "magnolia", 1.3 } };
			auto it = sparse.find(kind);
			return it != sparse.end() ? it->second : 1;
		}

		struct TreeState
		{
			double spinMul = 1;
			std::vector<const Tree*> inCrown;
			std::vector<std::string> hits;
		};

		// One step of the ball among trees (physics.js treeInteract). Returns "" or trunk/branch/leaves.
		std::string TreeInteract(const Trees& trees, V3& p, V3& v, double dt, TreeState& st, bool trunks, const std::function<double()>& rnd)
		{
			std::vector<const Tree*> list;
			trees.Near(p.x, p.z, list);
			if (list.empty()) return "";
			std::string event;
			for (const Tree* tp : list)
			{
				const Tree& t = *tp;
				const double dx = p.x - t.x, dz = p.z - t.z, dh = std::hypot(dx, dz);
				if (p.y < t.trunkTop && dh < t.trunkR + BALL_RADIUS)
				{
					if (!trunks) continue;
					const double nx = dh > 1e-6 ? dx / dh : 1, nz = dh > 1e-6 ? dz / dh : 0;
					const double vn = v.x * nx + v.z * nz;
					if (vn < 0)
					{
						const double e = 0.3 + rnd() * 0.15;
						v.x -= (1 + e) * vn * nx; v.z -= (1 + e) * vn * nz;
						v.x *= 0.6; v.y *= 0.6; v.z *= 0.6;
						p.x = t.x + nx * (t.trunkR + BALL_RADIUS + 0.01); p.z = t.z + nz * (t.trunkR + BALL_RADIUS + 0.01);
						st.spinMul *= 0.3;
						event = "trunk";
					}
					continue;
				}
				double depth = -1;
				if (t.cone)
				{
					const double f = (p.y - t.coneBase) / t.coneH;
					if (f >= 0 && f <= 1) { const double Rr = t.coneR * (1 - f); if (dh < Rr) depth = 1 - dh / Rr; }
				}
				else
				{
					const double q = std::pow(dh / t.rh, 2) + std::pow((p.y - t.cy) / t.rv, 2);
					if (q < 1) depth = 1 - std::sqrt(q);
				}
				if (depth < 0) continue;
				const double speed = hypot3(v.x, v.y, v.z);
				if (speed < 1e-3) continue;
				const double step = speed * dt, dense = t.kind == "spruce" ? 1.7 : t.cone ? 1.5 : SparseFor(t.kind);
				if (std::find(st.inCrown.begin(), st.inCrown.end(), tp) == st.inCrown.end()) { st.inCrown.push_back(tp); if (event.empty()) event = "leaves"; }
				const double keep = std::exp(-(0.008 + 0.12 * depth) * dense * step);
				const double jit = (0.03 + 0.06 * depth) * std::sqrt(step) * speed;
				v.x = v.x * keep + (rnd() - 0.5) * jit;
				v.y = v.y * keep + (rnd() - 0.5) * jit;
				v.z = v.z * keep + (rnd() - 0.5) * jit;
				if (rnd() < (0.04 + 0.4 * depth * depth) * dense * step)
				{
					double rx = rnd() * 2 - 1, ry = rnd() * 2 - 1.3, rz = rnd() * 2 - 1;
					double rl = hypot3(rx, ry, rz); if (rl == 0) rl = 1;
					rx /= rl; ry /= rl; rz /= rl;
					const double ux = v.x / speed, uy = v.y / speed, uz = v.z / speed, mix = 0.5 + rnd() * 0.4;
					const double nx = ux * (1 - mix) + rx * mix, ny = uy * (1 - mix) + ry * mix, nz = uz * (1 - mix) + rz * mix;
					double nl = hypot3(nx, ny, nz); if (nl == 0) nl = 1;
					const double ns = speed * (0.2 + rnd() * 0.35);
					v.x = (nx / nl) * ns; v.y = (ny / nl) * ns; v.z = (nz / nl) * ns;
					st.spinMul *= 0.2;
					event = "branch";
				}
			}
			return event;
		}

		// One rolling step near the cup (physics.js cup). Returns "holed", "lipout" or "".
		std::string Cup(V3& p, V3& v, const V3& pin, bool& over, double dt, const std::function<double()>& rnd)
		{
			const double G = of::G;
			const double dx = p.x - pin.x, dz = p.z - pin.z, d = std::hypot(dx, dz);
			if (d > 0.3) { over = false; return ""; }
			const double speed = std::hypot(v.x, v.z);
			const double rim = CUP_RADIUS + BALL_RADIUS * 0.6;
			if (d >= CUP_RADIUS)
			{
				over = false;
				if (d < rim && speed > 1e-4)
				{
					const double overlap = (rim - d) / (rim - CUP_RADIUS);
					const double a = G * 0.35 * overlap;
					v.x -= (dx / d) * a * dt; v.z -= (dz / d) * a * dt;
				}
				return "";
			}
			if (over) return "";
			over = true;
			if (speed < 0.05) return "holed";
			const double ux = v.x / speed, uz = v.z / speed;
			const double along = dx * ux + dz * uz;
			const double b = std::abs(dx * uz - dz * ux);
			const double chord = -along + std::sqrt(std::max(0.0, CUP_RADIUS * CUP_RADIUS - b * b)) - BALL_RADIUS * 0.5;
			const double tOver = std::max(0.0, chord) / speed;
			const double drop = 0.5 * G * tOver * tOver;
			if (drop >= BALL_RADIUS * 1.2) return "holed";
			double side = sign(dx * uz - dz * ux); if (side == 0) side = 1;
			const double turn = side * (0.25 + 0.9 * (b / CUP_RADIUS)) * (0.6 + 0.4 * rnd());
			const double c = std::cos(turn), s = std::sin(turn), keep = 0.45 + 0.2 * (b / CUP_RADIUS);
			const double nx = ux * c - uz * s, nz = ux * s + uz * c;
			v.x = nx * speed * keep; v.z = nz * speed * keep;
			return "lipout";
		}
	}

	const char* SurfaceName(Surface s)
	{
		switch (s)
		{
		case Surface::Water: return "water"; case Surface::Green: return "green"; case Surface::Fringe: return "fringe";
		case Surface::Fairway: return "fairway"; case Surface::Tee: return "tee"; case Surface::FirstCut: return "firstcut";
		case Surface::Rough: return "rough"; case Surface::DeepRough: return "deeprough"; case Surface::Native: return "native";
		case Surface::Path: return "path"; case Surface::Sand: return "sand"; case Surface::Oob: return "oob";
		default: return "";
		}
	}

	Surface SurfaceFromName(const std::string& name)
	{
		static const std::map<std::string, Surface> m = {
			{ "water", Surface::Water }, { "green", Surface::Green }, { "fringe", Surface::Fringe }, { "fairway", Surface::Fairway },
			{ "tee", Surface::Tee }, { "firstcut", Surface::FirstCut }, { "rough", Surface::Rough }, { "deeprough", Surface::DeepRough },
			{ "native", Surface::Native }, { "path", Surface::Path }, { "sand", Surface::Sand }, { "oob", Surface::Oob },
		};
		auto it = m.find(name);
		return it != m.end() ? it->second : Surface::None;
	}

	LieEffect LieEffectFor(Surface lie)
	{
		switch (lie)
		{
		case Surface::Tee: case Surface::Fairway: case Surface::Green: return { 1, 1 };
		case Surface::Fringe: return { 0.99, 0.95 };
		case Surface::FirstCut: return { 0.97, 0.88 };
		case Surface::DeepRough: return { 0.78, 0.45 };
		case Surface::Native: return { 0.8, 0.45 };
		case Surface::Path: return { 1, 0.9 };
		case Surface::Sand: return { 0.85, 0.8 };
		default: return { 0.9, 0.62 }; // rough (and anything unknown)
		}
	}

	ShotResult SimulateShot(const Course& course, const V3& origin, const Shot& shot, const ShotOptions& opts)
	{
		const double DT = of::DT, RPM = of::RAD_PER_RPM;
		std::function<double()> rnd = opts.random ? opts.random : []() { return 0.5; };
		const double tempC = opts.tempC;
		const double back = shot.backSpin, side = shot.sideSpin;
		const of::Launch launch = of::ResolveLaunch(shot.speedMph, shot.vla, std::hypot(back, side));
		of::Env env;
		env.rho = of::AirDensity(opts.altitudeM, tempC, opts.humidity, opts.pressureHpa > 0 ? opts.pressureHpa * 100 : 0);
		env.mu = of::AirViscosity(tempC);
		env.dragScale = launch.dragScale; env.liftScale = launch.liftScale; env.launchDeg = launch.launchDeg;
		const double wet = std::min(1.0, std::max(0.0, opts.wet));
		const double greenRolling = of::GreenRollingFriction(std::max(6.0, opts.stimp - 1.8 * wet));
		std::map<Surface, of::SurfaceParams> settings;
		auto surfaceParams = [&](Surface surf) -> const of::SurfaceParams& {
			auto it = settings.find(surf);
			if (it != settings.end()) return it->second;
			of::SurfaceParams sp = SurfaceFor(surf);
			if (surf == Surface::Green) sp.rollingFriction = greenRolling;
			if (wet != 0)
			{
				sp.restitutionScale = sp.restitutionScale * (1 - 0.35 * wet);
				sp.kineticFriction = sp.kineticFriction * (1 + 0.15 * wet);
				sp.rollingFriction = sp.rollingFriction * (surf == Surface::Green ? 1 : 1 + 0.6 * wet);
				sp.grassViscosity = sp.grassViscosity * (1 + 0.8 * wet);
			}
			return settings[surf] = sp;
		};
		const double heading = opts.heading + (shot.hla * kPi) / 180;
		const double vla = (shot.vla * kPi) / 180;
		const double speed = shot.speedMph * MPH;
		const double fx = std::sin(heading), fz = std::cos(heading);
		V3 v = vec(speed * std::cos(vla) * fx, speed * std::sin(vla), speed * std::cos(vla) * fz);
		V3 spin = vec(-back * RPM * fz, side * RPM, back * RPM * fx);
		const double tilt = std::atan2(side, back);
		const double wf = (opts.windFromDeg * kPi) / 180;
		const double wm = opts.windMps;
		const V3 wind = vec(-std::sin(wf) * wm, 0, -std::cos(wf) * wm);
		V3 windH = vec(0, 0, 0);

		V3 p = origin;
		ShotResult res;
		std::vector<PathPoint>& points = res.points;
		points.push_back(PathPoint{ 0, p.x, p.y, p.z, 0, false });
		double t = 0, apex = p.y;
		bool air = true, firstImpact = true;
		int bounces = 0;
		double impactRpm = 0;
		const Surface startSurf = course.SurfaceAt(origin.x, origin.z);
		if ((startSurf == Surface::Green || startSurf == Surface::Fringe) && shot.vla < 6 && shot.speedMph < 25)
		{
			air = false;
			const V3 n = course.NormalAt(origin.x, origin.z);
			const double vn = v.x * n.x + v.y * n.y + v.z * n.z;
			v.x -= vn * n.x; v.y -= vn * n.y; v.z -= vn * n.z;
			spin = of::LaunchSpin(v, spin, n);
			impactRpm = hypot3(spin.x, spin.y, spin.z) / RPM;
		}
		std::string result = "rest";
		V3 lastDry = p;
		const bool hasPin = opts.hasPin;
		const V3 pin = opts.pin;
		double sampleAcc = 0;

		auto push = [&]() { points.push_back(PathPoint{ t, p.x, p.y, p.z, hypot3(spin.x, spin.y, spin.z), air }); };
		auto scaleSpin = [&](double k) { spin.x *= k; spin.y *= k; spin.z *= k; };
		auto alongGround = [&]() {
			const V3 n = course.NormalAt(p.x, p.z);
			const double vn = v.x * n.x + v.y * n.y + v.z * n.z;
			v.x -= vn * n.x; v.y -= vn * n.y; v.z -= vn * n.z;
		};
		bool cupOver = false;
		const Trees* trees = opts.trees;
		const Collider* collider = opts.collider;
		if (collider) const_cast<Collider*>(collider)->SetPin(hasPin ? &pin : nullptr);
		V3 prev = vec(0, 0, 0);
		TreeState treeState;
		auto treeStep = [&]() {
			if (!trees) return;
			const std::string ev = TreeInteract(*trees, p, v, DT, treeState, !collider, rnd);
			if (!ev.empty()) treeState.hits.push_back(ev);
			if (treeState.spinMul != 1) { scaleSpin(treeState.spinMul); treeState.spinMul = 1; }
		};
		auto hitObstacle = [&](const Hit& hit) {
			const V3& n = hit.normal;
			const Obstacle k = ObstacleFor(hit.kind);
			const double vn = v.x * n.x + v.y * n.y + v.z * n.z;
			if (vn < 0)
			{
				const double e = k.e + rnd() * k.eVar;
				v.x -= (1 + e) * vn * n.x; v.y -= (1 + e) * vn * n.y; v.z -= (1 + e) * vn * n.z;
				v.x *= k.keep; v.y *= k.keep; v.z *= k.keep;
				scaleSpin(k.spin);
			}
			p.x = hit.point.x + n.x * 0.005; p.y = hit.point.y + n.y * 0.005; p.z = hit.point.z + n.z * 0.005;
			treeState.hits.push_back(hit.kind);
		};
		auto dropIntoCup = [&]() {
			const double x0 = p.x, z0 = p.z, y0 = p.y;
			for (int k = 1; k <= 8; k++)
			{
				t += 0.02;
				const double f = k / 8.0;
				p.x = x0 + (pin.x - x0) * f; p.z = z0 + (pin.z - z0) * f;
				p.y = y0 - 0.03 * f * f;
				push();
			}
		};
		auto heightOr = [&](double x, double z, double fallback) { const double h = course.HeightAt(x, z); return std::isnan(h) ? fallback : h; };

		for (int step = 0; step < 120 * 60; step++)
		{
			if (air)
			{
				const double wk = of::WindFactor(p.y - origin.y + BALL_RADIUS);
				windH.x = wind.x * wk; windH.z = wind.z * wk;
				of::StepFlight(v, spin, env, windH, DT);
				prev = p;
				p.x += v.x * DT; p.y += v.y * DT; p.z += v.z * DT;
				t += DT;
				bool landed = false;
				double spent = 1;
				if (collider)
				{
					Hit hit;
					if (collider->Cast(prev, p, false, hit))
					{
						if (hit.kind != "terrain") { hitObstacle(hit); push(); continue; }
						const V3 n = course.NormalAt(hit.point.x, hit.point.z);
						if (v.x * n.x + v.y * n.y + v.z * n.z < 0)
						{
							const double stepL = hypot3(p.x - prev.x, p.y - prev.y, p.z - prev.z);
							spent = stepL > 0 ? std::min(1.0, hypot3(hit.point.x - prev.x, hit.point.y - prev.y, hit.point.z - prev.z) / stepL) : 0;
							p = hit.point; landed = true;
						}
					}
				}
				treeStep();
				apex = std::max(apex, p.y);
				double ground = course.HeightAt(p.x, p.z);
				if (std::isnan(ground)) { result = "oob"; push(); break; }
				if (!landed && p.y <= ground && v.y < 0)
				{
					landed = true;
					const double above = prev.y - heightOr(prev.x, prev.z, ground);
					spent = above > 0 ? std::min(1.0, above / (above - (p.y - ground))) : 0;
					p.x = prev.x + (p.x - prev.x) * spent; p.y = prev.y + (p.y - prev.y) * spent; p.z = prev.z + (p.z - prev.z) * spent;
					ground = heightOr(p.x, p.z, ground);
				}
				if (landed)
				{
					p.y = ground;
					t -= (1 - spent) * DT;
					const Surface surf = course.SurfaceAt(p.x, p.z);
					if (!res.hasCarryPoint) { res.hasCarryPoint = true; res.carryPoint = PathPoint{ t, p.x, p.y, p.z, 0, false }; }
					if (surf == Surface::Water) { result = "water"; res.hasWaterEntry = true; res.waterEntry = lastDry; push(); break; }
					if (hasPin && std::hypot(p.x - pin.x, p.z - pin.z) < CUP_RADIUS - BALL_RADIUS * 0.3 && hypot3(v.x, v.y, v.z) < 7)
					{
						dropIntoCup(); result = "holed"; break;
					}
					lastDry = p;
					const V3 n = course.NormalAt(p.x, p.z);
					const of::SurfaceParams& sp = surfaceParams(surf);
					if (firstImpact) impactRpm = hypot3(spin.x, spin.y, spin.z) / RPM;
					of::ResolveImpact(v, spin, n, sp, firstImpact, &launch.landing);
					firstImpact = false;
					bounces++;
					if (!of::HasRebound(v, n, sp) || bounces > 30) { air = false; alongGround(); }
					push();
					prev = p;
					p.x += v.x * (1 - spent) * DT; p.y += v.y * (1 - spent) * DT; p.z += v.z * (1 - spent) * DT;
					t += (1 - spent) * DT;
					const double h = course.HeightAt(p.x, p.z);
					if (!std::isnan(h) && (!air || p.y < h)) p.y = h;
					if (collider)
					{
						Hit wall;
						if (collider->Cast(prev, p, true, wall))
						{
							hitObstacle(wall);
							if (!air) { p.y = heightOr(p.x, p.z, p.y); alongGround(); }
						}
					}
					continue;
				}
			}
			else
			{
				const Surface surf = course.SurfaceAt(p.x, p.z);
				if (surf == Surface::Water) { result = "water"; res.hasWaterEntry = true; res.waterEntry = lastDry; push(); break; }
				if (surf == Surface::None) { result = "oob"; push(); break; }
				lastDry = p;
				const of::SurfaceParams& sp = surfaceParams(surf);
				const V3 n = course.NormalAt(p.x, p.z);
				of::StepGround(v, spin, n, sp, impactRpm, DT);
				if (of::CanRest(v, spin, n, sp))
				{
					if (hasPin && std::hypot(p.x - pin.x, p.z - pin.z) < CUP_RADIUS) { dropIntoCup(); result = "holed"; break; }
					v = vec(0, 0, 0); push(); break;
				}
				prev = p;
				p.x += v.x * DT; p.z += v.z * DT;
				treeStep();
				const double h = course.HeightAt(p.x, p.z);
				if (std::isnan(h)) { result = "oob"; push(); break; }
				p.y = h;
				if (collider && (v.x != 0 || v.z != 0))
				{
					Hit hit;
					if (collider->Cast(prev, p, true, hit))
					{
						if (hit.kind == "pole" && hasPin)
						{
							const double sp2 = std::hypot(v.x, v.z);
							const double off = std::abs((prev.x - pin.x) * v.z - (prev.z - pin.z) * v.x) / (sp2 != 0 ? sp2 : 1);
							if (sp2 < 1.6 - 1.4 * std::min(1.0, off / CUP_RADIUS)) { dropIntoCup(); result = "holed"; break; }
						}
						hitObstacle(hit);
						p.y = heightOr(p.x, p.z, p.y);
						alongGround();
						if (hypot3(v.x, v.y, v.z) < 0.1) { v = vec(0, 0, 0); push(); break; }
					}
				}
				t += DT;
				if (hasPin)
				{
					if (Cup(p, v, pin, cupOver, DT, rnd) == "holed") { dropIntoCup(); result = "holed"; break; }
				}
			}
			sampleAcc += DT;
			if (sampleAcc >= 1.0 / 60 - 1e-9) { sampleAcc = 0; push(); }
		}
		if (points.back().t != t) push();
		res.result = result;
		res.rest = p;
		auto hd = [&](double x, double z) { return std::hypot(x - origin.x, z - origin.z); };
		const double ox = std::sin(opts.heading), oz = std::cos(opts.heading);
		res.carry = res.hasCarryPoint ? hd(res.carryPoint.x, res.carryPoint.z) : hd(p.x, p.z);
		res.total = hd(p.x, p.z);
		res.offline = (p.x - origin.x) * oz - (p.z - origin.z) * ox;
		res.apex = apex - origin.y;
		res.flightTime = res.hasCarryPoint ? res.carryPoint.t : t;
		res.duration = t;
		res.treeHits = treeState.hits;
		res.spinTilt = tilt;
		res.regime = launch.regime;
		return res;
	}
}
