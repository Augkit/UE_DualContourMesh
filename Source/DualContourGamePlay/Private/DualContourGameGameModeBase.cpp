#include "DualContourGameGameModeBase.h"
#include "DualContourFPCharacter.h"
#include "DualContourGamePlayerController.h"

ADualContourGameGameModeBase::ADualContourGameGameModeBase()
{
	PlayerControllerClass = ADualContourGamePlayerController::StaticClass();

	// First person character with an animated arms mesh, head-socket camera and a
	// cosmetic pistol in the right hand.
	DefaultPawnClass = ADualContourFPCharacter::StaticClass();
}
