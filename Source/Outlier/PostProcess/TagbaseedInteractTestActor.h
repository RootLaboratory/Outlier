// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interface/InteractableInterface.h"
#include "TagbaseedInteractTestActor.generated.h"

class AFirstPersonCharacter;
class UInteractableComponent;

UCLASS()
class OUTLIER_API ATagbaseedInteractTestActor : public AActor, public IInteractableInterface
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ATagbaseedInteractTestActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	virtual UInteractableComponent* GetInteractableComponent() const override; //Tag 사용으로 Component로 확장. 
	virtual bool Interact(class AFirstPersonCharacter* Interactor) override;

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Component")
	TObjectPtr<UInteractableComponent> InteractableComponent;
};
