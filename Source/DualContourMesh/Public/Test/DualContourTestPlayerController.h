#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "DualContourTestPlayerController.generated.h"

UCLASS()
class DUALCONTOURMESH_API ADualContourTestPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DualContour")
	float HemisphereRadius = 50.f;

protected:
	virtual void SetupInputComponent() override;

private:
	void OnLeftClick();
	void OnRightClick();
	void DoHemisphereEdit(bool bExcavate);
};
