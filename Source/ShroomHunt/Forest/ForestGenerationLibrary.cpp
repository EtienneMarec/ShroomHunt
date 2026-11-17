#include "Forest/ForestGenerationLibrary.h"

#include "Core/ShroomLog.h"
#include "Forest/ForestBake.h"
#include "Forest/ForestDefinition.h"
#include "Forest/ForestField.h"

FString UForestGenerationLibrary::BakeForest(const FString& ForestName)
{
	FForestDefinition Definition;
	TArray<FString> Errors;
	if (!ForestData::LoadForest(ForestName, Definition, Errors))
	{
		const FString Report = TEXT("FAILED: ") + FString::Join(Errors, TEXT("\n"));
		UE_LOG(LogShroomForest, Error, TEXT("%s"), *Report);
		return Report;
	}

	FForestField Field;
	if (!Field.Generate(Definition))
	{
		return TEXT("FAILED: field generation");
	}

	FString Report;
	ForestBake::BakeAll(Field, ForestBake::GetDefaultOutputFolder(ForestName), Report);
	return Report;
}
