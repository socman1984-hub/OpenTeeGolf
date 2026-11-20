// Copyright __PUBLISHER__. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "HearthlineTypes.h"
#include "HearthlineMeshBuilder.h"

class AHearthlineHouse;

/** Builds the mesh for one AHearthlineHouse. Lives only for the duration of a rebuild. */
struct FHearthlineHouseGen
{
	struct FVol
	{
		const FHearthlineVolume* V = nullptr;
		double X0 = 0, X1 = 0, Y0 = 0, Y1 = 0;
		double ZBase = 0, ZTop = 0, ZPeak = 0;
		/** Roof run length and span in the roof's own frame. */
		double L = 0, S = 0;
		bool bRot = false;
		EHearthlineRoof Roof = EHearthlineRoof::Gable;
	};

	struct FOpen
	{
		double S0 = 0, S1 = 0, Z0 = 0, Z1 = 0;
		EHearthlineOpening Type = EHearthlineOpening::Window;
		bool bFrontDoor = false;
	};

	struct FGableArgs
	{
		double L = 0, S = 0, Z0 = 0, Pitch = 30, Overhang = 40;
		double Rake = 30;
		bool bOpen0 = true, bOpen1 = true;
		double Ext0 = 0, Ext1 = 0;
		int32 GableMat = 0;
		FLinearColor GableColor = FLinearColor::White;
		bool bVents = false, bGutters = false;
		double FasciaDepth = 16;
	};

	explicit FHearthlineHouseGen(const AHearthlineHouse& InHouse);
	void Build();

	const AHearthlineHouse& H;
	FHearthlineMeshBuilder B;
	TArray<FVol> Vols;
	FRandomStream Rng;

private:
	static void SideFrame(const FVol& V, int32 Side, FVector& Origin, FVector& Dir, FVector& N, double& Len);
	bool InsideOther(const FVector& P, int32 Self, int32* OutOther = nullptr) const;
	void Blocked(int32 VolIndex, int32 Side, double ZMid, TArray<FVector2D>& Out) const;
	void CollectOpenings(int32 VolIndex, int32 Side, int32 Storey, double L, double Height, const TArray<FVector2D>& BlockedSpans, TArray<FOpen>& Out) const;
	void CladdingFor(const FVol& V, int32 Storey, bool bGable, int32& Mat, FLinearColor& Col) const;

	void EmitVolumeWalls(int32 VolIndex);
	void EmitVolumeRoof(int32 VolIndex);
	void EmitWallSkin(double L, double Height, const TArray<FOpen>& Opens);
	void EmitReveal(double S0, double S1, double Z0, double Z1, double Depth);
	void EmitCasing(double S0, double S1, double Z0, double Z1, bool bSill);
	void EmitGlass(double S0, double S1, double Z0, double Z1, double Y);
	void EmitWindow(const FOpen& O, bool bAllowShutters);
	void EmitDoor(const FOpen& O);
	void EmitGarageDoor(const FOpen& O);
	void EmitLantern(double S, double Z);
	void EmitSteps(double SCentre, double Width, double YStart, double TopZ, int32 Mat, const FLinearColor& Col, double& OutRun);
	void EmitGable(const FGableArgs& A);
	void EmitHip(double L, double S, double Z0, double Pitch, double Overhang);
	void EmitShed(double L, double S, double Z0, double Pitch, double Overhang, int32 WallMat, const FLinearColor& WallCol);
	void EmitFlat(double L, double S, double Z0, int32 WallMat, const FLinearColor& WallCol);
	void EmitDormers(const FVol& V, int32 VolIndex, double Sign, int32 Count);
	void EmitDownspout(const FVector& Top, const FVector& WallPoint);
	void EmitPorch(const FHearthlinePorch& P);
	void EmitChimney();
	void EmitLot();
	void EmitPlanting();
	void EmitFence();
	void EmitFenceRun(const FVector& From, const FVector& To);
	bool OnPaving(const FVector& P, double Margin) const;

	/** Walkway start distances recorded while building, per front door. */
	bool bHaveDoor = false;
	FVector DoorOrigin = FVector::ZeroVector, DoorDir = FVector::ForwardVector;
	double DoorS = 0, DoorOut = 0;
	bool bHaveGarage = false;
	FVector GarageOrigin = FVector::ZeroVector, GarageDir = FVector::ForwardVector;
	double GarageS0 = 0, GarageS1 = 0;
};
