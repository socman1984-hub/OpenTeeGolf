// OpenTeeGolf ball physics in C++: the game's public/js/openfairway.js and public/js/physics.js,
// line for line, so a shot plays the same in the Unreal game as in the web version. Plain C++17
// with no Unreal types: the game and the parity test (unreal/tests/physics) build the same files.
//
// OpenFairway (openfairway.js) is a port of the OpenFairwayPhysics plugin of uopengolf
// (https://github.com/digitalhand/uopengolf, MIT, Copyright (c) 2026 uopengolf contributors), whose
// equations and calibration come from ogolf-physics' OpenFairway. See NOTICE.md.
//
// Course space as in the game: x and z horizontal, y up, metres; a heading of 0 points along +z.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace otg
{
	struct V3
	{
		double x = 0, y = 0, z = 0;
	};

	// ---- OpenFairway: the ball, the air, contacts and roll (openfairway.js)
	namespace of
	{
		extern const double MASS, RADIUS, G, RAD_PER_RPM, DT;

		// A surface's contact settings (openfairway.js `surface`)
		struct SurfaceParams
		{
			double kineticFriction = 0, rollingFriction = 0, grassViscosity = 0, criticalAngle = 0, spinbackResponseScale = 0;
			double spinbackThetaBoostMax = 0, spinbackSpinStartRpm = 0, spinbackSpinEndRpm = 0, spinbackSpeedStartMps = 0, spinbackSpeedEndMps = 0;
			double rollingSpinScale = 1, restitutionScale = 1, impactRetentionScale = 1, impactPlowRatio = 0;
			bool hasBounce = false;
			double firstImpactMinimumSpeed = 0, laterImpactMinimumSpeed = 0, laterImpactScale = 0, minimumBounceHeight = 0;
		};

		struct Landing
		{
			double spinBiteScale = 0, maxSpinbackMps = 0, speedRampStartMps = 0, speedRampEndMps = 0;
		};

		struct Launch
		{
			std::string regime, bucket;
			double dragScale = 1, liftScale = 1, launchDeg = 0;
			Landing landing;
		};

		struct Env
		{
			double rho = 1.2, mu = 1.8e-5, dragScale = 1, liftScale = 1, launchDeg = 0;
		};

		struct Aero
		{
			double speed = 0, spinRatio = 0, reynolds = 0, cd = 0, cl = 0, spinDecay = 0;
		};

		SurfaceParams MakeSurface(double kineticFriction, double rollingFriction, double grassViscosity, double criticalAngle, double spinbackResponseScale);
		const SurfaceParams& Preset(const char* name); // fairway, fairwaySoft, rough, firm, fringe, green, sand

		double GreenRollingFriction(double stimpFt, double viscosity = -1);
		double PuttSpeed(double distanceM, double stimpFt, double viscosity = -1);
		double AirDensity(double altitudeM, double tempC, double humidity = 0, double stationPressurePa = 0);
		double AirViscosity(double tempC);
		double WindFactor(double heightM, double referenceM = 10, double roughnessM = 0.03);
		double DragCoefficient(double re);
		double LiftCoefficient(double re, double spinRatio);
		Aero AeroSample(const V3& air, const V3& spin, const Env& env);
		Aero StepFlight(V3& v, V3& spin, const Env& env, const V3& wind, double dt);
		std::string RegimeKey(double mph, double vla, double rpm);
		std::string LandingBucket(double vla, double rpm);
		Launch ResolveLaunch(double speedMph, double vlaDeg, double totalSpinRpm);
		void ResolveImpact(V3& v, V3& spin, const V3& n, const SurfaceParams& surf, bool firstImpact, const Landing* response);
		bool HasRebound(const V3& v, const V3& n, const SurfaceParams& surf);
		V3 LaunchSpin(const V3& v, const V3& measured, const V3& n);
		double SpinFrictionMultiplier(double rpm, double speed);
		void StepGround(V3& v, V3& spin, const V3& n, const SurfaceParams& surf, double impactRpm, double dt);
		bool CanRest(const V3& v, const V3& spin, const V3& n, const SurfaceParams& surf);
	}

	// ---- the ball on the course (physics.js)

	// The course's surfaces (physics.js SURFACES; None: off the course)
	enum class Surface : uint8_t { None, Water, Green, Fringe, Fairway, Tee, FirstCut, Rough, DeepRough, Native, Path, Sand, Oob };
	const char* SurfaceName(Surface s);   // as the game names them: "green", "deeprough" ...
	Surface SurfaceFromName(const std::string& name);

	extern const double BALL_RADIUS, CUP_RADIUS;

	// How the lie affects the next strike (physics.js LIE_EFFECT)
	struct LieEffect { double speed = 1, spin = 1; };
	LieEffect LieEffectFor(Surface lie);

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
		bool hasPin = false;
		V3 pin;
		const Trees* trees = nullptr;
		const Collider* collider = nullptr;
		std::function<double()> random; // [0, 1); Math.random's place (leaves, branches, lip-outs, ricochets)
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

	ShotResult SimulateShot(const Course& course, const V3& origin, const Shot& shot, const ShotOptions& opts);
}
