#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Widgets/SWidget.h"
#include "DualContourSaveLoadWidget.generated.h"

class UButton;
class UTextBlock;

/** Runtime save/load panel built entirely from native UMG widgets. */
UCLASS()
class DUALCONTOURGAMEPLAY_API UDualContourSaveLoadWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UDualContourSaveLoadWidget(const FObjectInitializer& ObjectInitializer);

	void SetStatusMessage(const FText& InMessage);
	int32 GetSelectedSlot() const { return SelectedSlot; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;

private:
	void BuildWidget();
	void RefreshSelectionVisuals();

	UFUNCTION()
	void OnSlot0Clicked();
	UFUNCTION()
	void OnSlot1Clicked();
	UFUNCTION()
	void OnSlot2Clicked();
	UFUNCTION()
	void OnSaveClicked();
	UFUNCTION()
	void OnLoadClicked();
	UFUNCTION()
	void OnClearClicked();
	UFUNCTION()
	void OnCloseClicked();

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> SlotButtons;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> SlotLabels;

	int32 SelectedSlot = 0;
};
