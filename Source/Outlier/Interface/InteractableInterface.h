// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "InteractableInterface.generated.h"

/**
 * 
 */
UINTERFACE(MinimalAPI, Blueprintable)
class UInteractableInterface : public UInterface
{
	GENERATED_BODY()
};

class OUTLIER_API IInteractableInterface
{
	GENERATED_BODY()

public:
	virtual void Interact(class AFirstPersonCharacter* Interactor) = 0;
};
