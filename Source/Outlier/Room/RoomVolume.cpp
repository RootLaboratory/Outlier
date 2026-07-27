// Fill out your copyright notice in the Description page of Project Settings.


#include "Room/RoomVolume.h"
#include "Interface/RoomTagInterface.h"
#include "Room/RoomTagComponent.h"
#include "Components/BoxComponent.h"

// Sets default values
ARoomVolume::ARoomVolume()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;

	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
	TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &ARoomVolume::HandleBeginOverlap);
	TriggerBox->OnComponentEndOverlap.AddDynamic(this, &ARoomVolume::HandleEndOverlap);

	SetRootComponent(TriggerBox);

	TriggerBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TriggerBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	TriggerBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	TriggerBox->SetGenerateOverlapEvents(true);
}

void ARoomVolume::HandleBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	if (!HasAuthority() || !RoomTag.IsValid())
	{
		return;
	}

	const IRoomTagInterface* RoomTagOwner = Cast<IRoomTagInterface>(OtherActor);
	if (!RoomTagOwner)
	{
		return;
	}

	URoomTagComponent* RoomTagComp = RoomTagOwner->GetRoomTagComp();

	if (!RoomTagComp)
	{
		return;
	}

	RoomTagComp->EnterRoom(this);
}

void ARoomVolume::HandleEndOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex)
{
	if (!HasAuthority() || !RoomTag.IsValid())
	{
		return;
	}

	const IRoomTagInterface* RoomTagOwner = Cast<IRoomTagInterface>(OtherActor);
	if (!RoomTagOwner)
	{
		return;
	}

	URoomTagComponent* RoomTagComp = RoomTagOwner->GetRoomTagComp();

	if (!RoomTagComp)
	{
		return;
	}

	RoomTagComp->LeaveRoom(this);
}
