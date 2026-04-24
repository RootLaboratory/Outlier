// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VisualEffectProvider.h"
#include "DummyWall.generated.h"


UCLASS(Blueprintable)
class VISUALEVENT_API ADummyWall : public AActor , public IVisualEffectProvider
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ADummyWall();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

public:
	virtual FVisualEventSet GetVisualEventSet() const override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual Effect")
	FVisualEventSet VisualEventSet;

};
