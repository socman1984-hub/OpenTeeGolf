// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TidewrightSettings.h"
#include "TidewrightSimulation.h"
#include "TidewrightWater.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMaterialParameterCollection;
class UTextureRenderTarget2D;
class UPostProcessComponent;
class UProceduralMeshComponent;
class UTexture2D;
class UTidewrightPreset;
class ATidewrightShoreline;

/**
 * The water. Drop one into a level, pick a preset, done.
 *
 * The actor's height is the rest level of the water. In Ocean mode the surface follows the camera
 * and never ends; in Lake mode it is a rectangle centred on the actor.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Tidewright Water"))
class TIDEWRIGHT_API ATidewrightWater : public AActor
{
	GENERATED_BODY()

public:
	ATidewrightWater();

	/** Sea state to start from. Choosing one copies its values into Waves and Look below, where they can be tuned further. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tidewright")
	TObjectPtr<UTidewrightPreset> Preset;

	/** Endless ocean, or a bounded lake centred on this actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tidewright")
	ETidewrightWaterMode Mode = ETidewrightWaterMode::Ocean;

	/** Size of the lake along world X and Y. Lake mode only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tidewright", meta = (EditCondition = "Mode == ETidewrightWaterMode::Lake", ClampMin = "1.0", Units = "m"))
	FVector2D LakeSize = FVector2D(300.0, 200.0);

	/** Wave shape: wind, swell, choppiness and foam build-up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Tidewright", meta = (ShowOnlyInnerProperties))
	FTidewrightWaveSettings Waves;

	/** Shading: water colour, clarity, roughness and foam colour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Tidewright", meta = (ShowOnlyInnerProperties))
	FTidewrightLookSettings Look;

	/** The view when the camera is below the surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Tidewright", meta = (ShowOnlyInnerProperties))
	FTidewrightUnderwaterSettings Underwater;

	/** Detail of the simulated waves. The cost is CPU time and rises four-fold per step. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tidewright|Quality")
	ETidewrightSimulationQuality SimulationQuality = ETidewrightSimulationQuality::Medium;

	/** Number of mesh segments around the camera. More segments resolve smaller waves in the silhouette and cost more GPU time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tidewright|Quality", meta = (ClampMin = "48", ClampMax = "512"))
	int32 SurfaceSegments = 192;

	/** How far the surface mesh reaches from the camera. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tidewright|Quality", meta = (ClampMin = "0.2", ClampMax = "200.0", Units = "km"))
	float ViewDistance = 40.0f;

	/** Changes the random arrangement of waves without changing their character. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tidewright|Animation")
	int32 Seed = 1;

	/** Speed of time for the waves. 1 is real time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Tidewright|Animation", meta = (ClampMin = "0.0", ClampMax = "8.0"))
	float TimeScale = 1.0f;

	/** Use Manual Time instead of the world clock. Key Manual Time in Sequencer for exactly repeatable renders. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Tidewright|Animation")
	bool bUseManualTime = false;

	/** Wave time in seconds when Use Manual Time is on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Tidewright|Animation", meta = (EditCondition = "bUseManualTime", Units = "s"))
	float ManualTime = 0.0f;

	/**
	 * Shares the large waves with every material in the level, so meshes using the MF_Tidewright_Float material
	 * function can ride them without any component. Leave on for one water actor per level.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tidewright|Advanced")
	bool bShareWavesWithMaterials = true;

	/** Post-process material used for the underwater view. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tidewright|Advanced")
	TObjectPtr<UMaterialInterface> UnderwaterMaterial;

	/** Material used for the surface. Replace it with your own instance or a material built on the same parameters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tidewright|Advanced")
	TObjectPtr<UMaterialInterface> WaterMaterial;

	/** Copies a preset's values into this water, cross-fading the waves over BlendSeconds. */
	UFUNCTION(BlueprintCallable, Category = "Tidewright")
	void ApplyPreset(UTidewrightPreset* NewPreset, float BlendSeconds = 0.0f);

	/** Changes the wave settings, cross-fading over BlendSeconds. */
	UFUNCTION(BlueprintCallable, Category = "Tidewright")
	void SetWaveSettings(const FTidewrightWaveSettings& NewWaves, float BlendSeconds = 0.0f);

	/**
	 * Finds the water surface above or below a world location.
	 * @param WorldLocation  Any location; only X and Y are used.
	 * @param OutSurfaceLocation  The point on the water surface directly above or below.
	 * @param OutNormal  Surface normal there.
	 * @return false if the location is outside a lake or the water has not simulated yet.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tidewright")
	bool SampleWaterSurface(const FVector& WorldLocation, FVector& OutSurfaceLocation, FVector& OutNormal) const;

	/** Height of the water surface (world Z) at a world location. Returns the rest level if the location is outside the water. */
	UFUNCTION(BlueprintPure, Category = "Tidewright")
	float GetWaterHeightAtLocation(const FVector& WorldLocation) const;

	/** True if the location is below the water surface. */
	UFUNCTION(BlueprintPure, Category = "Tidewright")
	bool IsLocationUnderwater(const FVector& WorldLocation) const;

	/** Four times the standard deviation of wave height, in metres: the standard single-number measure of sea state. */
	UFUNCTION(BlueprintPure, Category = "Tidewright")
	float GetSignificantWaveHeight() const;

	/** CPU time the wave simulation and texture upload took last frame, in milliseconds. Use it to choose Simulation Quality. */
	UFUNCTION(BlueprintPure, Category = "Tidewright")
	float GetLastSimulationMilliseconds() const { return LastSimulationMs; }

	/** Wave time in seconds as currently displayed. */
	UFUNCTION(BlueprintPure, Category = "Tidewright")
	float GetWaveTime() const { return static_cast<float>(WaveTime); }

	/** Finds the first Tidewright Water in the world, or null. */
	UFUNCTION(BlueprintPure, Category = "Tidewright", meta = (WorldContext = "WorldContextObject"))
	static ATidewrightWater* GetTidewrightWater(const UObject* WorldContextObject);

	/** True while the camera that rendered the last frame is below the surface. Use it to switch audio or gameplay. */
	UFUNCTION(BlueprintPure, Category = "Tidewright")
	bool IsViewUnderwater() const { return bViewUnderwater; }

	/** How far the camera is below the surface, in metres. Negative above it. */
	UFUNCTION(BlueprintPure, Category = "Tidewright")
	float GetViewDepth() const { return ViewDepth; }

	/** The shoreline currently shaping this water: the scanned one nearest the camera. Null if there is none. */
	UFUNCTION(BlueprintPure, Category = "Tidewright")
	ATidewrightShoreline* GetActiveShoreline() const { return ActiveShoreline.Get(); }

	const FTidewrightSimulation& GetSimulation() const { return Simulation; }

	//~ AActor
	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }
	virtual void PostRegisterAllComponents() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Destroyed() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void EnsureSurface();
	void ReleaseSurface();
	void BuildMesh();
	void CreateTextures();
	void UploadTextures();
	void PushMaterialParameters(bool bForce);
	void UpdateShore(const FVector& ViewLocation);
	void UpdateUnderwater();
	void ShareWaves();
	bool IsInsideWater(const FVector& WorldLocation) const;
	int32 GetResolution() const;

	UPROPERTY(VisibleAnywhere, Category = "Tidewright")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> SurfaceMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SurfaceMaterial;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> DisplacementTextures;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> DerivativeTextures;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> ShoreTexture;

	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> SharedWaveTargets[2];

	UPROPERTY()
	TObjectPtr<UMaterialParameterCollection> SharedCollection;

	UPROPERTY(Transient)
	TObjectPtr<UPostProcessComponent> UnderwaterPost;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> UnderwaterInstance;

	float LastSimulationMs = 0.0f;
	float ViewDepth = -1000.0f;
	bool bViewUnderwater = false;

	TWeakObjectPtr<ATidewrightShoreline> ActiveShoreline;
	uint32 AppliedShoreVersion = 0;
	bool bShoreApplied = false;

	FTidewrightSimulation Simulation;
	FTidewrightWaveSettings AppliedWaves;
	FTidewrightLookSettings AppliedLook;
	FVector MeshCentre = FVector::ZeroVector;
	double WaveTime = 0.0;
	double LastWaveTime = 0.0;
	float PendingBlendSeconds = 0.0f;
	int32 BuiltSegments = 0;
	float BuiltViewDistance = 0.0f;
	int32 BuiltSeed = 0;
	bool bMaterialDirty = true;
	bool bHasCentre = false;
};
