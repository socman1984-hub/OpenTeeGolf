// OpenTeeGolf ball physics in C++. The ball on the course (SimulateShot) is public/js/physics.js, kept
// line for line; OpenFairway (namespace of) is the newer OpenFairwayPhysics of uopengolf, ahead of the web
// version's openfairway.js. See OTGBallPhysics.h.

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

	// ================================================================== OpenFairway
	// The OpenFairwayPhysics plugin of uopengolf at e8c318e3 (MIT; see OTGBallPhysics.h), in plain C++ with
	// doubles: Private/OpenFairway/{SpinRatioData.h, BallData.h, FlightAerodynamics, FlightStep, Atmosphere,
	// WindProfile, Impact, GroundRoll, Surfaces, BallProfile}.cpp. The game's surfaces (rough, deep rough,
	// path ...) are set on top in SurfaceFor below.
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
			const double MIN_AERO_SPEED = 0.5;
			const double REST_SPEED = 0.05;                 // GolfPhysics::RestSpeedMps
			const double STOP_SPIN = 8.0;                   // GroundRollDetail::StopSpinRadPerS
			const double PUTT_RAMP_START = 12, PUTT_RAMP_END = 18;   // GolfPhysics::PuttSpeedRamp*

			// (InterpolateHeld: piecewise-linear, held at the ends)
			template <int N>
			double held(const double (&x)[N], const double (&y)[N], double v)
			{
				if (v <= x[0]) return y[0];
				for (int i = 1; i < N; i++) if (v < x[i]) return lerp(y[i - 1], y[i], (v - x[i - 1]) / (x[i] - x[i - 1]));
				return y[N - 1];
			}

			// ---- BallData.h: measured drag of a modern ball (Lyu et al. 2020, digitized), near 2250 rpm
			const double BD_RE[8] = { 50000, 65000, 75000, 100000, 125000, 150000, 175000, 200000 };
			const double BD_CD[8] = { .49, .33, .25, .215, .18, .17, .19, .20 };
			double ballDrag(double re) { return held(BD_RE, BD_CD, re); }

			// ---- SpinRatioData.h: the flight table, drag and lift against spin ratio W
			const double SR_W[10] = { 0.06, 0.08, 0.10, 0.12, 0.15, 0.16, 0.22, 0.32, 0.50, 0.80 };
			const double SR_CD[10] = { 0.1895485, 0.2073769, 0.2251196, 0.2384511, 0.2461, 0.2793, 0.2985, 0.3244, 0.3605, 0.3605 };
			const double SR_CL[10] = { 0.1302894, 0.1449717, 0.1793615, 0.1977254, 0.2251639, 0.2387429, 0.2852, 0.3319, 0.3528, 0.413 };
			const double CRISIS_SCALE = 0.6838, CRISIS_START_RE = 100000;
			const double MIN_RE = 35000, MAX_RE = 215000, MIN_W = 0.06, MAX_W = 0.80;

			// ---- profiles (Profiles.h: FBounceProfile, FRolloutProfile; BallProfile: GroundDragScale)
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
				const double slopeResistanceRatio = 1.6;
			}
			const double SPIN_DECAY = 2.0e-5;                // FFlightProfile::SpinDecayCoefficient
			const double SAND_PLOW_RATIO = 2.5, SAND_ROLLING = 0.45, GREEN_VISCOSITY = 0.0009;

			// ---- FlightAerodynamics.cpp GroundDragCurve: the old flight model's drag, kept for a ball on the ground
			namespace GD
			{
				const double capRe = 200000, a = 1.1948, b = -0.0000209661, c = 1.42472e-10, d = -3.14383e-16;
				const double highReCap = 0.2, highReBlendStart = 175000, lowReFloor = 0.38, lowReBlendStart = 30000, at50k = 0.4632;
				const double spinCoeff = 4.0, spinMax = 1.2, spinHighMax = 1.03, spinUltraMax = 1.21;
				const double highSrStart = 0.3, highSrEnd = 0.48, reliefReFullMax = 90000, reliefReZero = 105000;
				const double ultraSrStart = 0.57, ultraSrEnd = 0.77, progStart = 0.33, progEnd = 0.5, progBoost = 0.25;
				double drag(double re)
				{
					if (re > capRe) return highReCap;
					if (re >= 50000)
					{
						const double poly = a + b * re + c * re * re + d * re * re * re;
						return re <= highReBlendStart ? poly : lerp(poly, highReCap, smoothRange(re, highReBlendStart, capRe));
					}
					if (re <= lowReBlendStart) return lowReFloor;
					return lerp(lowReFloor, at50k, smoothRange(re, lowReBlendStart, 50000));
				}
				double spinMult(double s, double re)
				{
					if (s <= 0) return 1;
					const double high = smoothRange(s, highSrStart, highSrEnd);
					const double relief = 1 - smoothRange(re, reliefReFullMax, reliefReZero);
					double cap = lerp(spinMax, spinHighMax, high * relief);
					cap += progBoost * smoothRange(s, progStart, progEnd);
					cap = lerp(cap, spinUltraMax, smoothRange(s, ultraSrStart, ultraSrEnd));
					return std::min(1 + spinCoeff * s * s, cap);
				}
			}

			// ---- Surfaces.cpp: the turf recalibration (fitted to a 124-shot launch-monitor roll-out on fairway)
			const double TURF_LANDING_ANGLE_LOSS = 0.665, TURF_LANDING_SPIN_LOSS = 0.464;
			const double TURF_ROLLING = 0.0469, TURF_VISCOSITY = 0.0242, TURF_FORM_DRAG = 0.0086;
			const double TURF_STRUCK_VISCOSITY = 0.26, TURF_STRUCK_FORM_DRAG = 0.69;
			const double PORTED_FAIRWAY_ROLLING = 0.05, PORTED_FAIRWAY_VISCOSITY = 0.0017;

			std::map<std::string, SurfaceParams> MakePresets()
			{
				std::map<std::string, SurfaceParams> p;
				p["fairway"] = Turf(MakeSurface(0.5, 0.05, 0.0017, 0.29, 0.78));
				p["fairwaySoft"] = Turf(MakeSurface(0.56, 0.07, 0.0024, 0.32, 0.92));
				p["rough"] = Turf(MakeSurface(0.62, 0.095, 0.0032, 0.35, 0.7));
				p["firm"] = Turf(MakeSurface(0.3, 0.03, 0.001, 0.25, 0.6));
				p["fringe"] = Turf(MakeSurface(0.56, 0.0725, 0.00245, 0.29, 0.78));
				SurfaceParams green = MakeSurface(0.58, std::numeric_limits<double>::quiet_NaN(), GREEN_VISCOSITY, 0.36, 1.12);
				green.spinbackThetaBoostMax = 0.12; green.spinbackSpinStartRpm = 3500; green.spinbackSpinEndRpm = 5500;
				green.spinbackSpeedStartMps = 8; green.spinbackSpeedEndMps = 20; green.rollingSpinScale = 0;
				p["green"] = green;
				SurfaceParams sand = MakeSurface(0.65, SAND_ROLLING, 0.012, 0.45, 1.0);
				sand.restitutionScale = 0.6; sand.impactRetentionScale = 0.25; sand.impactPlowRatio = SAND_PLOW_RATIO;
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

		SurfaceParams Turf(SurfaceParams s)
		{
			// (Surfaces.cpp RecalibrateTurf: each turf keeps its ratio of grass drag and rolling friction to the
			// ported fairway's; the landing law and the struck-along-the-ground drag are the same on all)
			const double dragRatio = s.grassViscosity / PORTED_FAIRWAY_VISCOSITY;
			s.rollingFriction = TURF_ROLLING * s.rollingFriction / PORTED_FAIRWAY_ROLLING;
			s.grassViscosity = TURF_VISCOSITY * dragRatio;
			s.grassFormDrag = TURF_FORM_DRAG * dragRatio;
			s.landingAngleLoss = TURF_LANDING_ANGLE_LOSS;
			s.landingSpinLoss = TURF_LANDING_SPIN_LOSS;
			s.rollingSpinScale = 0;
			s.slidingSpinScale = 0;
			s.struckViscosityScale = TURF_STRUCK_VISCOSITY;
			s.struckFormDragScale = TURF_STRUCK_FORM_DRAG;
			return s;
		}

		const SurfaceParams& Preset(const char* name)
		{
			static const std::map<std::string, SurfaceParams> presets = MakePresets();
			auto it = presets.find(name);
			return it != presets.end() ? it->second : presets.at("rough");
		}

		// ---- green speed (Surfaces.cpp GreenSpeed): the rolling friction that, with the green's grass drag and
		// the ground air drag in standard air, stops a stimpmeter ball at the stimp distance
		namespace
		{
			const double STIMPMETER_MPS = 1.83;
			double speedDragPerSecond(double viscosity) { return (10 * kPi * RADIUS * viscosity) / MASS; }
			// (the distance a ball rolling at Speed goes with a0 + k v + q v^2 against it: Simpson's rule)
			double rollMeters(double speed, double a0, double k)
			{
				const double rollRe = 1.225 * STIMPMETER_MPS * RADIUS * 2 / 1.789e-5;
				const double q = 5.0 / 7.0 * 0.5 * 1.225 * GD::drag(rollRe) * GD::spinMult(1, rollRe) * AREA / MASS;
				const int n = 128;
				const double h = speed / n;
				double sum = 0;
				for (int i = 0; i <= n; i++)
				{
					const double v = i * h;
					const double drag = v >= MIN_AERO_SPEED ? q * v * v : 0;
					sum += (i == 0 || i == n ? 1 : i % 2 ? 4 : 2) * (v / (a0 + k * v + drag));
				}
				return sum * h / 3;
			}
		}

		double GreenRollingFriction(double stimpFt, double viscosity)
		{
			if (viscosity < 0) viscosity = GREEN_VISCOSITY;
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
			if (viscosity < 0) viscosity = GREEN_VISCOSITY;
			const double a0 = (5 * G * GreenRollingFriction(stimpFt, viscosity)) / 7, k = speedDragPerSecond(viscosity);
			double lo = 0, hi = 30;
			for (int i = 0; i < 40; i++)
			{
				const double mid = 0.5 * (lo + hi);
				if (rollMeters(mid, a0, k) > distanceM) hi = mid; else lo = mid;
			}
			return 0.5 * (lo + hi);
		}

		// ---- air (Atmosphere.cpp, WindProfile.cpp)
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
			if (!(std::isfinite(referenceM) && std::isfinite(roughnessM) && roughnessM > 0 && referenceM > roughnessM)) return 1;
			if (!std::isfinite(heightM) || heightM <= roughnessM) return 0;
			const double lz = std::log(roughnessM);
			return (std::log(std::min(heightM, 100.0)) - lz) / (std::log(referenceM) - lz);
		}

		// ---- aerodynamics (FlightAerodynamics.cpp, SpinRatioData.h)
		double FlightLift(double spinRatio)
		{
			const double w = std::max(spinRatio, 0.0);
			return w < SR_W[0] ? SR_CL[0] * w / SR_W[0] : held(SR_W, SR_CL, w);
		}

		double FlightDrag(double re, double spinRatio)
		{
			const double crisis = re < CRISIS_START_RE ? std::max(ballDrag(re) - ballDrag(CRISIS_START_RE), 0.0) : 0.0;
			return held(SR_W, SR_CD, std::max(spinRatio, 0.0)) + CRISIS_SCALE * crisis;
		}

		double GroundDragCoefficient(double re, double spinRatio) { return GD::drag(re) * GD::spinMult(spinRatio, re); }

		Aero AeroSample(const V3& air, const V3& spin, const Env& env)
		{
			Aero out;
			out.speed = len(air);
			out.spinDecay = (std::max(0.0, SPIN_DECAY) * out.speed) / RADIUS;
			if (out.speed < MIN_AERO_SPEED) return out;
			out.spinRatio = (len(spin) * RADIUS) / out.speed;
			out.reynolds = (env.rho * out.speed * RADIUS * 2) / env.mu;
			out.cl = FlightLift(out.spinRatio);
			out.cd = FlightDrag(out.reynolds, out.spinRatio);
			out.outside = out.reynolds < MIN_RE || out.reynolds > MAX_RE || out.spinRatio < MIN_W || out.spinRatio > MAX_W;
			return out;
		}

		V3 WindAt(const Env& env, double heightM)
		{
			return scale(env.wind, WindFactor(heightM, env.windReferenceM, env.windRoughnessM));
		}

		// (FlightStep.cpp: one coupled RK4 step of position, velocity and spin, the wind read at the ball
		// centre's height above the launch ground at every stage)
		Aero StepFlight(V3& p, V3& v, V3& spin, const Env& env, double dt)
		{
			struct S { V3 p, v, w; };
			Aero first;
			bool sampled = false;
			auto deriv = [&](const S& s) {
				const V3 air = add(s.v, WindAt(env, s.p.y - env.groundY + RADIUS), -1);
				const Aero a = AeroSample(air, s.w, env);
				if (!sampled) { first = a; sampled = true; }
				V3 acc = vec(0, -G, 0);
				if (a.speed >= MIN_AERO_SPEED)
				{
					const double q = 0.5 * env.rho * AREA * a.speed / MASS;
					acc = add(acc, air, -q * a.cd * env.rainDragScale);
					const double wl = len(s.w);
					if (wl > 0.1) acc = add(acc, cross(s.w, air), q * a.cl / wl);
				}
				return S{ s.v, acc, scale(s.w, -a.spinDecay) };
			};
			auto adv = [](const S& s, const S& k, double h) { return S{ add(s.p, k.p, h), add(s.v, k.v, h), add(s.w, k.w, h) }; };
			const S s0{ p, v, spin };
			const S k1 = deriv(s0);
			const S k2 = deriv(adv(s0, k1, dt * 0.5));
			const S k3 = deriv(adv(s0, k2, dt * 0.5));
			const S k4 = deriv(adv(s0, k3, dt));
			auto mix = [&](const V3& a, const V3& b, const V3& c, const V3& d) { return vec((a.x + 2 * b.x + 2 * c.x + d.x) * dt / 6, (a.y + 2 * b.y + 2 * c.y + d.y) * dt / 6, (a.z + 2 * b.z + 2 * c.z + d.z) * dt / 6); };
			p = add(p, mix(k1.p, k2.p, k3.p, k4.p));
			v = add(v, mix(k1.v, k2.v, k3.v, k4.v));
			spin = add(spin, mix(k1.w, k2.w, k3.w, k4.w));
			return first;
		}

		// ---- the landing response (BallProfile.cpp: buckets by launch angle and backspin, blended)
		namespace
		{
			const double LAUNCH_ANCHORS[6] = { 5, 14, 21.5, 27.5, 31.5, 36.5 };
			const double SPIN_ANCHORS[5] = { 1250, 3250, 4750, 6500, 8500 };
			struct AW { double a[2], w[2]; };
			template <int N>
			AW bracket(const double (&anchors)[N], double x)
			{
				if (x <= anchors[0]) return { { anchors[0], anchors[0] }, { 1, 0 } };
				for (int i = 1; i < N; i++)
					if (x < anchors[i]) { const double t = (x - anchors[i - 1]) / (anchors[i] - anchors[i - 1]); return { { anchors[i - 1], anchors[i] }, { 1 - t, t } }; }
				return { { anchors[N - 1], anchors[N - 1] }, { 1, 0 } };
			}
			Landing landingRow(const std::string& bucket)
			{
				if (bucket == "release") return { 0.5, 0, 8, 20 };
				if (bucket == "check") return { 0.85, 0, 8, 20 };
				if (bucket == "spinback") return { 1, 1.2, 8, 20 };
				return { 0, 0, 0, 0 };
			}
		}

		std::string LandingBucket(double vla, double rpm)
		{
			if (rpm < 4000) return "run";
			if (rpm < 5500) return "release";
			if (rpm < 7500) return "check";
			return vla < 18 ? "check" : "spinback";
		}

		Launch ResolveLaunch(double vlaDeg, double backSpinRpm)
		{
			const double rpm = std::max(0.0, backSpinRpm);   // (only backspin bites at landing)
			Launch out;
			const AW l = bracket(LAUNCH_ANCHORS, vlaDeg), s = bracket(SPIN_ANCHORS, rpm);
			for (int a = 0; a < 2; a++)
				for (int b = 0; b < 2; b++)
				{
					const double w = l.w[a] * s.w[b];
					const Landing r = landingRow(LandingBucket(l.a[a], s.a[b]));
					out.landing.spinBiteScale += r.spinBiteScale * w; out.landing.maxSpinbackMps += r.maxSpinbackMps * w;
					out.landing.speedRampStartMps += r.speedRampStartMps * w; out.landing.speedRampEndMps += r.speedRampEndMps * w;
				}
			out.bucket = LandingBucket(vlaDeg, rpm);
			return out;
		}

		// ---- impact (Impact.cpp)
		namespace
		{
			double landingWeight(double vn) { return rampUp(vn, B::flightRetentionRampStartMps, B::flightRetentionRampEndMps); }
			double flightRetention(double vn, double rpm)
			{
				const double landing = B::flightTangentialRetentionBase * clamp(1 - rpm / B::flightSpinFactorDivisor, B::flightSpinFactorMin, 1);
				const double w = landingWeight(vn);
				return w >= 1 ? landing : lerp(1, landing, w);
			}
			bool usesTurfLanding(const SurfaceParams& s) { return s.landingAngleLoss > 0 || s.landingSpinLoss > 0; }
			// (a turf impact keeps exp(-k vn/vt) of its tangential speed, and loses backspin's grip on it - bounded
			// by the contact's Coulomb limit)
			double turfRetention(const V3& v, const V3& w, const V3& n, double incoming, double gripLimit, const SurfaceParams& s)
			{
				const V3 t = add(v, n, incoming);
				const double ts = len(t);
				const double backspin = ts >= 0.01 ? std::max(0.0, dot(w, cross(scale(t, 1 / ts), n))) : 0.0;
				const double speed = std::max(ts, 0.01);
				const double grip = std::min(s.landingSpinLoss * RADIUS * backspin, std::max(0.0, gripLimit));
				return std::exp(-s.landingAngleLoss * incoming / speed) * std::max(0.0, 1 - grip / speed);
			}
			// (the backspin across the ball's travel: neither sidespin nor topspin checks a ball)
			struct ImpactSpin { double rpm, ratioRad; };
			ImpactSpin readImpactSpin(const V3& v, const V3& w, const V3& n, double vn)
			{
				const V3 t = add(v, n, -vn);
				const double ts = len(t);
				if (ts < 0.01) { const double plane = len(tangent(w, n)); return { plane / RAD_PER_RPM, plane }; }
				const double backspin = dot(w, cross(scale(t, 1 / ts), n));
				return { std::max(0.0, backspin) / RAD_PER_RPM, std::abs(backspin) };
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
			const ImpactSpin is = readImpactSpin(v, spin, n, vn);
			const double rpm = is.rpm;
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
				const double angle = std::atan2(incoming, len(add(v, n, -vn)));
				spinback = 1 + (spinback - 1) * clamp((angle - effective) / 0.1, 0, 1);
			}
			const double maxJt = surf.kineticFriction * std::max(0.0, spinback) * jn;
			const double jtl = len(jt);
			if (jtl > maxJt && jtl > 0) jt = scale(jt, maxJt / jtl);

			V3 nv = add(add(v, n, jn / MASS), jt, 1 / MASS);
			V3 nw = add(spin, cross(arm, jt), 1 / INERTIA);
			const double speed = len(v);
			const bool turf = usesTurfLanding(surf);
			double keep = turf ? turfRetention(v, spin, n, incoming, maxJt / MASS, surf)
				: firstImpact ? flightRetention(incoming, rpm)
				: speed > 0.1 && B::rolloutSpinRatioThreshold > 0
				? lerp(B::rolloutLowSpinRetention, B::rolloutHighSpinRetention, std::min((is.ratioRad * RADIUS) / speed / B::rolloutSpinRatioThreshold, 1.0))
				: B::rolloutHighSpinRetention;
			const double normalShare = clamp((incoming * incoming) / std::max(dot(v, v), 1e-12), 0, 1);
			// (landingKeepScale: the game's own, a longer grass grabbing a landing ball - SurfaceFor)
			keep = clamp(keep * surf.landingKeepScale * lerp(1, surf.impactRetentionScale, normalShare), 0, 1);
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
			if (firstImpact && !turf && response && response->spinBiteScale > 0) landingBite(v, spin, nv, nw, n, surf, *response);
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
			const double rolling = (1 - landingWeight(std::abs(vn))) * (1 - rampUp(len(t), PUTT_RAMP_START, PUTT_RAMP_END));
			return rolling <= 0 ? vec(0, 0, 0) : scale(cross(n, t), rolling / RADIUS);
		}

		double StruckAlongGroundWeight(double tangentSpeed) { return rampUp(tangentSpeed, PUTT_RAMP_START, PUTT_RAMP_END); }

		SurfaceParams StruckAlongGround(const SurfaceParams& s, double weight)
		{
			const double w = clamp(weight, 0, 1);
			if (w <= 0) return s;
			SurfaceParams out = s;
			out.grassViscosity *= lerp(1, s.struckViscosityScale, w);
			out.grassFormDrag *= lerp(1, s.struckFormDragScale, w);
			return out;
		}

		double RolloutSpinRpm(const V3& spin, const V3& n) { return len(tangent(spin, validNormal(n))) / RAD_PER_RPM; }

		// ---- ground (GroundRoll.cpp)
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
			// (a slowing ball settles down into longer grass, which grabs it harder: the surface's rolling
			// friction plus its settle friction, faded in below its settle speed - so a fairway lets a ball run
			// but holds one on a slope instead of letting it creep down for a minute)
			double rollingAt(const SurfaceParams& surf, double speed)
			{
				if (surf.settleFriction <= 0 || surf.settleSpeed <= 0) return surf.rollingFriction;
				return surf.rollingFriction + surf.settleFriction * (1 - smooth01(speed / surf.settleSpeed));
			}
			V3 resistContactSpin(const V3& w, const V3& n, double rollingFriction, double load, double dt)
			{
				const double ns = dot(w, n);
				const double remaining = std::max(0.0, std::abs(ns) - (rollingFriction * load * RADIUS / INERTIA) * dt);
				return add(w, n, (ns < 0 ? -remaining : remaining) - ns);
			}
		}

		void StepGround(V3& v, V3& spin, const V3& nIn, const SurfaceParams& surf, double impactRpm, double dt, const Env& env)
		{
			const V3 n = validNormal(nIn);
			const V3 vel = tangent(v, n);
			const V3 slip = tangent(add(vel, cross(spin, scale(n, -RADIUS))), n);
			const double load = MASS * G * std::max(0.0, n.y);
			const V3 slope = add(vec(0, -MASS * G, 0), n, load);
			const double slopeL = len(slope);
			const double rolling = rollingAt(surf, len(vel));
			const double staticCap = rolling * R::slopeResistanceRatio * load;
			const V3 rollingSpin = tangent(spin, n);
			if (dot(vel, vel) < REST_SPEED * REST_SPEED && dot(rollingSpin, rollingSpin) < STOP_SPIN * STOP_SPIN && slopeL <= staticCap)
			{
				v = vec(0, 0, 0); spin = vec(0, 0, 0);
				return;
			}
			if (dot(vel, vel) < 1e-4 && dot(slip, slip) < 1e-4 && slopeL <= staticCap)
			{
				v = vec(0, 0, 0);
				spin = resistContactSpin(dampenSpin(spin, surf.grassViscosity, dt), n, rolling, load, dt);
				return;
			}
			const double mult = SpinFrictionMultiplier(std::max(impactRpm, len(rollingSpin) / RAD_PER_RPM), len(vel));
			// grass drag, linear and speed-squared, integrated exactly
			const double speed = len(vel);
			const double k = 6 * kPi * RADIUS * surf.grassViscosity / MASS;
			const double decay = std::exp(-k * dt);
			const double dragIntegral = k > 1e-8 ? (1 - decay) / k : dt;
			V3 nv = scale(vel, decay / (1 + surf.grassFormDrag * speed * dragIntegral));
			// the air (the ball meets the wind one radius above its lie)
			const V3 tWind = tangent(WindAt(env, RADIUS), n);
			const V3 air = add(nv, tWind, -1);
			const double airSpeed = len(air);
			if (airSpeed >= MIN_AERO_SPEED)
			{
				const double re = env.rho * airSpeed * RADIUS * 2 / env.mu;
				const double sr = len(spin) * RADIUS / airSpeed;
				const double cd = GroundDragCoefficient(re, sr) * env.groundDragScale;
				const double rate = 0.5 * cd * env.rho * AREA / MASS;
				nv = add(tWind, air, 1 / (1 + rate * airSpeed * dt));
			}
			// rolling resistance, then the slope
			const double rollMult = 1 + (mult - 1) * surf.rollingSpinScale;
			const double rollDelta = rolling * rollMult * load / MASS * dt;
			const double dragged = len(nv);
			if (dragged > 0) nv = scale(nv, std::max(0.0, dragged - rollDelta) / dragged);
			nv = add(nv, slope, dt / MASS);
			V3 nw = resistContactSpin(dampenSpin(spin, surf.grassViscosity, dt), n, rolling, load, dt);
			// the contact: a Coulomb-capped impulse toward rolling without slipping
			const V3 arm = scale(n, -RADIUS);
			const V3 nslip = tangent(add(nv, cross(nw, arm)), n);
			V3 j = scale(nslip, (-2 * MASS) / 7);
			const double slideMult = 1 + (mult - 1) * surf.slidingSpinScale;
			const double cap = surf.kineticFriction * slideMult * load * dt;
			const double jl = len(j);
			if (jl > cap && jl > 0) j = scale(j, cap / jl);
			v = add(nv, j, 1 / MASS);
			spin = add(nw, cross(arm, j), 1 / INERTIA);
		}

		bool CanRest(const V3& v, const V3& spin, const V3& nIn, const SurfaceParams& surf)
		{
			if (dot(v, v) >= REST_SPEED * REST_SPEED) return false;
			const V3 n = validNormal(nIn);
			if (dot(tangent(spin, n), tangent(spin, n)) >= 0.5 * 0.5) return false;
			const V3 g = vec(0, -MASS * G, 0);
			return len(tangent(g, n)) <= rollingAt(surf, 0) * R::slopeResistanceRatio * MASS * G * std::max(0.0, n.y);
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

		// (physics.js SURFACES: longer grass cushions, grabs and stops the ball sooner; sand all but plugs
		// it; a cart path bounces it high and lets it run. The grass is OpenFairway's turf - of::Turf - each
		// cut keeping its own drag and grab on top)
		// (the settle frictions: a ball slowing to a stop settles into the grass. With them a fairway rolls
		// about 5.3 ft on the stimpmeter and holds a ball on a slope of up to about 6 degrees - steeper banks
		// still feed it down - the collar a little quicker than the fairway, the cut and the rough slower)
		const double FR_ROLL = 0.05, FR_VISC = 0.0019, FR_SET = 0.07, FR_SPD = 1.8;
		const double FW_SET = 0.1, FW_SPD = 2.0;
		const double FC_SET = 0.08, FC_SPD = 1.0;
		const double RO_SET = 0.12, RO_SPD = 1.0;
		const double DR_SET = 0.2;
		of::SurfaceParams SurfaceFor(Surface s)
		{
			auto with = [](of::SurfaceParams p, double restitution, double impactRetention, double landingKeep) {
				p.restitutionScale = restitution; p.impactRetentionScale = impactRetention; p.landingKeepScale = landingKeep; return p;
			};
			// (the grass a slowing ball settles into: see of::rollingAt)
			auto settle = [](of::SurfaceParams p, double friction, double speed) { p.settleFriction = friction; p.settleSpeed = speed; return p; };
			switch (s)
			{
			case Surface::Green: return of::Preset("green");
			case Surface::Fringe: return settle(with(of::Turf(of::MakeSurface(0.56, FR_ROLL, FR_VISC, 0.3, 0.85)), 0.92, 0.95, 1), FR_SET, FR_SPD);
			case Surface::Fairway: case Surface::Tee: return settle(of::Preset("fairway"), FW_SET, FW_SPD);
			case Surface::FirstCut: return settle(with(of::Turf(of::MakeSurface(0.58, 0.12, 0.0038, 0.33, 0.68)), 0.85, 0.85, 0.9), FC_SET, FC_SPD);
			case Surface::DeepRough: return settle(with(of::Turf(of::MakeSurface(0.72, 0.55, 0.018, 0.42, 0.35)), 0.38, 0.4, 0.4), DR_SET, 1.0);
			case Surface::Native: return settle(with(of::Turf(of::MakeSurface(0.74, 0.65, 0.022, 0.44, 0.3)), 0.33, 0.35, 0.35), DR_SET, 1.0);
			case Surface::Path:
			{
				of::SurfaceParams p = with(of::MakeSurface(0.22, 0.028, 0.0002, 0.25, 0.2), 2.8, 1, 1.7);
				p.hasBounce = true; p.firstImpactMinimumSpeed = 0.5; p.laterImpactMinimumSpeed = 1.0; p.laterImpactScale = 0.85; p.minimumBounceHeight = 0.02;
				return p;
			}
			case Surface::Sand:
			{
				of::SurfaceParams p = with(of::MakeSurface(0.65, 0.45, 0.02, 0.45, 1.0), 0.12, 0.15, 1);
				p.impactPlowRatio = 3.5;
				p.hasBounce = true; p.firstImpactMinimumSpeed = 0.15; p.laterImpactMinimumSpeed = 0.15; p.laterImpactScale = 0.6; p.minimumBounceHeight = 0.001;
				return p;
			}
			default: return settle(with(of::Turf(of::MakeSurface(0.66, 0.2, 0.007, 0.38, 0.5)), 0.62, 0.65, 0.75), RO_SET, RO_SPD); // rough, oob, anything else
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
		const of::Launch launch = of::ResolveLaunch(shot.vla, back);
		of::Env env;
		env.rho = of::AirDensity(opts.altitudeM, tempC, opts.humidity, opts.pressureHpa > 0 ? opts.pressureHpa * 100 : 0);
		env.mu = of::AirViscosity(tempC);
		env.groundY = origin.y;
		// Heavy rain: the drops the ball flies through, and the water on it, slow it a little more than
		// the air alone. Nothing in light rain; from moderate rain up a few percent more drag - a storm
		// costs a long shot a few yards of carry (the launch monitor already measures a wet clubface)
		env.rainDragScale = 1 + 0.03 * std::min(1.0, std::max(0.0, (opts.rainMm - 1.5) / 5.5));
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
		env.wind = vec(-std::sin(wf) * wm, 0, -std::cos(wf) * wm);   // (at 10 m, growing with height: of::WindAt)

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
			impactRpm = of::RolloutSpinRpm(spin, n);
		}
		// (a full shot struck into or along the ground - a topped shot - rolls with the turf's struck grass drag
		// for the rest of its way: OpenFairway's ShotSimulator)
		double struckWeight = 0;
		if (air)
		{
			const V3 n = course.NormalAt(origin.x, origin.z);
			const double vn = dot(v, n);
			if (vn <= 0) struckWeight = of::StruckAlongGroundWeight(std::sqrt(std::max(0.0, dot(v, v) - vn * vn)));
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
		// (things moving while the ball does: physics.js moverHit)
		auto moverHit = [&](Hit& out) {
			for (const auto& m : opts.movers) if (m && m(prev, p, t, out)) return true;
			return false;
		};
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
				prev = p;
				of::StepFlight(p, v, spin, env, DT);
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
				if (!opts.movers.empty()) { Hit mh; if (moverHit(mh)) { hitObstacle(mh); push(); continue; } }
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
					if (firstImpact) impactRpm = of::RolloutSpinRpm(spin, n);
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
				const of::SurfaceParams sp = struckWeight > 0 ? of::StruckAlongGround(surfaceParams(surf), struckWeight) : surfaceParams(surf);
				const V3 n = course.NormalAt(p.x, p.z);
				of::StepGround(v, spin, n, sp, impactRpm, DT, env);
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
				if (!opts.movers.empty() && (v.x != 0 || v.z != 0))
				{
					Hit mh;
					if (moverHit(mh)) { hitObstacle(mh); p.y = heightOr(p.x, p.z, p.y); alongGround(); }
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
		res.regime = launch.bucket;
		return res;
	}
}
