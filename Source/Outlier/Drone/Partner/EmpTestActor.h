// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interface/EMPableInterface.h"
#include "EmpTestActor.generated.h"

class UEMPableComponent;
UCLASS()
class OUTLIER_API AEmpTestActor : public AActor, public IEMPableInterface
{
	GENERATED_BODY()

public:
	AEmpTestActor();

protected:
	virtual void BeginPlay() override;

public:
	virtual UEMPableComponent* GetEMPableComponent() const override;
	virtual void HandleEmp(FGameplayTag EffectTag) override;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Components")
	TObjectPtr<UEMPableComponent> EMPComponent;
};
