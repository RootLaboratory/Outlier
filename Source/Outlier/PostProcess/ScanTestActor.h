// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interface/ScannableInterface.h"
#include "ScanTestActor.generated.h"

UCLASS()
class OUTLIER_API AScanTestActor : public AActor, public IScannableInterface
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AScanTestActor();

public:
	virtual int32 GetScanStencilValue() const override;
protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	UPROPERTY(EditAnywhere, Category = "ScanStenecilNum")
	int32 ScanStencilValue = 1;
};
