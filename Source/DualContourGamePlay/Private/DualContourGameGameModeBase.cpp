#include "DualContourGameGameModeBase.h"
#include "DualContourGamePlayerController.h"
#include "GameFramework/Pawn.h"

ADualContourGameGameModeBase::ADualContourGameGameModeBase()
{
	PlayerControllerClass = ADualContourGamePlayerController::StaticClass();

	// The UE first person template character supplies the animated arms, locomotion and
	// head-socket camera; the controller adds mining, the weapon and the beam on top.
	static ConstructorHelpers::FClassFinder<APawn> FirstPersonPawnFinder(
		TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
	if (FirstPersonPawnFinder.Succeeded())
		DefaultPawnClass = FirstPersonPawnFinder.Class;
}
