// Fill out your copyright notice in the Description page of Project Settings.


#include "Interface/HackableInterface.h"
#include "Drone/Partner/HackableComponent.h"
#include "GameFramework/Actor.h"

FVector IHackableInterface::GetHackTargetLocation() const
{
	FVector TargetLocation;
	const UHackableComponent* Hackable = GetHackableComponent();
	if (Hackable && Hackable->TryGetHackTargetLocation(TargetLocation))
	{
		return TargetLocation;
	}

	// 목표점 미지정 또는 참조 무효 시 기존 Actor 원점 계산으로 돌아간다.
	const AActor* Actor = Cast<AActor>(_getUObject());
	return Actor ? Actor->GetActorLocation() : FVector::ZeroVector;
}

FVector IHackableInterface::ResolveHackTargetLocation(const AActor* Actor)
{
	if (const IHackableInterface* Hackable = Cast<IHackableInterface>(Actor))
	{
		return Hackable->GetHackTargetLocation();
	}
	return Actor ? Actor->GetActorLocation() : FVector::ZeroVector;
}
