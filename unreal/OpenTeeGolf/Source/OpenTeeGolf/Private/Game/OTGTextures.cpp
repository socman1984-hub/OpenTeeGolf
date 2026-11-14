// See OTGTextures.h.

#include "OTGTextures.h"

#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "TextureResource.h"

DEFINE_LOG_CATEGORY_STATIC(LogOTGTextures, Log, All);

namespace
{
	// Half the size, each pixel the mean of the four under it (Channels bytes a pixel)
	TArray<uint8> Halve(const TArray<uint8>& Src, int32 W, int32 H, int32 Channels, int32& OutW, int32& OutH)
	{
		OutW = FMath::Max(1, W / 2);
		OutH = FMath::Max(1, H / 2);
		TArray<uint8> Out;
		Out.SetNumUninitialized(OutW * OutH * Channels);
		for (int32 y = 0; y < OutH; y++)
		{
			const int32 y0 = FMath::Min(y * 2, H - 1), y1 = FMath::Min(y * 2 + 1, H - 1);
			for (int32 x = 0; x < OutW; x++)
			{
				const int32 x0 = FMath::Min(x * 2, W - 1), x1 = FMath::Min(x * 2 + 1, W - 1);
				for (int32 c = 0; c < Channels; c++)
				{
					const int32 Sum = Src[(y0 * W + x0) * Channels + c] + Src[(y0 * W + x1) * Channels + c] + Src[(y1 * W + x0) * Channels + c] + Src[(y1 * W + x1) * Channels + c];
					Out[(y * OutW + x) * Channels + c] = uint8((Sum + 2) / 4);
				}
			}
		}
		return Out;
	}
}

UTexture2D* otg::LoadTexture(const FString& File, ETexKind Kind, int32 MaxSize, UObject* Outer)
{
	return LoadTexture(File, Kind, MaxSize, Outer, nullptr);
}

UTexture2D* otg::LoadTexture(const FString& File, ETexKind Kind, int32 MaxSize, UObject* Outer, FPicture* OutPixels)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *File)) return nullptr;
	IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const EImageFormat Format = Module.DetectImageFormat(Bytes.GetData(), Bytes.Num());
	TSharedPtr<IImageWrapper> Img = Module.CreateImageWrapper(Format);
	const bool bGrey = Kind == ETexKind::Mask;
	TArray64<uint8> Raw;
	if (!Img.IsValid() || !Img->SetCompressed(Bytes.GetData(), Bytes.Num()) || !Img->GetRaw(bGrey ? ERGBFormat::Gray : ERGBFormat::BGRA, 8, Raw))
	{
		UE_LOG(LogOTGTextures, Warning, TEXT("Can't decode %s"), *File);
		return nullptr;
	}
	int32 W = Img->GetWidth(), H = Img->GetHeight();
	const int32 Ch = bGrey ? 1 : 4;
	TArray<uint8> Top(Raw.GetData(), static_cast<int32>(Raw.Num()));
	// (no bigger than MaxSize: halved until it fits)
	while (W > MaxSize || H > MaxSize)
	{
		int32 NW, NH;
		Top = Halve(Top, W, H, Ch, NW, NH);
		W = NW; H = NH;
	}
	if (OutPixels) { OutPixels->W = W; OutPixels->H = H; OutPixels->Channels = Ch; OutPixels->Pixels = Top; }
	UTexture2D* T = UTexture2D::CreateTransient(W, H, bGrey ? PF_G8 : PF_B8G8R8A8, NAME_None);
	if (!T) return nullptr;
	T->SRGB = Kind == ETexKind::Photo;
	T->CompressionSettings = bGrey ? TC_Grayscale : (Kind == ETexKind::Data ? TC_VectorDisplacementmap : TC_Default);
	T->Filter = TF_Trilinear;
	T->AddressX = T->AddressY = TA_Clamp;
	T->LODGroup = TEXTUREGROUP_World;
	T->NeverStream = true;
	// the mips, each half the last, down to 1 x 1
	FTexturePlatformData* PD = T->GetPlatformData();
	{
		void* Dst = PD->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Dst, Top.GetData(), Top.Num());
		PD->Mips[0].BulkData.Unlock();
	}
	TArray<uint8> Level = MoveTemp(Top);
	int32 LW = W, LH = H;
	while (LW > 1 || LH > 1)
	{
		int32 NW, NH;
		Level = Halve(Level, LW, LH, Ch, NW, NH);
		LW = NW; LH = NH;
		FTexture2DMipMap* Mip = new FTexture2DMipMap(LW, LH, 1);
		PD->Mips.Add(Mip);
		Mip->BulkData.Lock(LOCK_READ_WRITE);
		void* Dst = Mip->BulkData.Realloc(Level.Num());
		FMemory::Memcpy(Dst, Level.GetData(), Level.Num());
		Mip->BulkData.Unlock();
	}
	T->UpdateResource();
	UE_LOG(LogOTGTextures, Display, TEXT("%s: %d x %d, %d mips"), *FPaths::GetCleanFilename(File), W, H, PD->Mips.Num());
	return T;
}
