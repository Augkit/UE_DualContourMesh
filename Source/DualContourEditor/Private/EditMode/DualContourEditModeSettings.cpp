#include "EditMode/DualContourEditModeSettings.h"

#if WITH_EDITOR
void UDualContourEditModeSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	SaveConfig();
}
#endif
