#include "UI/CounterWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Economy/EconomySubsystem.h"
#include "Knowledge/KnowledgeSubsystem.h"
#include "Mycology/SpeciesDatabaseSubsystem.h"
#include "Player/ForagingSettings.h"

#define LOCTEXT_NAMESPACE "ShroomHunt.Counter"

namespace CounterWidgetDetail
{
	constexpr float PanelWidth = 1100.0f;
	constexpr float PanelHeight = 680.0f;
	constexpr float ColumnWidth = 500.0f;
	const FLinearColor Title(1.0f, 0.94f, 0.78f);
	const FLinearColor Body(0.9f, 0.89f, 0.85f);
	const FLinearColor Dim(0.7f, 0.68f, 0.62f);
	const FLinearColor Good(0.7f, 0.92f, 0.6f);
	const FLinearColor Bad(1.0f, 0.55f, 0.45f);

	FText Euros(float Value)
	{
		FNumberFormattingOptions Options;
		Options.MinimumFractionalDigits = 2;
		Options.MaximumFractionalDigits = 2;
		return FText::Format(LOCTEXT("Euros", "{0} EUR"), FText::AsNumber(Value, &Options));
	}

	FText Percent(float Value)
	{
		return FText::AsPercent(FMath::Clamp(Value, 0.0f, 1.0f));
	}

	FText VerdictText(ESaleVerdict Verdict)
	{
		switch (Verdict)
		{
		case ESaleVerdict::Unlabelled: return LOCTEXT("Unlabelled", "refused: you cannot name it");
		case ESaleVerdict::Rotten:     return LOCTEXT("Rotten", "refused: too old or not fresh");
		case ESaleVerdict::Inedible:   return LOCTEXT("Inedible", "refused: not edible");
		case ESaleVerdict::Toxic:      return LOCTEXT("Toxic", "TOXIC: the buyer will refuse the whole basket");
		case ESaleVerdict::Banned:     return LOCTEXT("Banned", "the buyer will not deal with you for now");
		default:                       return FText::GetEmpty();
		}
	}
}

void UCounterButtonHandler::HandleClicked()
{
	if (UCounterWidget* Widget = Owner.Get())
	{
		Widget->OnAction(*this);
	}
}

UTextBlock* UCounterWidget::MakeText(int32 Size, const FLinearColor& Color, float WrapAt)
{
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	FSlateFontInfo Font = Text->GetFont();
	Font.Size = Size;
	Text->SetFont(Font);
	Text->SetColorAndOpacity(FSlateColor(Color));
	Text->SetWrapTextAt(WrapAt);
	return Text;
}

UButton* UCounterWidget::MakeButton(const FText& Label, UCounterButtonHandler* Handler, bool bEnabled)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	UTextBlock* Text = MakeText(14, FLinearColor(0.08f, 0.07f, 0.05f), 0.0f);
	Text->SetText(Label);
	Button->SetContent(Text);
	Button->SetIsEnabled(bEnabled);
	Handlers.Add(Handler);
	Button->OnClicked.AddDynamic(Handler, &UCounterButtonHandler::HandleClicked);
	return Button;
}

UEconomySubsystem* UCounterWidget::GetEconomy() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UEconomySubsystem>() : nullptr;
}

TSharedRef<SWidget> UCounterWidget::RebuildWidget()
{
	using namespace CounterWidgetDetail;
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Root;

		UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Panel->SetBrushColor(FLinearColor(0.10f, 0.09f, 0.07f, 0.94f));
		Panel->SetPadding(FMargin(28.0f, 22.0f));
		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		Panel->SetContent(Column);

		Header = MakeText(22, Title, PanelWidth - 60.0f);
		Column->AddChildToVerticalBox(Header)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));

		// Two columns: the basket on the left, the shop on the right.
		UHorizontalBox* Columns = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		UVerticalBoxSlot* ColumnsSlot = Column->AddChildToVerticalBox(Columns);
		ColumnsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		BasketList = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
		ShopList = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
		UHorizontalBoxSlot* Left = Columns->AddChildToHorizontalBox(BasketList);
		Left->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Left->SetPadding(FMargin(0.0f, 0.0f, 24.0f, 0.0f));
		Columns->AddChildToHorizontalBox(ShopList)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		Message = MakeText(15, Good, PanelWidth - 60.0f);
		Column->AddChildToVerticalBox(Message)->SetPadding(FMargin(0.0f, 10.0f, 0.0f, 0.0f));
		Hint = MakeText(13, Dim, PanelWidth - 60.0f);
		Column->AddChildToVerticalBox(Hint)->SetPadding(FMargin(0.0f, 6.0f, 0.0f, 0.0f));

		UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Panel);
		PanelSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		PanelSlot->SetAlignment(FVector2D(0.5, 0.5));
		PanelSlot->SetSize(FVector2D(PanelWidth, PanelHeight));
	}
	return Super::RebuildWidget();
}

void UCounterWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (UEconomySubsystem* Economy = GetEconomy())
	{
		EconomyChangedHandle = Economy->OnEconomyChanged.AddUObject(this, &UCounterWidget::Refresh);
	}
	Refresh();
}

void UCounterWidget::NativeDestruct()
{
	if (UEconomySubsystem* Economy = GetEconomy())
	{
		Economy->OnEconomyChanged.Remove(EconomyChangedHandle);
	}
	Super::NativeDestruct();
}

void UCounterWidget::Refresh()
{
	using namespace CounterWidgetDetail;
	UEconomySubsystem* Economy = GetEconomy();
	const UGameInstance* GameInstance = GetGameInstance();
	const USpeciesDatabaseSubsystem* Database = GameInstance ? GameInstance->GetSubsystem<USpeciesDatabaseSubsystem>() : nullptr;
	const UKnowledgeSubsystem* Knowledge = GameInstance ? GameInstance->GetSubsystem<UKnowledgeSubsystem>() : nullptr;
	if (!Economy || !Database || !Knowledge || !BasketList)
	{
		return;
	}
	Handlers.Reset();

	FText Standing = FText::Format(LOCTEXT("Header", "The buyer's counter    Money: {0}    Reputation: {1}/100"),
		Euros(Economy->GetMoney()), FText::AsNumber(FMath::RoundToInt(Economy->GetReputation())));
	if (Economy->IsBanned())
	{
		Standing = FText::Format(LOCTEXT("HeaderBanned", "{0}    Banned for {1} more day(s)"), Standing, FText::AsNumber(FMath::CeilToInt(Economy->GetBanRemainingGameDays())));
	}
	Header->SetText(Standing);

	// --- Basket -------------------------------------------------------------------------
	BasketList->ClearChildren();
	UTextBlock* BasketTitle = MakeText(18, Title, ColumnWidth);
	BasketTitle->SetText(LOCTEXT("BasketTitle", "Your basket"));
	BasketList->AddChild(BasketTitle);
	const TArray<FSpecimenAppraisal> Quotes = Economy->QuoteBasket();
	float Offer = 0.0f;
	int32 Sellable = 0;
	int32 Unsellable = 0;
	for (const FSpecimenAppraisal& Quote : Quotes)
	{
		const FMushroomSpeciesRow* Row = Database->FindSpecies(Quote.SpeciesId);
		const bool bKnown = Knowledge->GetLevel(Quote.SpeciesId) == EKnowledgeLevel::Identified;
		const FText Name = bKnown && Row ? Row->CommonName : LOCTEXT("UnknownSpecimen", "Unknown mushroom");
		const FText Weight = FText::Format(LOCTEXT("Grams", "{0} g"), FText::AsNumber(FMath::RoundToInt(Quote.WeightKg * 1000.0f)));
		FText Line;
		FLinearColor Color = Body;
		if (Quote.Verdict == ESaleVerdict::Sold)
		{
			Offer += Quote.PriceEuros;
			++Sellable;
			Line = FText::Format(LOCTEXT("QuoteSold", "{0}, {1}: {2}  (quality {3}{4})"), Name, Weight, Euros(Quote.PriceEuros), Percent(Quote.Quality),
				Quote.bWormy ? LOCTEXT("Wormy", ", wormy") : FText::GetEmpty());
		}
		else
		{
			++Unsellable;
			Color = Quote.Verdict == ESaleVerdict::Toxic ? Bad : Dim;
			Line = FText::Format(LOCTEXT("QuoteRefused", "{0}, {1}: {2}"), Name, Weight, VerdictText(Quote.Verdict));
		}
		UTextBlock* Text = MakeText(14, Color, ColumnWidth);
		Text->SetText(Line);
		BasketList->AddChild(Text);
	}
	if (Quotes.Num() == 0)
	{
		UTextBlock* Empty = MakeText(14, Dim, ColumnWidth);
		Empty->SetText(LOCTEXT("BasketEmpty", "Empty. Go and pick some mushrooms."));
		BasketList->AddChild(Empty);
	}
	UHorizontalBox* Actions = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	UCounterButtonHandler* SellHandler = NewObject<UCounterButtonHandler>(this);
	SellHandler->Owner = this;
	SellHandler->bSell = true;
	Actions->AddChildToHorizontalBox(MakeButton(FText::Format(LOCTEXT("Sell", "Sell ({0})"), Euros(Offer)), SellHandler, Sellable > 0 || Unsellable > 0))
		->SetPadding(FMargin(0.0f, 10.0f, 12.0f, 0.0f));
	UCounterButtonHandler* DiscardHandler = NewObject<UCounterButtonHandler>(this);
	DiscardHandler->Owner = this;
	DiscardHandler->bDiscard = true;
	Actions->AddChildToHorizontalBox(MakeButton(LOCTEXT("Discard", "Throw away the refused ones"), DiscardHandler, Unsellable > 0))
		->SetPadding(FMargin(0.0f, 10.0f, 0.0f, 0.0f));
	BasketList->AddChild(Actions);

	// --- Shop ---------------------------------------------------------------------------
	ShopList->ClearChildren();
	UTextBlock* ShopTitle = MakeText(18, Title, ColumnWidth);
	ShopTitle->SetText(LOCTEXT("ShopTitle", "For sale"));
	ShopList->AddChild(ShopTitle);
	for (const FShopItem& Item : Economy->GetShop().Items)
	{
		const int32 Owned = Economy->GetState().GetItemCount(Item.Id);
		UTextBlock* Name = MakeText(15, Body, ColumnWidth);
		Name->SetText(FText::Format(LOCTEXT("ShopItem", "{0} - {1}"), Item.Title, Euros(Item.Price)));
		UTextBlock* Description = MakeText(13, Dim, ColumnWidth);
		Description->SetText(Item.Kind == EShopItemKind::Kit && Owned > 0
			? FText::Format(LOCTEXT("KitOwned", "{0} ({1} uses left)"), Item.Description, FText::AsNumber(Owned))
			: Item.Description);
		ShopList->AddChild(Name);
		ShopList->AddChild(Description);
		UCounterButtonHandler* Handler = NewObject<UCounterButtonHandler>(this);
		Handler->Owner = this;
		Handler->ItemId = Item.Id;
		const bool bOwnedBook = Item.Kind == EShopItemKind::Book && Owned > 0;
		UButton* Button = MakeButton(bOwnedBook ? LOCTEXT("Owned", "Owned") : LOCTEXT("Buy", "Buy"), Handler, Economy->GetState().CanBuy(Item));
		ShopList->AddChild(Button);
		UTextBlock* Gap = MakeText(8, FLinearColor::Transparent, ColumnWidth);
		Gap->SetText(FText::FromString(TEXT(" ")));
		ShopList->AddChild(Gap);
	}

	Hint->SetText(FText::Format(LOCTEXT("CounterHint", "Mouse: click    [{0}] Leave the counter"), GetDefault<UForagingSettings>()->ExamineKey.GetDisplayName()));
}

