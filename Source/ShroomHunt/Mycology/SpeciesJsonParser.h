#pragma once

#include "CoreMinimal.h"
#include "Mycology/MushroomSpeciesTypes.h"

/**
 * Parser and validator for the species authoring format (Data/Species/*.json).
 *
 * The JSON uses a compact, human-friendly form (trapezoids as [a, b, c, d],
 * colors as "#RRGGBB", enums as names). This parser converts it into
 * FMushroomSpeciesRow and reports precise errors ("BoletusEdulis.json: Cap.DiameterCm:
 * min > max") so data mistakes are caught at import time and by automation tests.
 * Unknown keys are reported as warnings (typo detection); keys starting with "_" are
 * comments and ignored.
 */
namespace SpeciesJson
{
	struct FParseReport
	{
		TArray<FString> Errors;
		TArray<FString> Warnings;

		bool HasErrors() const { return Errors.Num() > 0; }

		/** All messages, one per line, errors first. */
		SHROOMHUNT_API FString ToString() const;
	};

	/** Parses one species. SourceName is used in messages and must match the "Name" field. */
	SHROOMHUNT_API bool ParseSpecies(const FString& JsonText, const FString& SourceName, FName& OutId, FMushroomSpeciesRow& OutRow, FParseReport& Report);

	/** Reads and parses one file (the file base name must equal the species id). */
	SHROOMHUNT_API bool ParseSpeciesFile(const FString& FilePath, FName& OutId, FMushroomSpeciesRow& OutRow, FParseReport& Report);

	/** Parses every *.json in a folder. Returns the number of valid species. Duplicate ids are errors. */
	SHROOMHUNT_API int32 LoadFolder(const FString& Folder, TMap<FName, FMushroomSpeciesRow>& OutSpecies, FParseReport& Report);

	/** <Project>/Data/Species */
	SHROOMHUNT_API FString GetDefaultFolder();
}
