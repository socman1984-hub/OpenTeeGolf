// OpenTeeGolf ball physics in C++: the game's public/js/openfairway.js and public/js/physics.js,
// line for line, so a shot plays the same in the Unreal game as in the web version. Plain C++17
// with no Unreal types: the game and the parity test (unreal/tests/physics) build the same files.
//
// OpenFairway (namespace of) is the OpenFairwayPhysics plugin of uopengolf
// (https://github.com/digitalhand/uopengolf, MIT, Copyright (c) 2026 uopengolf contributors) at revision
// e8c318e3 - its fitted spin-ratio flight, turf landing and roll - translated to plain C++; its impact,
// ground and rest equations come from ogolf-physics' OpenFairway. See NOTICE.md. Since that revision the
// game's physics is ahead of the web version's (openfairway.js, an older OpenFairway): the parity test
// (unreal/tests/physics/parity.mjs) no longer holds them to the same results.
//
// Course space as in the game: x and z horizontal, y up, metres; a heading of 0 points along +z.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// (exported from the plugin's module in Unreal; nothing outside it, as in the parity tests)
#ifndef OTGBALLPHYSICS_API
#define OTGBALLPHYSICS_API
#endif

namespace otg
{
	struct V3
	{
		double x = 0, y = 0, z = 0;
	};

	// ---- OpenFairway: the ball, the air, contacts and roll (openfairway.js)
	namespace of
	{
		OTGBALLPHYSICS_API extern const double MASS, RADIUS, G, RAD_PER_RPM, DT;

		// A surface's contact settings (openfairway.js `surface`)
		struct SurfaceParams
		{
			double kineticFriction = 0, rollingFriction = 0, grassViscosity = 0, criticalAngle = 0, spinbackResponseScale = 0;
			double spinbackThetaBoostMax = 0, spinbackSpinStartRpm = 0, spinbackSpinEndRpm = 0, spinbackSpeedStartMps = 0, spinbackSpeedEndMps = 0;
			double rollingSpinScale = 1, slidingSpinScale = 1, restitutionScale = 1, impactRetentionScale = 1, landingKeepScale = 1, impactPlowRatio = 0;
			// (turf: the landing loss read from the impact - exp(-landingAngleLoss vn/vt), and backspin's grip -
			// the speed-squared grass drag along the ground, and the drag shares of a shot struck along the ground)
			double landingAngleLoss = 0, landingSpinLoss = 0, grassFormDrag = 0, struckViscosityScale = 1, struckFormDragScale = 1;
			// (grass a slowing ball settles into: this much more rolling friction, faded in below settleSpeed m/s)
			double settleFriction = 0, settleSpeed = 0;
			bool hasBounce = false;
			double firstImpactMinimumSpeed = 0, laterImpactMinimumSpeed = 0, laterImpactScale = 0, minimumBounceHeight = 0;
		};

		struct Landing
		{
			double spinBiteScale = 0, maxSpinbackMps = 0, speedRampStartMps = 0, speedRampEndMps = 0;
		};

		// The landing response of a launch (its bucket: run, release, check, spinback)
		struct Launch
		{
			std::string bucket;
			Landing landing;
		};

		// The air a shot flies and rolls through: density, viscosity, and the wind at its reference height
		// (it grows with height over the launch ground, groundY: the log wind profile)
		struct Env
		{
			double rho = 1.225, mu = 1.789e-5, groundDragScale = 1.01;
			double rainDragScale = 1;   // (heavy rain: the ball pushing through the drops - RainDragScale)
			V3 wind;
			double windReferenceM = 10, windRoughnessM = 0.03, groundY = 0;
		};

		struct Aero
		{
			double speed = 0, spinRatio = 0, reynolds = 0, cd = 0, cl = 0, spinDecay = 0;
			bool outside = false;   // (outside the Reynolds number and spin ratio the flight table was fitted over)
		};

		OTGBALLPHYSICS_API SurfaceParams MakeSurface(double kineticFriction, double rollingFriction, double grassViscosity, double criticalAngle, double spinbackResponseScale);
		// A grass surface recalibrated as OpenFairway's turf: its grass drag and rolling friction kept in their ratio
		// to the ported fairway's, the turf landing law and the struck-along-the-ground drag shares
		OTGBALLPHYSICS_API SurfaceParams Turf(SurfaceParams s);
		OTGBALLPHYSICS_API const SurfaceParams& Preset(const char* name); // fairway, fairwaySoft, rough, firm, fringe, green, sand

