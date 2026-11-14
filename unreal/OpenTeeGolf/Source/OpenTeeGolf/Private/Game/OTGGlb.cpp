// See OTGGlb.h.

#include "OTGGlb.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogOTGGlb, Log, All);

namespace
{
	struct FAccessor
	{
		const uint8* Data = nullptr;
		int32 Count = 0, Comps = 1, CompType = 5126, Stride = 0;
		bool bNormalized = false;
		bool Valid() const { return Data && Count > 0; }
		double Get(int32 i, int32 c) const
		{
			const uint8* P = Data + i * Stride;
			switch (CompType)
			{
			case 5126: return reinterpret_cast<const float*>(P)[c];
			case 5125: return reinterpret_cast<const uint32*>(P)[c];
			case 5123: { const uint16 v = reinterpret_cast<const uint16*>(P)[c]; return bNormalized ? v / 65535.0 : v; }
			case 5121: { const uint8 v = P[c]; return bNormalized ? v / 255.0 : v; }
			case 5122: { const int16 v = reinterpret_cast<const int16*>(P)[c]; return bNormalized ? FMath::Max(v / 32767.0, -1.0) : v; }
			case 5120: { const int8 v = reinterpret_cast<const int8*>(P)[c]; return bNormalized ? FMath::Max(v / 127.0, -1.0) : v; }
			}
			return 0;
		}
	};

	int32 CompSize(int32 T) { return T == 5126 || T == 5125 ? 4 : T == 5123 || T == 5122 ? 2 : 1; }
	int32 CompCount(const FString& Type) { return Type == TEXT("SCALAR") ? 1 : Type == TEXT("VEC2") ? 2 : Type == TEXT("VEC3") ? 3 : Type == TEXT("VEC4") ? 4 : 1; }
}

bool otg::ReadGlb(const FString& File, FGlbMesh& Out)
{
	TArray<uint8> B;
	if (!FFileHelper::LoadFileToArray(B, *File)) return false;
	if (B.Num() < 20 || FMemory::Memcmp(B.GetData(), "glTF", 4) != 0) { UE_LOG(LogOTGGlb, Warning, TEXT("%s isn't a glTF binary"), *File); return false; }
	const uint32 JsonLen = *reinterpret_cast<const uint32*>(&B[12]);
	FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(&B[20]), JsonLen);
	const FString Json(Conv.Length(), Conv.Get());
	TSharedPtr<FJsonObject> J;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), J) || !J.IsValid()) { UE_LOG(LogOTGGlb, Warning, TEXT("%s: its JSON doesn't read"), *File); return false; }
	const int64 BinStart = 20 + int64(JsonLen) + 8;
	if (BinStart > B.Num()) return false;
	const uint8* Bin = B.GetData() + BinStart;
	const int64 BinLen = B.Num() - BinStart;

	const TArray<TSharedPtr<FJsonValue>>& Views = J->GetArrayField(TEXT("bufferViews"));
	const TArray<TSharedPtr<FJsonValue>>& Accs = J->GetArrayField(TEXT("accessors"));
	auto Acc = [&](int32 Index) {
		FAccessor A;
		if (!Accs.IsValidIndex(Index)) return A;
		const TSharedPtr<FJsonObject> O = Accs[Index]->AsObject();
		int32 ViewI = -1;
		if (!O->TryGetNumberField(TEXT("bufferView"), ViewI) || !Views.IsValidIndex(ViewI)) return A;
		const TSharedPtr<FJsonObject> V = Views[ViewI]->AsObject();
		int64 Off = 0, AccOff = 0;
		V->TryGetNumberField(TEXT("byteOffset"), Off);
		O->TryGetNumberField(TEXT("byteOffset"), AccOff);
		A.Count = O->GetIntegerField(TEXT("count"));
		A.CompType = O->GetIntegerField(TEXT("componentType"));
		A.Comps = CompCount(O->GetStringField(TEXT("type")));
		O->TryGetBoolField(TEXT("normalized"), A.bNormalized);
		int32 Stride = 0;
		V->TryGetNumberField(TEXT("byteStride"), Stride);
		A.Stride = Stride ? Stride : A.Comps * CompSize(A.CompType);
		if (Off + AccOff + int64(A.Stride) * (A.Count - 1) + A.Comps * CompSize(A.CompType) > BinLen) { A.Count = 0; return A; }
		A.Data = Bin + Off + AccOff;
		return A;
	};

	for (const TSharedPtr<FJsonValue>& MV : J->GetArrayField(TEXT("meshes")))
		for (const TSharedPtr<FJsonValue>& PV : MV->AsObject()->GetArrayField(TEXT("primitives")))
		{
			const TSharedPtr<FJsonObject> P = PV->AsObject();
			const TSharedPtr<FJsonObject> At = P->GetObjectField(TEXT("attributes"));
			int32 PosI = -1, NrmI = -1, ColI = -1, IdxI = -1;
			At->TryGetNumberField(TEXT("POSITION"), PosI);
			At->TryGetNumberField(TEXT("NORMAL"), NrmI);
			At->TryGetNumberField(TEXT("COLOR_0"), ColI);
			P->TryGetNumberField(TEXT("indices"), IdxI);
			const FAccessor Pos = Acc(PosI);
			if (!Pos.Valid()) continue;
			const FAccessor Nrm = Acc(NrmI), Col = Acc(ColI), Idx = Acc(IdxI);
			const int32 Base = Out.Positions.Num();
			for (int32 i = 0; i < Pos.Count; i++)
			{
				Out.Positions.Add(FVector3f(Pos.Get(i, 0), Pos.Get(i, 1), Pos.Get(i, 2)));
				if (Nrm.Valid()) Out.Normals.Add(FVector3f(Nrm.Get(i, 0), Nrm.Get(i, 1), Nrm.Get(i, 2)));
				if (Col.Valid()) Out.Colors.Add(FLinearColor(Col.Get(i, 0), Col.Get(i, 1), Col.Get(i, 2), Col.Comps > 3 ? Col.Get(i, 3) : 1.0));
			}
			if (Idx.Valid()) for (int32 i = 0; i < Idx.Count; i++) Out.Indices.Add(Base + int32(Idx.Get(i, 0)));
			else for (int32 i = 0; i < Pos.Count; i++) Out.Indices.Add(Base + i);
		}
	// (all or none: a part without normals or colours drops them for the whole)
	if (Out.Normals.Num() != Out.Positions.Num()) Out.Normals.Reset();
	if (Out.Colors.Num() != Out.Positions.Num()) Out.Colors.Reset();
	return Out.Positions.Num() > 0;
}
