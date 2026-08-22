#pragma once

#include "CoreMinimal.h"
#include "OutlierPlayerState.h"
#include "OutlierMatchRequest.generated.h"

class AController;

USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierMatchRequest
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<AController> Controller;

	UPROPERTY()
	EOutlierPlayerRole DesiredRole = EOutlierPlayerRole::None;

	UPROPERTY()
	int32 PendingLobbySlotIndex = INDEX_NONE;

	UPROPERTY()
	double RequestTime = 0.0;
};

USTRUCT(BlueprintType)
struct OUTLIER_API FOutlierPendingRolePickMatch
{
	GENERATED_BODY()

	UPROPERTY()
	int32 PendingMatchId = INDEX_NONE;

	UPROPERTY()
	TObjectPtr<AController> FirstController;

	UPROPERTY()
	TObjectPtr<AController> SecondController;

	UPROPERTY()
	TObjectPtr<AController> ShooterController;

	UPROPERTY()
	TObjectPtr<AController> PartnerController;

	UPROPERTY()
	double CreatedTime = 0.0;

	bool Contains(AController* Controller) const
	{
		return FirstController == Controller || SecondController == Controller;
	}

	bool IsRoleTaken(EOutlierPlayerRole Role) const
	{
		return (Role == EOutlierPlayerRole::Shooter && ShooterController) ||
			(Role == EOutlierPlayerRole::Partner && PartnerController);
	}

	bool IsReady() const
	{
		return ShooterController && PartnerController;
	}
};
