#include "DualContourSaveLoadWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "DualContourGamePlayerController.h"
#include "Styling/CoreStyle.h"

namespace
{
	UTextBlock* MakeLabel(UWidgetTree* WidgetTree, const FText& Text, int32 FontSize)
	{
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
		Label->SetText(Text);
		Label->SetColorAndOpacity(FLinearColor::White);
		Label->SetFont(FSlateFontInfo(FCoreStyle::GetDefaultFont(), FontSize));
		return Label;
	}

	void AddVerticalPadding(UVerticalBox* Box, UWidgetTree* WidgetTree, float Height)
	{
		USizeBox* Spacer = WidgetTree->ConstructWidget<USizeBox>();
		Spacer->SetHeightOverride(Height);
		Box->AddChildToVerticalBox(Spacer);
	}
}

UDualContourSaveLoadWidget::UDualContourSaveLoadWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
}

void UDualContourSaveLoadWidget::NativeConstruct()
{
	Super::NativeConstruct();
}

TSharedRef<SWidget> UDualContourSaveLoadWidget::RebuildWidget()
{
	// UUserWidget::RebuildWidget creates the Slate root before NativeConstruct.
	// Build the native UMG tree first, otherwise the viewport receives the
	// default empty SSpacer even though input mode changes successfully.
	BuildWidget();
	return Super::RebuildWidget();
}

void UDualContourSaveLoadWidget::BuildWidget()
{
	if (!WidgetTree || WidgetTree->RootWidget)
		return;

	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>();
	WidgetTree->RootWidget = Root;

	UBorder* DimBackground = WidgetTree->ConstructWidget<UBorder>();
	DimBackground->SetBrushColor(FLinearColor(0.015f, 0.02f, 0.03f, 0.86f));
	if (UOverlaySlot* BackgroundSlot = Root->AddChildToOverlay(DimBackground))
	{
		BackgroundSlot->SetHorizontalAlignment(HAlign_Fill);
		BackgroundSlot->SetVerticalAlignment(VAlign_Fill);
	}

	USizeBox* PanelSize = WidgetTree->ConstructWidget<USizeBox>();
	PanelSize->SetWidthOverride(560.0f);
	PanelSize->SetHeightOverride(440.0f);
	if (UOverlaySlot* PanelSlot = Root->AddChildToOverlay(PanelSize))
	{
		PanelSlot->SetHorizontalAlignment(HAlign_Center);
		PanelSlot->SetVerticalAlignment(VAlign_Center);
	}

	UBorder* PanelBackground = WidgetTree->ConstructWidget<UBorder>();
	PanelBackground->SetPadding(FMargin(32.0f));
	PanelBackground->SetBrushColor(FLinearColor(0.055f, 0.07f, 0.10f, 0.98f));
	PanelSize->AddChild(PanelBackground);

	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>();
	PanelBackground->AddChild(Content);

	UTextBlock* Title = MakeLabel(WidgetTree, FText::FromString(TEXT("存档 / 读档")), 30);
	Title->SetJustification(ETextJustify::Center);
	Content->AddChildToVerticalBox(Title);
	AddVerticalPadding(Content, WidgetTree, 16.0f);

	UTextBlock* Hint = MakeLabel(WidgetTree, FText::FromString(TEXT("选择一个槽位，然后点击保存或加载")), 16);
	Hint->SetJustification(ETextJustify::Center);
	Hint->SetColorAndOpacity(FLinearColor(0.72f, 0.78f, 0.86f, 1.0f));
	Content->AddChildToVerticalBox(Hint);
	AddVerticalPadding(Content, WidgetTree, 16.0f);

	SlotButtons.Reserve(3);
	SlotLabels.Reserve(3);
	for (int32 SlotIndex = 0; SlotIndex < 3; ++SlotIndex)
	{
		UButton* SlotButton = WidgetTree->ConstructWidget<UButton>();
		UTextBlock* SlotLabel = MakeLabel(WidgetTree, FText::GetEmpty(), 20);
		SlotLabel->SetJustification(ETextJustify::Center);
		SlotButton->AddChild(SlotLabel);
		Content->AddChildToVerticalBox(SlotButton);
		if (UVerticalBoxSlot* ButtonSlot = Cast<UVerticalBoxSlot>(SlotButton->Slot))
		{
			ButtonSlot->SetPadding(FMargin(0.0f, 4.0f));
		}

		SlotButtons.Add(SlotButton);
		SlotLabels.Add(SlotLabel);
	}
	SlotButtons[0]->OnClicked.AddDynamic(this, &UDualContourSaveLoadWidget::OnSlot0Clicked);
	SlotButtons[1]->OnClicked.AddDynamic(this, &UDualContourSaveLoadWidget::OnSlot1Clicked);
	SlotButtons[2]->OnClicked.AddDynamic(this, &UDualContourSaveLoadWidget::OnSlot2Clicked);

	AddVerticalPadding(Content, WidgetTree, 12.0f);
	UHorizontalBox* Actions = WidgetTree->ConstructWidget<UHorizontalBox>();
	Content->AddChildToVerticalBox(Actions);

	UButton* SaveButton = WidgetTree->ConstructWidget<UButton>();
	SaveButton->AddChild(MakeLabel(WidgetTree, FText::FromString(TEXT("保存")), 18));
	Actions->AddChildToHorizontalBox(SaveButton);
	if (UHorizontalBoxSlot* SaveSlot = Cast<UHorizontalBoxSlot>(SaveButton->Slot))
	{
		SaveSlot->SetPadding(FMargin(4.0f));
		SaveSlot->SetHorizontalAlignment(HAlign_Fill);
	}
	SaveButton->OnClicked.AddDynamic(this, &UDualContourSaveLoadWidget::OnSaveClicked);

	UButton* LoadButton = WidgetTree->ConstructWidget<UButton>();
	LoadButton->AddChild(MakeLabel(WidgetTree, FText::FromString(TEXT("加载")), 18));
	Actions->AddChildToHorizontalBox(LoadButton);
	if (UHorizontalBoxSlot* LoadSlot = Cast<UHorizontalBoxSlot>(LoadButton->Slot))
	{
		LoadSlot->SetPadding(FMargin(4.0f));
		LoadSlot->SetHorizontalAlignment(HAlign_Fill);
	}
	LoadButton->OnClicked.AddDynamic(this, &UDualContourSaveLoadWidget::OnLoadClicked);

	UButton* ClearButton = WidgetTree->ConstructWidget<UButton>();
	UTextBlock* ClearLabel = MakeLabel(WidgetTree, FText::FromString(TEXT("清空")), 18);
	ClearLabel->SetColorAndOpacity(FLinearColor(1.0f, 0.45f, 0.4f, 1.0f));
	ClearButton->AddChild(ClearLabel);
	Actions->AddChildToHorizontalBox(ClearButton);
	if (UHorizontalBoxSlot* ClearSlot = Cast<UHorizontalBoxSlot>(ClearButton->Slot))
	{
		ClearSlot->SetPadding(FMargin(4.0f));
		ClearSlot->SetHorizontalAlignment(HAlign_Fill);
	}
	ClearButton->OnClicked.AddDynamic(this, &UDualContourSaveLoadWidget::OnClearClicked);

	UButton* CloseButton = WidgetTree->ConstructWidget<UButton>();
	CloseButton->AddChild(MakeLabel(WidgetTree, FText::FromString(TEXT("关闭")), 18));
	Actions->AddChildToHorizontalBox(CloseButton);
	if (UHorizontalBoxSlot* CloseSlot = Cast<UHorizontalBoxSlot>(CloseButton->Slot))
	{
		CloseSlot->SetPadding(FMargin(4.0f));
		CloseSlot->SetHorizontalAlignment(HAlign_Fill);
	}
	CloseButton->OnClicked.AddDynamic(this, &UDualContourSaveLoadWidget::OnCloseClicked);

	AddVerticalPadding(Content, WidgetTree, 12.0f);
	StatusText = MakeLabel(WidgetTree, FText::FromString(TEXT("按 Tab 返回游戏")), 15);
	StatusText->SetJustification(ETextJustify::Center);
	StatusText->SetColorAndOpacity(FLinearColor(0.64f, 0.72f, 0.82f, 1.0f));
	Content->AddChildToVerticalBox(StatusText);

	RefreshSelectionVisuals();
}

