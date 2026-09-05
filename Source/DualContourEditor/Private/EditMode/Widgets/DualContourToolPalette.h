#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

class FToolBarBuilder;
class UDualContourEdMode;

/** Shared palette; resolve the mode on every action because preview modes can be recreated. */
namespace DualContourToolPalette
{
	void Build(FToolBarBuilder& ToolbarBuilder, TFunction<TWeakObjectPtr<UDualContourEdMode>()> GetMode);
}
