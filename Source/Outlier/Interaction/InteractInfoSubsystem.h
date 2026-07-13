#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameplayTagContainer.h"
#include "InteractInfoRow.h"
#include "InteractInfoSubsystem.generated.h"

UCLASS()
class OUTLIER_API UInteractInfoSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	bool TryGetInteractInfo(const FGameplayTag& InteractTag, FInteractInfoRow& OutInfo) const;
};
