// Knowledge debug console commands: sh.Knowledge.Status / UnlockAll / Reset.

#include "Core/ShroomLog.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Knowledge/KnowledgeSubsystem.h"
#include "Mycology/SpeciesDatabaseSubsystem.h"
#include "Time/CalendarSubsystem.h"

#if !UE_BUILD_SHIPPING

namespace KnowledgeConsoleCommandsDetail
{
	UKnowledgeSubsystem* FindKnowledge(UWorld* World)
	{
		UKnowledgeSubsystem* Knowledge = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UKnowledgeSubsystem>() : nullptr;
		if (!Knowledge)
		{
			UE_LOG(LogShroomDebug, Warning, TEXT("Knowledge commands need a game world (Play)"));
		}
		return Knowledge;
	}

	FAutoConsoleCommandWithWorld CmdKnowledgeStatus(
		TEXT("sh.Knowledge.Status"),
		TEXT("sh.Knowledge.Status: owned sources and the knowledge level of each species."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			const UKnowledgeSubsystem* Knowledge = FindKnowledge(World);
			const USpeciesDatabaseSubsystem* Database = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<USpeciesDatabaseSubsystem>() : nullptr;
			if (!Knowledge || !Database)
			{
				return;
			}
			for (const FKnowledgeSource& Source : Knowledge->GetSources().Sources)
			{
				UE_LOG(LogShroomDebug, Display, TEXT("Source %-16s %s"), *Source.Id.ToString(), Knowledge->HasSource(Source.Id) ? TEXT("owned") : TEXT("-"));
			}
			for (const FName Id : Database->GetAllSpeciesIds())
			{
				const FSpeciesKnowledge* Seen = Knowledge->GetState().Find(Id);
				UE_LOG(LogShroomDebug, Display, TEXT("Species %-22s %-10s examined %d, picked %d"), *Id.ToString(),
					*StaticEnum<EKnowledgeLevel>()->GetNameStringByValue(static_cast<int64>(Knowledge->GetLevel(Id))),
					Seen ? Seen->Observations : 0, Seen ? Seen->Picks : 0);
			}
		}));

	FAutoConsoleCommandWithWorld CmdKnowledgeUnlockAll(
		TEXT("sh.Knowledge.UnlockAll"),
		TEXT("sh.Knowledge.UnlockAll: owns every source and identifies every species."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			UKnowledgeSubsystem* Knowledge = FindKnowledge(World);
			const USpeciesDatabaseSubsystem* Database = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<USpeciesDatabaseSubsystem>() : nullptr;
			const UCalendarSubsystem* Calendar = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UCalendarSubsystem>() : nullptr;
			if (Knowledge && Database)
			{
				Knowledge->UnlockAll(Database->GetAllSpeciesIds(), Calendar ? Calendar->GetTotalGameSeconds() : 0.0);
				UE_LOG(LogShroomDebug, Display, TEXT("Knowledge: everything unlocked"));
			}
		}));

	FAutoConsoleCommandWithWorld CmdKnowledgeReset(
		TEXT("sh.Knowledge.Reset"),
		TEXT("sh.Knowledge.Reset: forgets every source and species (taken pages come back)."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (UKnowledgeSubsystem* Knowledge = FindKnowledge(World))
			{
				Knowledge->ResetKnowledge();
				UE_LOG(LogShroomDebug, Display, TEXT("Knowledge: reset"));
			}
		}));
}

#endif // !UE_BUILD_SHIPPING
