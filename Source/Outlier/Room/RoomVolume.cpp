// Fill out your copyright notice in the Description page of Project Settings.


#include "Room/RoomVolume.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSubsystem.h"
#include "Interface/RoomTagInterface.h"
#include "Room/RoomTagComponent.h"
#include "Components/BoxComponent.h"
#include "Components/WorldPartitionStreamingSourceComponent.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"
#include "WorldPartition/WorldPartitionSubsystem.h"

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

	CombatStreamingSource = CreateDefaultSubobject<UWorldPartitionStreamingSourceComponent>(
		TEXT("CombatStreamingSource"));
	CombatStreamingSource->DisableStreamingSource();
}

void ARoomVolume::BeginPlay()
{
	Super::BeginPlay();

	if (CombatStreamingSource && TriggerBox)
	{
		FStreamingSourceShape RoomShape;
		RoomShape.bUseGridLoadingRange = false;
		// 소환 반경이 아니라 전투 중인 Room의 WP 유지 범위다. 플레이어가 다른 층이나
		// Room 밖으로 이동해도 RoomVolume과 소환 오브젝트가 언로드되지 않게 Box 전체를 감싼다.
		RoomShape.Radius = FMath::Max(TriggerBox->GetScaledBoxExtent().Size(), 1.0);
		CombatStreamingSource->Shapes.Reset();
		CombatStreamingSource->Shapes.Add(RoomShape);
		CombatStreamingSource->DisableStreamingSource();
	}

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
	SetCombatStreamingSourceEnabled(false);

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

void ARoomVolume::SetCombatStreamingSourceEnabled(bool bEnabled)
{
	// 클라이언트 셀 로딩은 각 로컬 플레이어의 Streaming Source가 담당한다.
	if (!HasAuthority() || !CombatStreamingSource)
	{
		return;
	}

	if (bEnabled)
	{
		CombatStreamingSource->EnableStreamingSource();
	}
	else
	{
		CombatStreamingSource->DisableStreamingSource();
	}

	if (UWorldPartitionSubsystem* WorldPartitionSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UWorldPartitionSubsystem>()
		: nullptr)
	{
		WorldPartitionSubsystem->OnUpdateStreamingState();
	}
}

bool ARoomVolume::IsCombatStreamingSourceEnabled() const
{
	return CombatStreamingSource
		&& CombatStreamingSource->IsStreamingSourceEnabled();
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
