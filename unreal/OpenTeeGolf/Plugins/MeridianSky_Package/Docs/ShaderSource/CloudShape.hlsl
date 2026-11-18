// Cloud density and shading for M_MeridianClouds. This is the body of the material's Custom node (float2 output);
// keep the two in sync.
// Inputs: WorldPos (cm), NormAlt (0-1 height within the cloud layer), Coverage (0-1), Storm (0-1),
//         Density (extinction scale), Wind (cm offset, accumulated by the sky actor),
//         Noise (tiling 3D Perlin-Worley, values centred on 0.5), Detail (unused, reserved).

// Work in kilometres so the scale constants below read as real sizes.
float3 P = (WorldPos + Wind.xyz) * 0.00001;

// Weather: where cloud masses sit. Two unrelated low frequencies so the pattern does not visibly tile.
float W1 = Texture3DSampleLevel(Noise, NoiseSampler, float3(P.xy * 0.017, 0.37), 0).r;
float W2 = Texture3DSampleLevel(Noise, NoiseSampler, float3(P.yx * 0.0053 + 0.31, 0.71), 0).g;
float Weather = saturate(((W1 + W2) * 0.5 - 0.3) / 0.4);

// Shape: the individual cloud forms, a few kilometres across.
float Base = Texture3DSampleLevel(Noise, NoiseSampler, P * float3(0.09, 0.09, 0.13), 0).r;
float Field = Weather * 0.65 + saturate((Base - 0.25) / 0.5) * 0.35;

// Coverage decides how much of the field survives: nothing at 0, everything at 1.
float D = saturate((Field - (1.0 - Coverage)) * 3.5);
// Keep thick and thin patches even when the sky is fully covered, so an overcast deck still has structure.
D *= lerp(0.35, 1.0, saturate(Field * 1.3));

// A covered sky without a storm is a layer cloud (stratocumulus): a shallow sheet rather than tall heaps.
float Layered = smoothstep(0.6, 0.95, Coverage) * (1.0 - Storm);

// Height profile: flat base, rounded top. Dense weather and storms build taller towers; layer cloud stays shallow.
float Top = lerp(0.3, 1.0, saturate(Weather * Coverage + Storm)) * lerp(1.0, 0.4, Layered);
float Profile = smoothstep(0.0, 0.06, NormAlt) * (1.0 - smoothstep(Top * 0.4, Top, NormAlt));
D *= Profile;

// Erosion: fine detail eats the edges, more towards the top where real clouds billow.
float4 Fine = Texture3DSampleLevel(Noise, NoiseSampler, P * 0.61 + 0.13, 0);
float Erode = saturate(Fine.b) * lerp(0.2, 0.5, NormAlt) * (1.0 - Storm * 0.5);
D = saturate((D - Erode) / max(1.0 - Erode, 0.05));

// Layer cloud is a quilt of thick rolls and thin gaps a kilometre or two across. The thin gaps let light
// through, which is what gives an overcast sky its mottled light-and-dark underside.
float Mottle = saturate((Fine.g * 0.6 + Base * 0.5 - 0.3) / 0.45);
D *= lerp(1.0, 0.1 + 1.15 * Mottle * Mottle, Layered);

// A storm's underside is a patchwork of dark rain cores and paler gaps a few kilometres across, with a ragged
// base. Thin the lowest part of the cloud in the gaps, and darken the cores (returned as a shading scale).
float Cells = saturate((Base * 0.7 + Fine.g * 0.45 - 0.35) / 0.4);
float NearBase = 1.0 - smoothstep(0.0, 0.3, NormAlt);
D *= lerp(1.0, 0.35 + 0.95 * Cells, Storm * NearBase);
float Shade = lerp(1.0, 1.15 - 0.75 * Cells, Storm);

// Storm cloud is several times thicker than fair-weather cumulus; layer cloud is thinner.
// X is the extinction; Y scales the cloud's colour.
return float2(D * Density * lerp(1.0, 3.0, Storm) * lerp(1.0, 0.6, Layered), Shade);
