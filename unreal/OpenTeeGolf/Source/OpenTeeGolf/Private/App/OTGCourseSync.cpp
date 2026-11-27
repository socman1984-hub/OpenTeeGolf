// See OTGCourseSync.h.

#include "App/OTGCourseSync.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Misc/SecureHash.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogOTGSync, Log, All);

namespace otg::sync
{
	const TCHAR* MarkerFile = TEXT(".otg_shared.json");

	namespace
	{
		// (hashes worked out already: by path, kept while the file's size and time are the same)
		struct FKnown { int64 Size = 0; FDateTime Time; FString Hash; };
		FCriticalSection KnownLock;
		TMap<FString, FKnown> Known;

		TArray<FString> FilesIn(const FString& Dir)
		{
			TArray<FString> Names;
			IFileManager::Get().FindFiles(Names, *FPaths::Combine(Dir, TEXT("*")), true, false);
			Names.RemoveAll([](const FString& N) { return !Counts(N); });
			Names.Sort();
			return Names;
		}
	}

	bool Counts(const FString& Name)
	{
		return !Name.IsEmpty() && !Name.StartsWith(TEXT(".")) && !Name.EndsWith(TEXT(".otgpart"));
	}

	FString HashFile(const FString& Path)
	{
		IFileManager& FM = IFileManager::Get();
		const int64 Size = FM.FileSize(*Path);
		if (Size < 0) return FString();
		const FDateTime Time = FM.GetTimeStamp(*Path);
		{
			FScopeLock L(&KnownLock);
			if (const FKnown* K = Known.Find(Path); K && K->Size == Size && K->Time == Time) return K->Hash;
		}
		TUniquePtr<FArchive> R(FM.CreateFileReader(*Path));
		if (!R) return FString();
		FSHA1 Sha;
		TArray<uint8> Buf;
		Buf.SetNumUninitialized(1 << 20);
		int64 Left = R->TotalSize();
		while (Left > 0)
		{
			const int32 N = int32(FMath::Min<int64>(Left, Buf.Num()));
			R->Serialize(Buf.GetData(), N);
			if (R->IsError()) return FString();
			Sha.Update(Buf.GetData(), N);
			Left -= N;
		}
		Sha.Final();
		uint8 Digest[FSHA1::DigestSize];
		Sha.GetHash(Digest);
		const FString Hash = BytesToHex(Digest, FSHA1::DigestSize);
		FScopeLock L(&KnownLock);
		Known.Add(Path, FKnown{ Size, Time, Hash });
		return Hash;
	}

	TArray<FFile> Manifest(const FString& Dir)
	{
		TArray<FFile> Out;
		for (const FString& N : FilesIn(Dir))
		{
			const FString P = FPaths::Combine(Dir, N);
			FFile F;
			F.Path = N;
			F.Size = IFileManager::Get().FileSize(*P);
			F.Hash = HashFile(P);
			if (F.Size >= 0 && !F.Hash.IsEmpty()) Out.Add(F);
		}
		return Out;
	}

	FString Fingerprint(const TArray<FFile>& Files)
	{
		TArray<FString> Lines;
		for (const FFile& F : Files) Lines.Add(FString::Printf(TEXT("%s:%lld:%s"), *F.Path.ToLower(), F.Size, *F.Hash));
		Lines.Sort();
		return FMD5::HashAnsiString(*FString::Join(Lines, TEXT("\n")));
	}

