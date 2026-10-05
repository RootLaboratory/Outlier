#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"

namespace OutlierFirstPickup
{
inline int32 GetMode()
{
#if UE_BUILD_SHIPPING
	return 0;
#else
	const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("outlier.FPFirstPickupTest"));
	return Variable ? Variable->GetInt() : 0;
#endif
}

// Scopes measure synchronous work only, not asynchronous GPU/asset completion.
class FScope
{
public:
	FScope(const TCHAR* InStage, const UObject* Object)
		: Stage(InStage), bEnabled(GetMode() != 0)
	{
		if (bEnabled)
		{
			Name = GetNameSafe(Object);
			Start = FPlatformTime::Seconds();
			UE_LOG(LogTemp, Log, TEXT("[FPFirstPickup][Begin] Stage=%s Object=%s Wall=%.6f Frame=%llu"),
				Stage, *Name, Start, GFrameCounter);
		}
	}
	~FScope()
	{
		if (bEnabled)
		{
			UE_LOG(LogTemp, Log, TEXT("[FPFirstPickup][End] Stage=%s Object=%s Ms=%.3f Wall=%.6f Frame=%llu"),
				Stage, *Name, (FPlatformTime::Seconds() - Start) * 1000.0, FPlatformTime::Seconds(), GFrameCounter);
		}
	}
private:
	const TCHAR* Stage;
	FString Name;
	double Start = 0.0;
	bool bEnabled;
};
}
