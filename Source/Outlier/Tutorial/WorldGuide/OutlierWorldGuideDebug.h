#pragma once

#include "CoreMinimal.h"

class UWorld;

namespace OutlierWorldGuideDebug
{
	bool IsEnabled();
	void Trace(const UWorld* World, const FString& Message);
}
