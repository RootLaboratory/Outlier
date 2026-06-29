// Fill out your copyright notice in the Description page of Project Settings.


#include "Drone/Partner/HackTestActor.h"
#include "Drone/Partner/HackGameplayTags.h"
#include "Drone/Partner/HackableComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"

// Sets default values
AHackTestActor::AHackTestActor()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	HackComponent = CreateDefaultSubobject<UHackableComponent>(TEXT("HackComponent"));
}

void AHackTestActor::BeginPlay()
{
	Super::BeginPlay();
	
}

UHackableComponent* AHackTestActor::GetHackableComponent() const
{
	return HackComponent;
}

void AHackTestActor::HandleHackEffect(FGameplayTag EffectTag, const FHackResultContext& Context)
{
	//UE_LOG(LogTemp, Error, TEXT("HandleHackEffect Called"));

	if (Context.Result == EHackResult::Success)
	{
		//UE_LOG(LogTemp, Error, TEXT("Success"));
		HackComponent->HackTags.AddTag(OutlierGameplayTags::State::HackedOnce());
	}

	if (Context.Result == EHackResult::Fail)
	{
		//UE_LOG(LogTemp, Error, TEXT("Fail"));
	}

	if (Context.Result == EHackResult::Cancelled)
	{
		//UE_LOG(LogTemp, Error, TEXT("Cancelled"));
	}
}





