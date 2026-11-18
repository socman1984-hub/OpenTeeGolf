// Copyright (c) 2026 __PUBLISHER__. All Rights Reserved.

#pragma once

#include "GreenswardEditorPrivate.h"

#include "Engine/Texture2D.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionStaticSwitchParameter.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialParameterCollection.h"

namespace GreenswardCorePrivate
{
	static const TCHAR* MaterialFolder = TEXT("/Greensward/Materials");
	static const TCHAR* TextureFolder = TEXT("/Greensward/Textures");

	template <typename T>
	T* Node(UMaterial* Material, int32 X, int32 Y)
	{
		return CastChecked<T>(UMaterialEditingLibrary::CreateMaterialExpression(Material, T::StaticClass(), X, Y));
	}

	static UMaterialExpressionScalarParameter* Scalar(UMaterial* Material, const TCHAR* Name, float Default, const TCHAR* Group, int32 X, int32 Y, int32 PrimitiveDataIndex = INDEX_NONE)
	{
		UMaterialExpressionScalarParameter* P = Node<UMaterialExpressionScalarParameter>(Material, X, Y);
		P->ParameterName = Name;
		P->DefaultValue = Default;
		P->Group = Group;
		if (PrimitiveDataIndex != INDEX_NONE)
		{
			P->bUseCustomPrimitiveData = true;
			P->PrimitiveDataIndex = static_cast<uint8>(PrimitiveDataIndex);
		}
		return P;
	}

	static UMaterialExpressionVectorParameter* Vector(UMaterial* Material, const TCHAR* Name, const FLinearColor& Default, const TCHAR* Group, int32 X, int32 Y, int32 PrimitiveDataIndex = INDEX_NONE)
	{
		UMaterialExpressionVectorParameter* P = Node<UMaterialExpressionVectorParameter>(Material, X, Y);
		P->ParameterName = Name;
		P->DefaultValue = Default;
		P->Group = Group;
		if (PrimitiveDataIndex != INDEX_NONE)
		{
			P->bUseCustomPrimitiveData = true;
			P->PrimitiveDataIndex = static_cast<uint8>(PrimitiveDataIndex);
		}
		return P;
	}

	static UMaterialExpressionCustom* Custom(UMaterial* Material, const TCHAR* Description, ECustomMaterialOutputType OutputType, const TCHAR* Code, int32 X, int32 Y)
	{
		UMaterialExpressionCustom* C = Node<UMaterialExpressionCustom>(Material, X, Y);
		C->Description = Description;
		C->OutputType = OutputType;
		C->Code = Code;
		C->Inputs.Reset();
		return C;
	}

	static void In(UMaterialExpressionCustom* C, const TCHAR* Name, UMaterialExpression* Source, int32 OutputIndex = 0)
	{
		FCustomInput& Input = C->Inputs.AddDefaulted_GetRef();
		Input.InputName = Name;
		Input.Input.Connect(OutputIndex, Source);
	}

	static UMaterialExpressionCollectionParameter* Collection(UMaterial* Material, UMaterialParameterCollection* MPC, const TCHAR* Name, int32 X, int32 Y)
	{
		UMaterialExpressionCollectionParameter* P = Node<UMaterialExpressionCollectionParameter>(Material, X, Y);
		P->Collection = MPC;
		P->ParameterName = Name;
		P->ParameterId = MPC->GetParameterId(Name);
		return P;
	}

	/** DeleteAllMaterialExpressions can leave nodes behind in one pass; repeat until the graph is empty. */
	static void ClearGraph(UMaterial* Material)
	{
		for (int32 Pass = 0; Pass < 16 && Material->GetExpressions().Num() > 0; ++Pass)
		{
			UMaterialEditingLibrary::DeleteAllMaterialExpressions(Material);
		}
	}

	static UTexture2D* BuildTexture(const TCHAR* Name, int32 Size, bool bSRGB, TFunctionRef<FColor(float U, float V)> Shade)
	{
		TArray<uint8> Pixels;
		Pixels.SetNumUninitialized(Size * Size * 4);
		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				const FColor C = Shade((X + 0.5f) / Size, (Y + 0.5f) / Size);
				uint8* P = &Pixels[(Y * Size + X) * 4];
				P[0] = C.B;
				P[1] = C.G;
				P[2] = C.R;
				P[3] = C.A;
			}
		}

		UTexture2D* Texture = GreenswardEditor::FindOrCreateAsset<UTexture2D>(TextureFolder, Name);
		Texture->PreEditChange(nullptr);
		Texture->Source.Init(Size, Size, 1, 1, TSF_BGRA8, Pixels.GetData());
		Texture->SRGB = bSRGB;
		Texture->CompressionSettings = bSRGB ? TC_Default : TC_Masks;
		Texture->MipGenSettings = TMGS_FromTextureGroup;
		Texture->PostEditChange();
		GreenswardEditor::SaveAsset(Texture);
		return Texture;
	}

	static uint8 ToByte(float V)
	{
		return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt32(V * 255.0f), 0, 255));
	}
}