void UDualContourSaveLoadWidget::SetStatusMessage(const FText& InMessage)
{
	if (StatusText)
		StatusText->SetText(InMessage);
}

void UDualContourSaveLoadWidget::RefreshSelectionVisuals()
{
	for (int32 Index = 0; Index < SlotLabels.Num(); ++Index)
	{
		const bool bSelected = Index == SelectedSlot;
		const FString Label = FString::Printf(TEXT("槽位 %d%s"), Index + 1,
			bSelected ? TEXT("  (已选择)") : TEXT(""));
		if (SlotLabels[Index])
		{
			SlotLabels[Index]->SetText(FText::FromString(Label));
			SlotLabels[Index]->SetColorAndOpacity(bSelected
				? FLinearColor(0.35f, 0.85f, 1.0f, 1.0f)
				: FLinearColor::White);
		}
	}
}

void UDualContourSaveLoadWidget::OnSlot0Clicked()
{
	SelectedSlot = 0;
	RefreshSelectionVisuals();
}

void UDualContourSaveLoadWidget::OnSlot1Clicked()
{
	SelectedSlot = 1;
	RefreshSelectionVisuals();
}

void UDualContourSaveLoadWidget::OnSlot2Clicked()
{
	SelectedSlot = 2;
	RefreshSelectionVisuals();
}

void UDualContourSaveLoadWidget::OnSaveClicked()
{
	if (ADualContourGamePlayerController* Controller = Cast<ADualContourGamePlayerController>(GetOwningPlayer()))
		Controller->SaveToSlot(SelectedSlot);
}

void UDualContourSaveLoadWidget::OnLoadClicked()
{
	if (ADualContourGamePlayerController* Controller = Cast<ADualContourGamePlayerController>(GetOwningPlayer()))
		Controller->LoadFromSlot(SelectedSlot);
}

void UDualContourSaveLoadWidget::OnClearClicked()
{
	if (ADualContourGamePlayerController* Controller = Cast<ADualContourGamePlayerController>(GetOwningPlayer()))
		Controller->ClearSlot(SelectedSlot);
}

void UDualContourSaveLoadWidget::OnCloseClicked()
{
	if (ADualContourGamePlayerController* Controller = Cast<ADualContourGamePlayerController>(GetOwningPlayer()))
		Controller->CloseSaveLoadWidget();
}
