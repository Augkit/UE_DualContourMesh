#pragma once

#include "IDetailCustomization.h"
#include "Styling/SlateColor.h"

class ADualContourMeshActor;
struct FSlateBrush;

class FDualContourMeshActorDetails final : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	bool AreDivisionsValid(FString* OutStatus = nullptr) const;
	const FSlateBrush* GetValidationIcon() const;
	FSlateColor GetValidationIconColor() const;
	FText GetValidationTooltip() const;

	TArray<TWeakObjectPtr<ADualContourMeshActor>> CustomizedActors;
};
