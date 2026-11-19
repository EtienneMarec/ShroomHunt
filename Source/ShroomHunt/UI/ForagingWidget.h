#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ForagingWidget.generated.h"

class UBorder;
class UTextBlock;

/**
 * Foraging HUD: the prompt under the crosshair, the basket summary, and the examine panel
 * listing the observable criteria. Built in C++ (no widget Blueprint to author).
 */
UCLASS()
class SHROOMHUNT_API UForagingWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Prompt near the crosshair; empty text hides it. */
	void SetPrompt(const FText& Text);

	/** Examine panel on the right. */
	void ShowExamine(const FText& Title, const TArray<FText>& Lines, const FText& Hint);
	void HideExamine();

	void SetBasket(const FText& Text);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UTextBlock* MakeText(int32 Size, const FLinearColor& Color);

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Prompt;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Basket;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> ExaminePanel;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ExamineTitle;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ExamineBody;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ExamineHint;
};
