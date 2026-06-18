// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/Interface.h"
#include "GameplayTagProviderInterface.generated.h"

UINTERFACE(MinimalAPI)
class UGameplayTagProviderInterface : public UInterface
{
	GENERATED_BODY()
};

class OUTLIER_API IGameplayTagProviderInterface
{
	GENERATED_BODY()

public:
	virtual FGameplayTagContainer GetOwnedGameplayTagsForQuery() const = 0;
};
