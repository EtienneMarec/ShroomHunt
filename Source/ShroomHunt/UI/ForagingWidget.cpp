#include "UI/ForagingWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

UTextBlock* UForagingWidget::MakeText(int32 Size, const FLinearColor& Color)
{
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	FSlateFontInfo Font = Text->GetFont();
	Font.Size = Size;
	Text->SetFont(Font);
	Text->SetColorAndOpacity(FSlateColor(Color));
	Text->SetShadowOffset(FVector2D(1.0, 1.0));
	Text->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f));
	return Text;
}

TSharedRef<SWidget> UForagingWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Root;

		// Prompt a little below the screen center (the crosshair).
		Prompt = MakeText(17, FLinearColor(0.95f, 0.93f, 0.85f));
		Prompt->SetJustification(ETextJustify::Center);
		UCanvasPanelSlot* PromptSlot = Root->AddChildToCanvas(Prompt);
		PromptSlot->SetAnchors(FAnchors(0.5f, 0.6f));
		PromptSlot->SetAlignment(FVector2D(0.5, 0.0));
		PromptSlot->SetAutoSize(true);

		// Basket, top right.
		Basket = MakeText(14, FLinearColor(0.9f, 0.88f, 0.8f));
		UCanvasPanelSlot* BasketSlot = Root->AddChildToCanvas(Basket);
		BasketSlot->SetAnchors(FAnchors(1.0f, 0.0f));
		BasketSlot->SetAlignment(FVector2D(1.0, 0.0));
		BasketSlot->SetPosition(FVector2D(-28.0, 24.0));
		BasketSlot->SetAutoSize(true);

		// Examine panel, right side.
		ExaminePanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		ExaminePanel->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.02f, 0.6f));
		ExaminePanel->SetPadding(FMargin(20.0f, 16.0f));
		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		ExaminePanel->SetContent(Column);
		ExamineTitle = MakeText(20, FLinearColor(1.0f, 0.95f, 0.8f));
		ExamineBody = MakeText(15, FLinearColor(0.92f, 0.92f, 0.9f));
		ExamineBody->SetAutoWrapText(true);
		ExamineHint = MakeText(13, FLinearColor(0.7f, 0.7f, 0.66f));
		Column->AddChildToVerticalBox(ExamineTitle)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
		Column->AddChildToVerticalBox(ExamineBody)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));
		Column->AddChildToVerticalBox(ExamineHint);
		UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(ExaminePanel);
		PanelSlot->SetAnchors(FAnchors(1.0f, 0.5f));
		PanelSlot->SetAlignment(FVector2D(1.0, 0.5));
		PanelSlot->SetPosition(FVector2D(-40.0, 0.0));
		PanelSlot->SetSize(FVector2D(460.0, 380.0));
		ExaminePanel->SetVisibility(ESlateVisibility::Collapsed);
	}
	return Super::RebuildWidget();
}

void UForagingWidget::SetPrompt(const FText& Text)
{
	if (Prompt)
	{
		Prompt->SetText(Text);
		Prompt->SetVisibility(Text.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
}

void UForagingWidget::ShowExamine(const FText& Title, const TArray<FText>& Lines, const FText& Hint)
{
	if (!ExaminePanel)
	{
		return;
	}
	ExamineTitle->SetText(Title);
	ExamineBody->SetText(FText::Join(FText::FromString(TEXT("\n")), Lines));
	ExamineHint->SetText(Hint);
	ExaminePanel->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UForagingWidget::HideExamine()
{
	if (ExaminePanel)
	{
		ExaminePanel->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UForagingWidget::SetBasket(const FText& Text)
{
	if (Basket)
	{
		Basket->SetText(Text);
	}
}
