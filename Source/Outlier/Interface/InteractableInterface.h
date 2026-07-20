// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "InteractableInterface.generated.h"

class UInteractableComponent;
class AFirstPersonCharacter;

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
	virtual UInteractableComponent* GetInteractableComponent() const = 0;
	virtual void Interact(AFirstPersonCharacter* Interactor) = 0;

	virtual bool RequiresHoldInteract() const { return false; }
	virtual void BeginHoldInteract(AFirstPersonCharacter* Interactor) {}
	virtual void EndHoldInteract(AFirstPersonCharacter* Interactor, bool bCanceled) {}
};
