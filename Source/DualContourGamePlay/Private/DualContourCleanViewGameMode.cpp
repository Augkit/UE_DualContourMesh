#include "DualContourCleanViewGameMode.h"

#include "DualContourCleanViewController.h"

ADualContourCleanViewGameMode::ADualContourCleanViewGameMode()
{
	PlayerControllerClass = ADualContourCleanViewController::StaticClass();
	DefaultPawnClass = nullptr;
}
