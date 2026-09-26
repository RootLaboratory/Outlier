#pragma once

#include "CoreMinimal.h"
#include "Damage/OutlierDamageReceiver.h"
#include "GameFramework/Pawn.h"
#include "LaserKillFloorTestPawn.generated.h"

class USphereComponent;

UCLASS()
class ALaserKillFloorTestPawn : public APawn, public IOutlierDamageReceiver
{
	GENERATED_BODY()

public:
	ALaserKillFloorTestPawn();

	virtual float ReceiveOutlierDamage(const FOutlierDamageRequest& Request) override;

	int32 DamageCount = 0;
	FOutlierDamageRequest LastDamageRequest;

private:
	UPROPERTY()
	TObjectPtr<USphereComponent> CollisionSphere;
};
