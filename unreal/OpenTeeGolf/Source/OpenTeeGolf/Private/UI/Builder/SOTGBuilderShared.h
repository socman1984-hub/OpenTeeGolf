// Small helpers shared by the course builder's files (the course helper, the map's zoom limits).
// Private to UI/Builder.

#pragma once

#include "UI/Builder/SOTGCourseBuilder.h"
#include "UI/OTGUi.h"
#include "Course/OTGTextures.h"
#include "Course/OTGCourseSubsystem.h"
#include "App/OTGHelperSubsystem.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
using namespace otg::ui;

namespace
{
	UGameInstance* GI()
	{
		return GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetGameInstance() : nullptr;
	}
	UOTGHelperSubsystem* HelperSub() { UGameInstance* G = GI(); return G ? G->GetSubsystem<UOTGHelperSubsystem>() : nullptr; }
	constexpr double MinZoom = 2, MaxZoom = 18;
	constexpr int32 MaxTiles = 400, MaxAsking = 8;
	FString OsmTypeName(const FString& T) { return T == TEXT("n") ? FString(TEXT("node")) : T == TEXT("w") ? FString(TEXT("way")) : T == TEXT("r") ? FString(TEXT("relation")) : T; }
}
