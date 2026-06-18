// Fill out your copyright notice in the Description page of Project Settings.


#include "PostProcess/ScanTestActor.h"

// Sets default values
AScanTestActor::AScanTestActor()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;

}

int32 AScanTestActor::GetScanStencilValue() const
{
	UE_LOG(LogTemp, Error, TEXT("GetScanStencilValue Valid, %d"), ScanStencilValue);
	return ScanStencilValue;
}

// Called when the game starts or when spawned
void AScanTestActor::BeginPlay()
{
	Super::BeginPlay();
}


