// Fill out your copyright notice in the Description page of Project Settings.


#include "Room/RoomVolume.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSubsystem.h"
#include "Interface/RoomTagInterface.h"
#include "Room/RoomTagComponent.h"
#include "Components/BoxComponent.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

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

void ARoomVolume::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority() && CombatDefinition)
	{
		if (URoomCombatSubsystem* CombatSubsystem =
			GetWorld()->GetSubsystem<URoomCombatSubsystem>())
		{
			CombatSubsystem->RegisterRoom(this, RoomTag, CombatDefinition);
		}
	}
}

void ARoomVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		if (URoomCombatSubsystem* CombatSubsystem = GetWorld()
			? GetWorld()->GetSubsystem<URoomCombatSubsystem>()
			: nullptr)
		{
			CombatSubsystem->UnregisterRoom(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
EDataValidationResult ARoomVolume::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (CombatDefinition && !RoomTag.IsValid())
	{
		Context.AddError(FText::FromString(
			TEXT("A RoomVolume with a CombatDefinition requires a valid RoomTag.")));
		return EDataValidationResult::Invalid;
	}

	return Result == EDataValidationResult::NotValidated
		? EDataValidationResult::Valid
		: Result;
}
#endif

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