void UCounterWidget::OnAction(const UCounterButtonHandler& Handler)
{
	using namespace CounterWidgetDetail;
	UEconomySubsystem* Economy = GetEconomy();
	if (!Economy)
	{
		return;
	}
	FText Text;
	bool bGood = true;
	if (Handler.bSell)
	{
		const FSaleReport Report = Economy->SellBasket();
		if (Report.bRefusedToxic)
		{
			bGood = false;
			Text = Report.bDeadly
				? FText::Format(LOCTEXT("SaleDeadly", "A DEADLY mushroom in your basket! The buyer confiscates it, calls the gendarmerie and refuses everything: fine {0}, reputation {1}."),
					Euros(Report.Fine), FText::AsNumber(FMath::RoundToInt(Report.ReputationChange)))
				: FText::Format(LOCTEXT("SaleToxic", "A toxic mushroom in your basket! The buyer confiscates it and refuses everything: fine {0}, reputation {1}."),
					Euros(Report.Fine), FText::AsNumber(FMath::RoundToInt(Report.ReputationChange)));
		}
		else if (Report.SoldCount > 0)
		{
			Text = FText::Format(LOCTEXT("SaleDone", "Sold {0} mushroom(s), {1} kg, for {2}. {3} given back."),
				FText::AsNumber(Report.SoldCount), FText::AsNumber(Report.SoldKg), Euros(Report.Earned), FText::AsNumber(Report.ReturnedCount));
		}
		else
		{
			bGood = false;
			Text = LOCTEXT("SaleNothing", "The buyer takes nothing.");
		}
	}
	else if (Handler.bDiscard)
	{
		Text = FText::Format(LOCTEXT("Discarded", "Threw away {0} mushroom(s)."), FText::AsNumber(Economy->DiscardUnsellable()));
	}
	else
	{
		bGood = Economy->Buy(Handler.ItemId, Text);
	}
	// The economy broadcast already refreshed the lists; show what happened.
	Message->SetColorAndOpacity(FSlateColor(bGood ? Good : Bad));
	Message->SetText(Text);
}

#undef LOCTEXT_NAMESPACE
