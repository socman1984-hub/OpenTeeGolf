// Lightning bolt for M_MeridianLightning. This is the body of the material's Custom node; keep the two in sync.
// Inputs: UV (0-1 across a vertical card, V = 0 at the cloud base), Seed (changes the bolt's shape per strike).

float X = UV.x - 0.5;
float Y = UV.y;

// Main channel: a jagged path made by stacking linearly interpolated random offsets at rising frequencies.
float Path = 0;
float Amp = 0.17;
float Freq = 4.0;
for (int i = 0; i < 5; i++)
{
	float T = Y * Freq;
	float C = floor(T);
	float A = frac(sin(C * 127.1 + Seed * 13.7 + i * 31.0) * 43758.5453);
	float B = frac(sin((C + 1.0) * 127.1 + Seed * 13.7 + i * 31.0) * 43758.5453);
	Path += (lerp(A, B, T - C) - 0.5) * Amp;
	Freq *= 2.3;
	Amp *= 0.5;
}
float Dist = abs(X - Path);
float Glow = pow(0.0035 / (Dist + 0.0035), 1.5);

// One branch that forks off part-way down and dies out.
float Fork = 0.25 + 0.3 * frac(sin(Seed * 7.31) * 9173.3);
float Side = frac(sin(Seed * 3.17) * 5113.7) > 0.5 ? 1.0 : -1.0;
float Along = saturate((Y - Fork) / 0.35);
float BranchPath = Path + Side * Along * 0.22 + sin(Y * 90.0 + Seed) * 0.006 * Along;
float BranchLive = step(Fork, Y) * (1.0 - Along);
Glow += pow(0.002 / (abs(X - BranchPath) + 0.002), 1.5) * BranchLive * 0.6;

// Fade at the card's sides and where the bolt leaves the cloud.
Glow *= smoothstep(0.5, 0.38, abs(X)) * smoothstep(0.0, 0.04, Y);
return Glow;
