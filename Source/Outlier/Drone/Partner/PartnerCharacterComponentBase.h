// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PartnerCharacterComponentBase.generated.h"

class APartnerCharacter;
class AShooterCharacter;

UCLASS(Abstract, ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class OUTLIER_API UPartnerCharacterComponentBase : public UActorComponent
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	void RefreshCharacterRefsFromPlayerState();

protected:
	UPROPERTY()
	TObjectPtr<APartnerCharacter> PartnerCharacter;

	UPROPERTY()
	TObjectPtr<AShooterCharacter> ShooterCharacter;
};