	bool ReadMarker(const FString& Dir, FMarker& Out)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(Dir, MarkerFile))) return false;
		TSharedPtr<FJsonObject> O;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), O) || !O.IsValid()) return false;
		O->TryGetStringField(TEXT("id"), Out.Id);
		O->TryGetStringField(TEXT("name"), Out.Name);
		O->TryGetStringField(TEXT("original"), Out.Original);
		O->TryGetStringField(TEXT("originalName"), Out.OriginalName);
		O->TryGetStringField(TEXT("from"), Out.From);
		return !Out.Id.IsEmpty() && !Out.Original.IsEmpty();
	}

	bool WriteMarker(const FString& Dir, const FMarker& M)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("id"), M.Id);
		O->SetStringField(TEXT("name"), M.Name);
		O->SetStringField(TEXT("original"), M.Original);
		O->SetStringField(TEXT("originalName"), M.OriginalName);
		O->SetStringField(TEXT("from"), M.From);
		FString Text;
		FJsonSerializer::Serialize(O, TJsonWriterFactory<>::Create(&Text));
		return FFileHelper::SaveStringToFile(Text, *FPaths::Combine(Dir, MarkerFile), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	TArray<FLocal> LocalCourses(const FString& Root)
	{
		TArray<FLocal> Out;
		TArray<FString> Dirs;
		IFileManager::Get().FindFiles(Dirs, *FPaths::Combine(Root, TEXT("*")), false, true);
		for (const FString& D : Dirs)
		{
			FLocal L;
			L.Dir = FPaths::Combine(Root, D);
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(L.Dir, TEXT("play.json")))) continue;
			TSharedPtr<FJsonObject> Play;
			if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Play) || !Play.IsValid() || !Play->TryGetStringField(TEXT("id"), L.PlayId)) continue;
			Play->TryGetStringField(TEXT("name"), L.Name);
			L.bMarked = ReadMarker(L.Dir, L.M);
			Out.Add(L);
		}
		return Out;
	}

	FPlan MakePlan(const FString& Target, const TArray<FFile>& Want, const TArray<FString>& OtherDirs, bool bPrune)
	{
		FPlan P;
		P.Target = Target;
		IFileManager& FM = IFileManager::Get();
		// (the files here by size: where to look for one with the same contents)
		TMultiMap<int64, FString> BySize;
		bool bIndexed = false;
		auto Index = [&]() {
			if (bIndexed) return;
			bIndexed = true;
			for (const FString& D : OtherDirs)
				for (const FString& N : FilesIn(D))
				{
					const FString Path = FPaths::Combine(D, N);
					BySize.Add(FM.FileSize(*Path), Path);
				}
		};
		TSet<FString> Names;
		for (int32 i = 0; i < Want.Num(); i++)
		{
			const FFile& F = Want[i];
			Names.Add(F.Path.ToLower());
			const FString Here = FPaths::Combine(Target, F.Path);
			if (FM.FileSize(*Here) == F.Size && HashFile(Here) == F.Hash) { P.Kept++; continue; }
			// (another course's file of the same name first - the trees are in every course - then any of the same size)
			FString Found;
			for (const FString& D : OtherDirs)
			{
				const FString There = FPaths::Combine(D, F.Path);
				if (FM.FileSize(*There) == F.Size && HashFile(There) == F.Hash) { Found = There; break; }
			}
			if (Found.IsEmpty())
			{
				Index();
				TArray<FString> Same;
				BySize.MultiFind(F.Size, Same);
				for (const FString& S : Same) if (HashFile(S) == F.Hash) { Found = S; break; }
			}
			if (!Found.IsEmpty()) P.Copies.Emplace(Found, i);
			else
			{
				P.Download.Add(i);
				P.DownloadBytes += F.Size;
			}
		}
		if (bPrune)
			for (const FString& N : FilesIn(Target))
				if (!Names.Contains(N.ToLower())) P.Remove.Add(FPaths::Combine(Target, N));
		return P;
	}

	bool CopyFiles(const FPlan& Plan, const TArray<FFile>& Want, FString& Why)
	{
		IFileManager& FM = IFileManager::Get();
		FM.MakeDirectory(*Plan.Target, true);
		for (const TPair<FString, int32>& C : Plan.Copies)
		{
			const FString To = FPaths::Combine(Plan.Target, Want[C.Value].Path);
			const FString Part = To + TEXT(".otgpart");
			if (FM.Copy(*Part, *C.Key, true, true) != COPY_OK || !FM.Move(*To, *Part, true, true))
			{
				FM.Delete(*Part, false, true, true);
				Why = FString::Printf(TEXT("couldn't copy %s"), *Want[C.Value].Path);
				return false;
			}
		}
		return true;
	}

	void RemoveFiles(const FPlan& Plan)
	{
		for (const FString& R : Plan.Remove)
		{
			UE_LOG(LogOTGSync, Display, TEXT("Not the host's: %s (taken out)"), *R);
			IFileManager::Get().Delete(*R, false, true, true);
		}
	}

	FString SafeName(const FString& S)
	{
		FString Out;
		for (TCHAR C : S) Out.AppendChar(FChar::IsAlnum(C) ? C : TEXT('_'));
		while (Out.ReplaceInline(TEXT("__"), TEXT("_"))) {}
		Out.TrimCharInline(TEXT('_'), nullptr);
		return Out.IsEmpty() ? FString(TEXT("player")) : Out;
	}
}
