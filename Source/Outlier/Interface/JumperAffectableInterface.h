#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "JumperAffectableInterface.generated.h"

USTRUCT()
struct OUTLIER_API FJumperMovementEffect
{
	GENERATED_BODY()

	UPROPERTY()
	FVector Direction = FVector::UpVector;

	UPROPERTY()
	float AscendMultiplier = 1.8f;

	UPROPERTY()
	float DescendMultiplier = 0.8f;

	UPROPERTY()
	float PartnerFlightMultiplier = 1.0f;
};

UINTERFACE()
class OUTLIER_API UJumperAffectableInterface : public UInterface
{
	GENERATED_BODY()
};

class OUTLIER_API IJumperAffectableInterface
{
	GENERATED_BODY()

public:
	virtual void BeginJumperEffect(
		FName EffectId,
		const FJumperMovementEffect& Effect,
		AActor* SourceActor) = 0;

	virtual void EndJumperEffect(FName EffectId, AActor* SourceActor) = 0;
};
