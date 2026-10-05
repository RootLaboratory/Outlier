// Fill out your copyright notice in the Description page of Project Settings.


#include "Interface/HackableInterface.h"
#include "GameFramework/Actor.h"

FVector IHackableInterface::GetHackTargetLocation() const
{
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
