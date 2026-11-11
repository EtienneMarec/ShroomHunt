#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ShroomDebugSubsystem.generated.h"

/**
 * On-screen debug HUD (date, time scale, weather variables, rain history).
 * Toggle with the console variable `sh.Debug.HUD 0/1`. Not compiled into shipping builds' output.
 */
UCLASS()
class SHROOMHUNT_API UShroomDebugSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	float RefreshTimer = 0.0f;
	float CachedRain24h = 0.0f;
	float CachedRainWindow = 0.0f;
};
