// Fill out your copyright notice in the Description page of Project Settings.


#include "Room/RoomVolume.h"
#include "Room/RoomCombatSubsystem.h"
#include "Network/OutlierArenaSubsystem.h"
#include "Interface/RoomTagInterface.h"
#include "Room/RoomTagComponent.h"
#include "Components/BoxComponent.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace
{
	URoomTagComponent* FindRoomTagComponent(AActor* Actor)
	{
		const IRoomTagInterface* RoomTagOwner = Cast<IRoomTagInterface>(Actor);
		return RoomTagOwner ? RoomTagOwner->GetRoomTagComp() : nullptr;
	}
}

ARoomVolume::ARoomVolume()
{
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

	if (HasAuthority() && RoomTag.IsValid())
	{
		if (URoomCombatSubsystem* CombatSubsystem =
			GetWorld()->GetSubsystem<URoomCombatSubsystem>())
		{
			CombatSubsystem->RegisterRoom(this, RoomTag);
		}
	}
	RefreshOverlappingRoomAssignments();
}

void ARoomVolume::RefreshOverlappingRoomAssignments()
{
	if (!HasAuthority() || !RoomTag.IsValid() || !TriggerBox)
	{
		return;
	}

	TriggerBox->UpdateOverlaps();
	TArray<AActor*> OverlappingActors;
	TriggerBox->GetOverlappingActors(OverlappingActors);
	for (AActor* Actor : OverlappingActors)
	{
		URoomTagComponent* RoomTagComp = FindRoomTagComponent(Actor);
		if (!RoomTagComp || RoomTagComp->HasActiveRoom(this))
		{
			continue;
		}

		// UpdateOverlaps만 호출하면 이미 캐시된 overlap의 BeginOverlap은 재발행되지 않는다.
		RoomTagComp->EnterRoom(this);
		UE_LOG(LogTemp, Log, TEXT("[RoomVolume] Restored overlap assignment Room=%s Actor=%s"),
			*RoomTag.ToString(), *GetNameSafe(Actor));
		OnRoomActorOverlapChanged.Broadcast(Actor, true);
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

bool ARoomVolume::ContainsWorldLocation(const FVector& Location) const
{
	if (!TriggerBox)
	{
		return false;
	}
	// 회전/스케일된 RoomVolume에서도 월드 좌표를 박스의 로컬 공간으로 옮겨 판정한다.
	const FVector Local = TriggerBox->GetComponentTransform().InverseTransformPosition(Location);
	const FVector Extent = TriggerBox->GetUnscaledBoxExtent();
	return FMath::Abs(Local.X) <= Extent.X
		&& FMath::Abs(Local.Y) <= Extent.Y
		&& FMath::Abs(Local.Z) <= Extent.Z;
}

#if WITH_EDITOR
EDataValidationResult ARoomVolume::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (!UOutlierArenaSubsystem::ValidateGameplayActorPlacement(this, Context))
	{
		Result = EDataValidationResult::Invalid;
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

	URoomTagComponent* RoomTagComp = FindRoomTagComponent(OtherActor);
	if (!RoomTagComp)
	{
		return;
	}

	RoomTagComp->EnterRoom(this);
	// 태그를 먼저 갱신해야 구독자가 이 이벤트에서 실제 Room 입장을 판정할 수 있다.
	OnRoomActorOverlapChanged.Broadcast(OtherActor, true);
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

	URoomTagComponent* RoomTagComp = FindRoomTagComponent(OtherActor);
	if (!RoomTagComp)
	{
		return;
	}

	// 위치 태그만 갱신한다. Room 이탈은 전투 완료나 Gameplay 서브레벨 언로드가 아니다.
	RoomTagComp->LeaveRoom(this);
	// 퇴장도 통지해 한 명이 나간 직후 다른 한 명이 들어온 상황을 다시 판정한다.
	OnRoomActorOverlapChanged.Broadcast(OtherActor, false);
}
