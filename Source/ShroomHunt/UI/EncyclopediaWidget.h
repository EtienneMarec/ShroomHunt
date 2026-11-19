#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EncyclopediaWidget.generated.h"

class UScrollBox;
class UTextBlock;
class UVerticalBox;

/**
 * Minimal encyclopedia: one page per species the player has met or been taught, in a
 * scrolling list (mouse wheel). Built in C++ like the foraging HUD (D-029).
 */
UCLASS()
class SHROOMHUNT_API UEncyclopediaWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	struct FPage
	{
		FText Title;
		TArray<FText> Lines;
	};

	void SetPages(const FText& Header, const TArray<FPage>& Pages, const FText& Hint);

	/** Scrolls by mouse wheel steps (positive = up). */
	void Scroll(float Steps);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UTextBlock* MakeText(int32 Size, const FLinearColor& Color, float WrapAt);

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Header;

	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> List;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Hint;
};
