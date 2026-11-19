#include "UI/EncyclopediaWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace EncyclopediaWidgetDetail
{
	constexpr float PanelWidth = 860.0f;
	constexpr float PanelHeight = 640.0f;
	constexpr float TextWidth = PanelWidth - 80.0f;
}

UTextBlock* UEncyclopediaWidget::MakeText(int32 Size, const FLinearColor& Color, float WrapAt)
{
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	FSlateFontInfo Font = Text->GetFont();
	Font.Size = Size;
	Text->SetFont(Font);
	Text->SetColorAndOpacity(FSlateColor(Color));
	Text->SetWrapTextAt(WrapAt);
	return Text;
}

TSharedRef<SWidget> UEncyclopediaWidget::RebuildWidget()
{
	using namespace EncyclopediaWidgetDetail;
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Root;

		// A notebook-like page in the middle of the screen.
		UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Panel->SetBrushColor(FLinearColor(0.10f, 0.09f, 0.07f, 0.92f));
		Panel->SetPadding(FMargin(28.0f, 22.0f));
		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		Panel->SetContent(Column);
		Header = MakeText(24, FLinearColor(1.0f, 0.94f, 0.78f), TextWidth);
		List = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
		Hint = MakeText(13, FLinearColor(0.7f, 0.68f, 0.62f), TextWidth);
		Column->AddChildToVerticalBox(Header)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 14.0f));
		UVerticalBoxSlot* ListSlot = Column->AddChildToVerticalBox(List);
		ListSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Column->AddChildToVerticalBox(Hint)->SetPadding(FMargin(0.0f, 12.0f, 0.0f, 0.0f));

		UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Panel);
		PanelSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		PanelSlot->SetAlignment(FVector2D(0.5, 0.5));
		PanelSlot->SetSize(FVector2D(PanelWidth, PanelHeight));
	}
	return Super::RebuildWidget();
}

void UEncyclopediaWidget::SetPages(const FText& InHeader, const TArray<FPage>& Pages, const FText& InHint)
{
	using namespace EncyclopediaWidgetDetail;
	if (!List)
	{
		return;
	}
	Header->SetText(InHeader);
	Hint->SetText(InHint);
	List->ClearChildren();
	for (const FPage& Page : Pages)
	{
		UTextBlock* Title = MakeText(18, FLinearColor(0.98f, 0.86f, 0.55f), TextWidth);
		Title->SetText(Page.Title);
		UTextBlock* Body = MakeText(14, FLinearColor(0.9f, 0.89f, 0.85f), TextWidth);
		Body->SetText(FText::Join(FText::FromString(TEXT("\n")), Page.Lines));
		List->AddChild(Title);
		List->AddChild(Body);
		// Spacer between pages.
		UTextBlock* Gap = MakeText(10, FLinearColor::Transparent, TextWidth);
		Gap->SetText(FText::FromString(TEXT(" ")));
		List->AddChild(Gap);
	}
	List->ScrollToStart();
}

void UEncyclopediaWidget::Scroll(float Steps)
{
	if (List)
	{
		List->SetScrollOffset(FMath::Max(0.0f, List->GetScrollOffset() - Steps * 60.0f));
	}
}
