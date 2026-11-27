// A course's files compared between two computers, for online games (UOTGNetSubsystem): each file's
// size and SHA-1 (a "manifest"), what a folder here needs to match the host's (kept, copied from
// another course here that has the same file - the tree models are the same in every course - or
// downloaded), and the marker that names a saved copy of someone else's version of a course.
//
// A course folder is flat: every file in it counts except those starting with "." (the marker, the
// web version's .mark) and half-written downloads (*.otgpart).
//
// Everything here reads or writes files: call it off the game thread.

#pragma once

#include "CoreMinimal.h"

namespace otg::sync
{
	struct FFile
	{
		FString Path;      // (the name in the folder)
		int64 Size = 0;
		FString Hash;      // (SHA-1, hex)
	};

	// The copy of another player's version of a course, saved beside your own: .otg_shared.json in
	// its folder. The course list shows it by this id and name.
	struct FMarker
	{
		FString Id, Name;              // (the copy's: its folder's name, "<course> (from <player>)")
		FString Original, OriginalName;// (the course it's a version of)
		FString From;                  // (whose it was)
	};
	extern const TCHAR* MarkerFile;
	bool ReadMarker(const FString& Dir, FMarker& Out);
	bool WriteMarker(const FString& Dir, const FMarker& M);

	bool Counts(const FString& Name);   // (a file that's part of the course)
	FString HashFile(const FString& Path);   // (empty: unreadable; remembered by path, size and time)
	TArray<FFile> Manifest(const FString& Dir);
	FString Fingerprint(const TArray<FFile>& Files);   // (one hash for them all)

	// A course folder here: its play.json id and its marker (a saved copy)
	struct FLocal
	{
		FString Dir, PlayId, Name;
		bool bMarked = false;
		FMarker M;
		FString CourseId() const { return bMarked ? M.Id : PlayId; }
		FString OriginalId() const { return bMarked ? M.Original : PlayId; }
	};
	TArray<FLocal> LocalCourses(const FString& Root);

	// Getting a folder to match the host's files
	struct FPlan
	{
		FString Target;
		TArray<int32> Download;        // (indexes into the host's files)
		TArray<TPair<FString, int32>> Copies;   // (a file here with the same contents, and which)
		TArray<FString> Remove;        // (files in the target the host doesn't have)
		int64 DownloadBytes = 0;
		int32 Kept = 0;
	};
	// (bPrune: take out the target's own files the host doesn't have - its folder made the host's)
	FPlan MakePlan(const FString& Target, const TArray<FFile>& Want, const TArray<FString>& OtherDirs, bool bPrune);
	bool CopyFiles(const FPlan& Plan, const TArray<FFile>& Want, FString& Why);
	void RemoveFiles(const FPlan& Plan);
	FString SafeName(const FString& S);   // (for a folder's name)
}
