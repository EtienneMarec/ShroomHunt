#include "Environment/GroundTextureLibrary.h"

#include "Core/ShroomLog.h"
#include "Environment/GroundTextures.h"
#include "Forest/ForestFieldCache.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Vegetation/VegetationDefinition.h"
#include "Vegetation/VegetationSettings.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "Misc/PackageName.h"
#include "Misc/ScopedSlowTask.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#endif

namespace GroundTextureLibraryDetail
{
	FString GetPreviewFolder()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("GroundTextures"));
	}

	bool WriteGroundPng(const FString& Path, const TArray<FColor>& Pixels, int32 Size, bool bWithAlpha)
	{
		TArray<FColor> Copy = Pixels;
		if (!bWithAlpha)
		{
			for (FColor& C : Copy)
			{
				C.A = 255;
			}
		}
		IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
		if (!Wrapper.IsValid() || !Wrapper->SetRaw(Copy.GetData(), Copy.Num() * sizeof(FColor), Size, Size, ERGBFormat::BGRA, 8))
		{
			return false;
		}
		return FFileHelper::SaveArrayToFile(Wrapper->GetCompressed(), *Path);
	}

	/** Loads the ground data and the vegetation (for the leaf shapes and colors). */
	bool LoadInputs(FGroundTexturesDefinition& OutGround, TSharedPtr<const FVegetationDefinition>& OutVegetation, TArray<FString>& OutErrors)
	{
		OutVegetation = ForestFieldCache::GetVegetation(OutErrors);
		return GroundTextures::Load(OutGround, OutErrors) && OutVegetation.IsValid();
	}

	FString GetStampPath()
	{
		return FPaths::ProjectSavedDir() / TEXT("GroundTextureBake.txt");
	}

	FString HashFile(const FString& Path)
	{
		FString Text;
		FFileHelper::LoadFileToString(Text, *Path);
		Text.ReplaceInline(TEXT("\r"), TEXT(""));
		return FString::Printf(TEXT("%08x"), FCrc::StrCrc32(*Text));
	}

	/** The textures depend on the ground data, the leaf data of the vegetation, the generator and the folder. */
	FString ComputeStamp()
	{
		return FString::Printf(TEXT("generator %d, ground %s, vegetation %s, folder %s"), GroundTextures::GeneratorVersion,
			*HashFile(GroundTextures::GetDefaultPath()),
			*HashFile(FPaths::Combine(FPaths::ProjectDir(), TEXT("Data"), TEXT("Vegetation"), TEXT("Vegetation.json"))),
			*GetDefault<UVegetationSettings>()->GroundTextureFolder);
	}

	FString GetPackageName(FName Layer, const TCHAR* Suffix)
	{
		FString Folder = GetDefault<UVegetationSettings>()->GroundTextureFolder;
		Folder.RemoveFromEnd(TEXT("/"));
		return Folder / FString::Printf(TEXT("T_Ground_%s_%s"), *Layer.ToString(), Suffix);
	}

#if WITH_EDITOR
	/** Creates or updates a texture asset in place (references from materials stay valid) and saves it. */
	bool SaveTexture(const FString& PackageName, const TArray<FColor>& Pixels, int32 Size, bool bNormalMap, FString& OutError)
	{
		const FString AssetName = FPackageName::GetShortName(PackageName);
		UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *(PackageName + TEXT(".") + AssetName), nullptr, LOAD_NoWarn | LOAD_Quiet);
		const bool bCreated = Texture == nullptr;
		UPackage* Package = bCreated ? CreatePackage(*PackageName) : Texture->GetPackage();
		if (bCreated)
		{
			Texture = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		}
		Texture->PreEditChange(nullptr);
		Texture->Source.Init(Size, Size, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(Pixels.GetData()));
		Texture->SRGB = !bNormalMap;
		Texture->CompressionSettings = bNormalMap ? TC_Normalmap : TC_Default;
		Texture->LODGroup = bNormalMap ? TEXTUREGROUP_WorldNormalMap : TEXTUREGROUP_World;
		Texture->MipGenSettings = TMGS_FromTextureGroup;
		Texture->AddressX = TA_Wrap;
		Texture->AddressY = TA_Wrap;
		Texture->PostEditChange();
		Texture->MarkPackageDirty();
		if (bCreated)
		{
			FAssetRegistryModule::AssetCreated(Texture);
		}
		const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Package, Texture, *Filename, SaveArgs))
		{
			OutError = FString::Printf(TEXT("cannot save %s"), *Filename);
			return false;
		}
		return true;
	}
