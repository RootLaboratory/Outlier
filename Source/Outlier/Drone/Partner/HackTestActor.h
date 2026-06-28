// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interface/HackableInterface.h"
#include "HackTestActor.generated.h"

class UHackableComponent;

UCLASS()
class OUTLIER_API AHackTestActor : public AActor , public IHackableInterface
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AHackTestActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:

	virtual UHackableComponent* GetHackableComponent() const override;
	virtual void HandleHackEffect(FGameplayTag EffectTag, const FHackResultContext& Context) override;

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Components")
	TObjectPtr<UHackableComponent> HackComponent;
};
