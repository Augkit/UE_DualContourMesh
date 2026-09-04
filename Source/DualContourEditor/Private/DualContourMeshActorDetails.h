#pragma once

#include "IDetailCustomization.h"
#include "Styling/SlateColor.h"

class ADualContourMeshActor;
class FReply;
struct FSlateBrush;

class FDualContourMeshActorDetails final : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	bool CanSaveDualContour() const;
	FReply SaveDualContour();
	bool CanSaveDualContourAs() const;
	FReply SaveDualContourAs();
	bool AreDivisionsValid(FString* OutStatus = nullptr) const;
	const FSlateBrush* GetValidationIcon() const;
	FSlateColor GetValidationIconColor() const;
	FText GetValidationTooltip() const;

	TArray<TWeakObjectPtr<ADualContourMeshActor>> CustomizedActors;
};
