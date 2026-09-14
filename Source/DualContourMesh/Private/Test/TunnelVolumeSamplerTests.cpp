#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "VolumeSampler/TunnelVolumeSampler.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTunnelVolumeSamplerGeometryTest,
	"DualContourMesh.VolumeSampler.Tunnel.Geometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTunnelVolumeSamplerGeometryTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UTunnelVolumeSampler> Tunnel(NewObject<UTunnelVolumeSampler>());
	FText Error;
	TestTrue(TEXT("Default tunnel settings prepare"), static_cast<UVolumeSampler*>(Tunnel.Get())->Prepare(Error));

	TestTrue(TEXT("Regular center is solid"), Tunnel->GetSignedDistance_Implementation(FVector(0.0, 0.0, 0.0)) < 0.0f);
	TestTrue(TEXT("Outside regular roof is air"), Tunnel->GetSignedDistance_Implementation(FVector(0.0, 0.0, 0.2)) > 0.0f);
	TestTrue(TEXT("Outside regular wall is air"), Tunnel->GetSignedDistance_Implementation(FVector(0.0, 0.333333, -0.166667)) > 0.0f);
	TestTrue(TEXT("Below the shared floor is air"), Tunnel->GetSignedDistance_Implementation(FVector(0.0, 0.0, -0.35)) > 0.0f);
	TestTrue(TEXT("Regular floor lies on the surface"),
		FMath::IsNearlyZero(Tunnel->GetSignedDistance_Implementation(FVector(0.0, 0.0, -0.333333))));

	TestTrue(TEXT("Tall point is solid in enlarged entrance"),
		Tunnel->GetSignedDistance_Implementation(FVector(-0.401786, 0.0, 0.25)) < 0.0f);
	TestTrue(TEXT("The same tall point is air in regular section"),
		Tunnel->GetSignedDistance_Implementation(FVector(0.0, 0.0, 0.25)) > 0.0f);
	TestTrue(TEXT("Before flat entrance is air"),
		Tunnel->GetSignedDistance_Implementation(FVector(-0.455357, 0.0, 0.0)) > 0.0f);

	TestTrue(TEXT("Rounded head lifts away from the body floor"),
		Tunnel->GetSignedDistance_Implementation(FVector(0.357143, 0.0, -0.325)) > 0.0f);
	TestTrue(TEXT("Rounded head has an interior"),
		Tunnel->GetSignedDistance_Implementation(FVector(0.357143, 0.0, -0.083333)) < 0.0f);
	TestTrue(TEXT("Head tip lies on the surface"),
		FMath::IsNearlyZero(Tunnel->GetSignedDistance_Implementation(FVector(0.446429, 0.0, -0.083333))));
	TestTrue(TEXT("Beyond head tip is air"),
		Tunnel->GetSignedDistance_Implementation(FVector(0.455357, 0.0, -0.083333)) > 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTunnelVolumeSamplerValidationTest,
	"DualContourMesh.VolumeSampler.Tunnel.Validation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTunnelVolumeSamplerValidationTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UTunnelVolumeSampler> Tunnel(NewObject<UTunnelVolumeSampler>());
	Tunnel->HeadLength = Tunnel->Length;

	FText Error;
	TestFalse(TEXT("Head cannot occupy the entire tunnel"),
		static_cast<UVolumeSampler*>(Tunnel.Get())->Prepare(Error));
	TestFalse(TEXT("Invalid settings provide an error"), Error.IsEmpty());
	return true;
}

#endif
