#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CounterWidget.generated.h"

class UButton;
class UCounterWidget;
class UEconomySubsystem;
class UScrollBox;
class UTextBlock;

/** One clickable action of the counter (a shop item, or a basket action). */
UCLASS()
class UCounterButtonHandler : public UObject
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<UCounterWidget> Owner;
	/** Shop item id, or NAME_None for the basket actions below. */
	FName ItemId;
	bool bSell = false;
	bool bDiscard = false;

	UFUNCTION()
	void HandleClicked();
};

/**
 * The buyer's counter at the forester's house (M3): the basket as the buyer values it
 * (price, quality, or why a specimen is refused), Sell and Discard buttons, and the shop
 * (field guides, test kits). Clicked with the mouse; built in C++ (D-029).
 */
UCLASS()
class SHROOMHUNT_API UCounterWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Rebuilds the lists from the economy (called on open and after every change). */
	void Refresh();

	void OnAction(const UCounterButtonHandler& Handler);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	UTextBlock* MakeText(int32 Size, const FLinearColor& Color, float WrapAt);
	UButton* MakeButton(const FText& Label, UCounterButtonHandler* Handler, bool bEnabled);
	UEconomySubsystem* GetEconomy() const;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Header;

	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> BasketList;

	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> ShopList;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Message;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Hint;

	/** Keeps the click handlers alive while their buttons exist. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UCounterButtonHandler>> Handlers;

	FDelegateHandle EconomyChangedHandle;
};