#endif

	/** Generates every layer; Visit(Style, Image) receives each one. */
	bool ForEachLayer(TFunctionRef<bool(const FGroundLayerStyle&, const FGroundTextureImage&)> Visit, FString& OutReport)
	{
		FGroundTexturesDefinition Ground;
		TSharedPtr<const FVegetationDefinition> Vegetation;
		TArray<FString> Errors;
		if (!LoadInputs(Ground, Vegetation, Errors))
		{
			OutReport = TEXT("FAILED\n") + FString::Join(Errors, TEXT("\n"));
			return false;
		}
		const double Start = FPlatformTime::Seconds();
		int32 Done = 0;
#if WITH_EDITOR
		FScopedSlowTask Progress(static_cast<float>(Ground.Layers.Num()), NSLOCTEXT("ShroomHunt", "BakeGround", "Drawing the ground textures"));
		Progress.MakeDialog();
#endif
		for (const FGroundLayerStyle& Style : Ground.Layers)
		{
#if WITH_EDITOR
			Progress.EnterProgressFrame(1.0f, FText::FromName(Style.Layer));
#endif
			const FGroundTextureImage Image = GroundTextures::Generate(Ground, Style, *Vegetation);
			const FString Folder = GetPreviewFolder();
			WriteGroundPng(Folder / Style.Layer.ToString() + TEXT("_D.png"), Image.Albedo, Image.Size, true);
			WriteGroundPng(Folder / Style.Layer.ToString() + TEXT("_N.png"), Image.Normal, Image.Size, false);
			WriteGroundPng(Folder / TEXT("preview_") + Style.Layer.ToString() + TEXT(".png"), GroundTextures::MakePreview(Image), Image.Size, false);
			if (!Visit(Style, Image))
			{
				Errors.Add(Style.Layer.ToString());
				continue;
			}
			++Done;
		}
		OutReport = FString::Printf(TEXT("%d of %d ground layers (%d px) in %.1f s, previews in %s"),
			Done, Ground.Layers.Num(), Ground.Resolution, FPlatformTime::Seconds() - Start, *GetPreviewFolder());
		if (Errors.Num() > 0)
		{
			OutReport = TEXT("FAILED: ") + OutReport + TEXT("\n") + FString::Join(Errors, TEXT("\n"));
			return false;
		}
		OutReport = TEXT("OK: ") + OutReport;
		return true;
	}
}

FString UGroundTextureLibrary::BakeGroundTextures()
{
#if WITH_EDITOR
	using namespace GroundTextureLibraryDetail;
	FString Report;
	const bool bOk = ForEachLayer([](const FGroundLayerStyle& Style, const FGroundTextureImage& Image)
	{
		FString Error;
		const bool bSaved = SaveTexture(GetPackageName(Style.Layer, TEXT("D")), Image.Albedo, Image.Size, false, Error)
			&& SaveTexture(GetPackageName(Style.Layer, TEXT("N")), Image.Normal, Image.Size, true, Error);
		if (!bSaved)
		{
			UE_LOG(LogShroomVegetation, Error, TEXT("Ground texture %s: %s"), *Style.Layer.ToString(), *Error);
		}
		return bSaved;
	}, Report);
	if (bOk)
	{
		FFileHelper::SaveStringToFile(ComputeStamp(), *GetStampPath());
	}
	return Report;
#else
	return TEXT("FAILED: baking textures needs the editor");
#endif
}

FString UGroundTextureLibrary::WriteGroundPreviews()
{
	FString Report;
	GroundTextureLibraryDetail::ForEachLayer([](const FGroundLayerStyle&, const FGroundTextureImage&) { return true; }, Report);
	return Report;
}

bool UGroundTextureLibrary::IsBakeUpToDate(FString& OutReason)
{
#if WITH_EDITOR
	using namespace GroundTextureLibraryDetail;
	FGroundTexturesDefinition Ground;
	TArray<FString> Errors;
	if (!GroundTextures::Load(Ground, Errors))
	{
		OutReason = TEXT("ground data invalid: ") + FString::Join(Errors, TEXT("; "));
		return true; // nothing sensible to bake
	}
	for (const FGroundLayerStyle& Style : Ground.Layers)
	{
		for (const TCHAR* Suffix : { TEXT("D"), TEXT("N") })
		{
			if (!FPackageName::DoesPackageExist(GetPackageName(Style.Layer, Suffix)))
			{
				OutReason = GetPackageName(Style.Layer, Suffix) + TEXT(" is missing");
				return false;
			}
		}
	}
	FString Stamp;
	if (!FFileHelper::LoadFileToString(Stamp, *GetStampPath()) || Stamp != ComputeStamp())
	{
		OutReason = TEXT("the ground data, the leaf data or the generator changed since the last bake");
		return false;
	}
	return true;
#else
	return true;
#endif
}

void UGroundTextureLibrary::BakeIfNeeded()
{
	FString Reason;
	if (IsBakeUpToDate(Reason))
	{
		return;
	}
	UE_LOG(LogShroomVegetation, Display, TEXT("Rebuilding the ground textures: %s"), *Reason);
	UE_LOG(LogShroomVegetation, Display, TEXT("%s"), *BakeGroundTextures().Left(400));
}
