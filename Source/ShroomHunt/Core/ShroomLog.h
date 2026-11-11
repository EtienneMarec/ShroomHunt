#pragma once

#include "CoreMinimal.h"

/** One log category per gameplay domain (filter in the Output Log with e.g. "LogShroomWeather"). */
DECLARE_LOG_CATEGORY_EXTERN(LogShroomTime, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogShroomWeather, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogShroomEnvironment, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogShroomForest, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogShroomSpawn, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogShroomMesh, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogShroomKnowledge, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogShroomSave, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogShroomDebug, Log, All);
