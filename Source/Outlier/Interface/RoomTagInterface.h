// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "RoomTagInterface.generated.h"

class URoomTagComponent;

/**
 * 
 */
UINTERFACE(MinimalAPI)
class URoomTagInterface : public UInterface
{
	GENERATED_BODY()
};

class OUTLIER_API IRoomTagInterface
{
	GENERATED_BODY()

public:
	virtual FGameplayTag GetCurrentRoomTag() const = 0;
	virtual FGameplayTag GetDefaultRoomTag() const = 0;
	virtual URoomTagComponent* GetRoomTagComp() const = 0;
};
