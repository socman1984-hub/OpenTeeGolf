// Stars, moon and aurora for M_MeridianSky. This is the body of the material's Custom node; keep the two in sync.
// Inputs: LocalPos (dome-space position; the dome is rotated by the sky actor so stars turn with the Earth),
//         CamVec (world-space vector from the pixel towards the camera), MoonDir and SunDir (world, towards the body),
//         StarBrightness, MoonBrightness, MoonSize (angular radius in radians), Time (seconds),
//         Aurora (brightness of the aurora, 0 = off).

float3 D = normalize(LocalPos);
float3 V = -normalize(CamVec);

// ---- Stars: one candidate per grid cell on two grids, so bright stars are sparse and faint ones are dense.
float3 Stars = 0;
for (int Layer = 0; Layer < 2; Layer++)
{
	float Scale = Layer == 0 ? 85.0 : 190.0;
	float3 P = D * Scale;
	float3 Cell = floor(P);
	float3 H = frac(sin(float3(
		dot(Cell, float3(127.1, 311.7, 74.7)),
		dot(Cell, float3(269.5, 183.3, 246.1)),
		dot(Cell, float3(113.5, 271.9, 124.6)))) * 43758.5453);
	// Keep the star away from the cell walls so it is never cut off.
	float Dist = length(P - (Cell + 0.25 + H * 0.5));
	float Core = smoothstep(0.09, 0.0, Dist);
	// Real star counts rise steeply as brightness falls: a steep power gives a few bright stars among many dim ones.
	float Rnd = frac(H.x * 91.7 + H.y * 37.3);
	float Magnitude = (0.05 + pow(Rnd, 9.0)) * (Layer == 0 ? 1.0 : 0.4);
	float Twinkle = 0.8 + 0.2 * sin(Time * (2.0 + H.z * 5.0) + H.x * 40.0);
	float3 Tint = lerp(float3(1.0, 0.82, 0.66), float3(0.72, 0.83, 1.0), H.z);
	Stars += Tint * (Core * Core * Magnitude * Twinkle);
}
// Thick air near the horizon hides stars.
Stars *= saturate(V.z * 5.0);

// ---- Moon: a lit sphere, so the phase falls out of where the sun and moon actually are.
float3 M = normalize(MoonDir);
float CosA = dot(V, M);
float MoonMask = 0;
float3 MoonColour = 0;
if (CosA > cos(MoonSize))
{
	float3 Right = normalize(cross(float3(0.0, 0.0, 1.0), M));
	float3 Up = cross(M, Right);
	float3 Offset = V - M * CosA;
	float2 Disc = float2(dot(Offset, Right), dot(Offset, Up)) / sin(MoonSize);
	float Z2 = 1.0 - dot(Disc, Disc);
	if (Z2 > 0.0)
	{
		// Surface normal of the visible hemisphere, facing back towards the viewer.
		float3 N = Disc.x * Right + Disc.y * Up - sqrt(Z2) * M;
		float Lit = saturate(dot(N, normalize(SunDir)));
		// Moon dust scatters light back the way it came, so the lit side is flatter than a matte ball.
		Lit = smoothstep(0.0, 0.12, Lit) * (0.65 + 0.35 * Lit);
		// Dark seas: a fixed blotchy pattern in the moon's own frame.
		float2 S = Disc * 3.1;
		float Seas = sin(S.x * 1.7 + 1.3) * sin(S.y * 2.1 + 0.4) + 0.5 * sin(S.x * 3.9 - S.y * 3.1 + 2.0);
		float Albedo = lerp(0.55, 1.0, smoothstep(-0.25, 0.35, Seas));
		MoonColour = float3(1.0, 0.97, 0.9) * Albedo * (Lit + 0.012) * MoonBrightness;
		MoonMask = smoothstep(0.0, 0.04, Z2);
	}
}
// ---- Aurora: curtains of light hanging high above the ground. March the view ray through a stack of
// horizontal sheets and add up the glow, so the curtains have depth and lean with perspective.
float3 AuroraGlow = 0;
if (Aurora > 0.001 && V.z > 0.02)
{
	const int Sheets = 14;
	// Offset the sheets by a different amount at every pixel; temporal anti-aliasing then blends the steps away.
	float Jitter = frac(sin(dot(V.xy, float2(12.9898, 78.233)) * 2000.0) * 43758.5453);
	for (int i = 0; i < Sheets; i++)
	{
		float T = (i + Jitter) / Sheets;    // 0 at the curtain's lower edge, 1 at its top
		float Height = lerp(0.9, 1.9, T);   // sheet height, in arbitrary units
		float2 Q = V.xy / max(V.z, 0.06) * Height * 0.55;

		// Slow, large folds, then tighter ripples riding on them.
		Q += float2(sin(Q.y * 0.9 + Time * 0.045), cos(Q.x * 0.7 - Time * 0.038)) * 0.8;
		Q += float2(sin(Q.y * 2.3 + Time * 0.10), cos(Q.x * 1.9 + Time * 0.07)) * 0.3;

		// A curtain is where the folded coordinate crosses a ridge: thin bright ribbons with dark sky between.
		float Ridge = sin(Q.x * 2.1 + sin(Q.y * 1.3 + Time * 0.08) * 1.7);
		float Curtain = pow(saturate(1.0 - abs(Ridge)), 7.0);
		// Fine vertical rays running up the curtain, flickering slowly.
		float Rays = 0.75 + 0.25 * sin(Q.y * 13.0 + sin(Q.x * 5.0) * 3.0 + Time * 0.5);
		// The display comes in patches rather than covering the whole sky.
		float Patch = smoothstep(0.4, 0.85, 0.5 + 0.5 * sin(Q.x * 0.33 + Time * 0.012) * sin(Q.y * 0.27 + 1.3));

		// Oxygen green low down, fading to violet at the top of the curtain, brightest at the lower edge.
		float3 Tint = lerp(float3(0.12, 1.0, 0.42), float3(0.5, 0.18, 0.85), smoothstep(0.35, 1.0, T));
		AuroraGlow += Tint * (Curtain * Rays * Patch * (1.0 - T * 0.65));
	}
	// Fade towards the horizon, where the path through the air is long.
	AuroraGlow *= (Aurora / Sheets) * saturate(V.z * 5.0 - 0.1);
}

// The moon hides the stars behind it, and neither shows below the horizon.
MoonColour *= saturate(V.z * 12.0 + 0.2);
return Stars * StarBrightness * (1.0 - MoonMask) + MoonColour * MoonMask + AuroraGlow * (1.0 - MoonMask);