		OTGBALLPHYSICS_API double GreenRollingFriction(double stimpFt, double viscosity = -1);
		OTGBALLPHYSICS_API double PuttSpeed(double distanceM, double stimpFt, double viscosity = -1);
		OTGBALLPHYSICS_API double AirDensity(double altitudeM, double tempC, double humidity = 0, double stationPressurePa = 0);
		OTGBALLPHYSICS_API double AirViscosity(double tempC);
		OTGBALLPHYSICS_API double WindFactor(double heightM, double referenceM = 10, double roughnessM = 0.03);
		OTGBALLPHYSICS_API double FlightDrag(double re, double spinRatio);   // (the fitted table: SpinRatioData.h)
		OTGBALLPHYSICS_API double FlightLift(double spinRatio);
		OTGBALLPHYSICS_API double GroundDragCoefficient(double re, double spinRatio);   // (a ball along the ground)
		OTGBALLPHYSICS_API V3 WindAt(const Env& env, double heightM);
		OTGBALLPHYSICS_API Aero AeroSample(const V3& air, const V3& spin, const Env& env);
		// One RK4 flight step of position, velocity and spin (the wind at the ball centre's height over env.groundY)
		OTGBALLPHYSICS_API Aero StepFlight(V3& p, V3& v, V3& spin, const Env& env, double dt);
		OTGBALLPHYSICS_API std::string LandingBucket(double vla, double rpm);
		OTGBALLPHYSICS_API Launch ResolveLaunch(double vlaDeg, double backSpinRpm);
		OTGBALLPHYSICS_API void ResolveImpact(V3& v, V3& spin, const V3& n, const SurfaceParams& surf, bool firstImpact, const Landing* response);
		OTGBALLPHYSICS_API bool HasRebound(const V3& v, const V3& n, const SurfaceParams& surf);
		OTGBALLPHYSICS_API V3 LaunchSpin(const V3& v, const V3& measured, const V3& n);
		OTGBALLPHYSICS_API double SpinFrictionMultiplier(double rpm, double speed);
		OTGBALLPHYSICS_API void StepGround(V3& v, V3& spin, const V3& n, const SurfaceParams& surf, double impactRpm, double dt, const Env& env);
		OTGBALLPHYSICS_API double RolloutSpinRpm(const V3& spin, const V3& n);   // (the landing spin a roll remembers)
		// A full shot struck into or along the ground (a topped shot): how much of the turf's struck drag it rolls with
		// (0 below putt speed, 12 m/s, all of it from 18 m/s), and the surface it rolls on
		OTGBALLPHYSICS_API double StruckAlongGroundWeight(double tangentSpeed);
		OTGBALLPHYSICS_API SurfaceParams StruckAlongGround(const SurfaceParams& s, double weight);
		OTGBALLPHYSICS_API bool CanRest(const V3& v, const V3& spin, const V3& n, const SurfaceParams& surf);
	}

	// ---- the ball on the course (physics.js)

	// The course's surfaces (physics.js SURFACES; None: off the course)
	enum class Surface : uint8_t { None, Water, Green, Fringe, Fairway, Tee, FirstCut, Rough, DeepRough, Native, Path, Sand, Oob };
	OTGBALLPHYSICS_API const char* SurfaceName(Surface s);   // as the game names them: "green", "deeprough" ...
	OTGBALLPHYSICS_API Surface SurfaceFromName(const std::string& name);

	OTGBALLPHYSICS_API extern const double BALL_RADIUS, CUP_RADIUS;

	// How the lie affects the next strike (physics.js LIE_EFFECT)
	struct LieEffect { double speed = 1, spin = 1; };
	OTGBALLPHYSICS_API LieEffect LieEffectFor(Surface lie);

	struct Course
	{
		virtual ~Course() = default;
		// The ground's height (NaN off the course), its normal, and the surface there
		virtual double HeightAt(double x, double z) const = 0;
		virtual V3 NormalAt(double x, double z) const = 0;
		virtual Surface SurfaceAt(double x, double z) const = 0;
	};

	// A tree as the physics sees it (the game's trees.js): a trunk, and a crown - an ellipsoid
	// (rh across, rv up, centred at cy) or, for conifers, a cone of needles
	struct Tree
	{
		double x = 0, z = 0, trunkTop = 0, trunkR = 0;
		bool cone = false;
		double coneBase = 0, coneH = 0, coneR = 0;   // (cone)
		double rh = 0, rv = 0, cy = 0;               // (ellipsoid)
		std::string kind;
	};
	struct Trees
	{
		virtual ~Trees() = default;
		virtual void Near(double x, double z, std::vector<const Tree*>& out) const = 0;
	};

	// Solid things the ball can hit (the game's collision world): what it met between two points
	struct Hit
	{
		std::string kind;   // terrain, trunk, building, pole, bumper, blade
		V3 point, normal;
	};
	struct Collider
	{
		virtual ~Collider() = default;
		virtual void SetPin(const V3* pin) = 0;
		// obstacleOnly: ignore the terrain
		virtual bool Cast(const V3& from, const V3& to, bool obstacleOnly, Hit& out) const = 0;
	};

	struct Shot
	{
		double speedMph = 0, vla = 0, hla = 0, backSpin = 0, sideSpin = 0;  // deg / rpm; hla, side spin + = right
	};

	struct ShotOptions
	{
		double heading = 0;            // radians
		double altitudeM = 0, tempC = 20, humidity = 0.5, pressureHpa = 0;  // (0: not measured)
		double windMps = 0, windFromDeg = 0;
		double wet = 0, stimp = 10;
		double rainMm = 0;   // (mm an hour: heavy rain takes a little off the carry)
		bool hasPin = false;
		V3 pin;
		const Trees* trees = nullptr;
		const Collider* collider = nullptr;
		std::function<double()> random; // [0, 1); Math.random's place (leaves, branches, lip-outs, ricochets)
		// Things that move while the ball does (the putt-putt windmill's sails; physics.js movers):
		// each is asked about every step of the ball, from a to b, t seconds after the strike, and
		// says what it met there (true) or nothing
		std::vector<std::function<bool(const V3& a, const V3& b, double t, Hit& out)>> movers;
	};

	struct PathPoint
	{
		double t = 0, x = 0, y = 0, z = 0, w = 0;
		bool air = false;
	};

	struct ShotResult
	{
		std::vector<PathPoint> points;
		std::string result;            // rest, holed, water, oob
		V3 rest;
		bool hasWaterEntry = false, hasCarryPoint = false;
		V3 waterEntry;
		PathPoint carryPoint;
		double carry = 0, total = 0, offline = 0, apex = 0, flightTime = 0, duration = 0, spinTilt = 0;
		std::vector<std::string> treeHits;
		std::string regime;
	};

	OTGBALLPHYSICS_API ShotResult SimulateShot(const Course& course, const V3& origin, const Shot& shot, const ShotOptions& opts);
}
