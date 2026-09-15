#include "DualContourGameGameModeBase.h"
#include "DualContourGamePlayerController.h"

ADualContourGameGameModeBase::ADualContourGameGameModeBase()
{
	PlayerControllerClass = ADualContourGamePlayerController::StaticClass();
}
