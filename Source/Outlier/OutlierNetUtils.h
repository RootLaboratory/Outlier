// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace OutlierNet
{
	FORCEINLINE const TCHAR* GetNetPrefix(const AActor* Actor)
	{
		return (Actor && Actor->HasAuthority()) ? TEXT("[Server]") : TEXT("[Client]");
	}
}
