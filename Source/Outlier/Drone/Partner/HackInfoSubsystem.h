#pragma once

#include "CoreMinimal.h"
#include "Drone/Partner/HackInfoRow.h"
#include "GameplayTagContainer.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HackInfoSubsystem.generated.h"

UCLASS()
class OUTLIER_API UHackInfoSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	bool TryGetHackInfo(const FGameplayTag& HackInfoTag, FHackInfoRow& OutInfo) const;
};
