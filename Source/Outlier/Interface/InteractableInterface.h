// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Interface/ScannableInterface.h"
#include "InteractableInterface.generated.h"

class UInteractableComponent;
class AFirstPersonCharacter;


UINTERFACE(MinimalAPI, Blueprintable)
class UInteractableInterface : public UScannableInterface
{
	GENERATED_BODY()
};

class OUTLIER_API IInteractableInterface : public IScannableInterface
{
	GENERATED_BODY()

public:
	virtual int32 GetScanStencilValue() const override
	{
		return static_cast<int32>(EScanType::Interaction);
	}

	virtual UInteractableComponent* GetInteractableComponent() const = 0;
	virtual bool Interact(AFirstPersonCharacter* Interactor) = 0;
	// 기본 상호작용은 즉시 완료한다. 비동기 대상만 실제 commit 이후 성공을 별도로 통지한다.
	virtual bool DefersInteractionCompletion() const { return false; }
};
