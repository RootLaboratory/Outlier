#include "Room/RoomCombatBarrier.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Level/Elevator/ElevatorShaftController.h"
#include "Net/UnrealNetwork.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomTagComponent.h"

ARoomCombatBarrier::ARoomCombatBarrier()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	BlockingBox = CreateDefaultSubobject<UBoxComponent>(TEXT("BlockingBox"));
	SetRootComponent(BlockingBox);
	BlockingBox->SetBoxExtent(FVector(20.0f, 150.0f, 150.0f));
	BlockingBox->SetCollisionProfileName(TEXT("BlockAll"));
	BlockingBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BlockingBox->SetGenerateOverlapEvents(false);

	// 표시용 Plane과 이동 차단용 Box를 분리해 머티리얼에 충돌 형태가 좌우되지 않게 한다.
	VisualPlane = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualPlane"));
	VisualPlane->SetupAttachment(BlockingBox);
	VisualPlane->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VisualPlane->SetGenerateOverlapEvents(false);
	VisualPlane->SetHiddenInGame(true);
}

void ARoomCombatBarrier::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARoomCombatBarrier, bBlocked);
}

void ARoomCombatBarrier::BeginPlay()
{
	if (HasAuthority())
	{
		// Section/Wall 아래에 중첩되어도 등록 전에 레벨 Controller 인스턴스의 전투 태그를 사용한다.
		for (AActor* Parent = GetParentActor(); Parent; Parent = Parent->GetParentActor())
		{
			if (const AElevatorShaftController* Controller = Cast<AElevatorShaftController>(Parent))
			{
				RoomTag = Controller->CombatRoomTag;
				break;
			}
		}
	}
	Super::BeginPlay();
	// 서버는 등록/안전 검사를 거친 상태만 적용한다. 복원된 true도 먼저 켜지 않는다.
	// 인스턴스에서 추가한 표시 메시도 시작 상태에 맞춘다. 서버의 복원된 true는 안전 검사까지 보류한다.
	if (!HasAuthority() || !bBlocked) ApplyBlockedState();
	if (HasAuthority())
	{
		if (URoomCombatSubsystem* Combat = GetWorld()->GetSubsystem<URoomCombatSubsystem>())
		{
			// WP에서 늦게 로드되면 시작 이벤트를 놓칠 수 있으므로 등록 직후 현재 상태도 조회한다.
			Combat->RegisterBarrier(this);
			Combat->OnCombatEvent.AddDynamic(this, &ARoomCombatBarrier::HandleCombatEvent);
			RefreshFromRoomCombat();
		}
	}
}

void ARoomCombatBarrier::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		if (URoomCombatSubsystem* Combat = GetWorld()
			? GetWorld()->GetSubsystem<URoomCombatSubsystem>() : nullptr)
		{
			Combat->OnCombatEvent.RemoveDynamic(this, &ARoomCombatBarrier::HandleCombatEvent);
			Combat->UnregisterBarrier(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void ARoomCombatBarrier::HandleCombatEvent(FGameplayTag EventRoomTag, ERoomCombatEvent Event,
	int32 CombatPhaseIndex, int32 GameplayGeneration)
{
	(void)Event;
	(void)CombatPhaseIndex;
	(void)GameplayGeneration;
	if (CanBlockRoom(EventRoomTag))
	{
		// 완료한 방의 이벤트여도 형제 방은 진행 중일 수 있다. 현재 그룹 상태를 다시 조회한다.
		RefreshFromRoomCombat();
	}
}

void ARoomCombatBarrier::RefreshFromRoomCombat()
{
	if (!HasAuthority())
	{
		return;
	}
	const URoomCombatSubsystem* Combat = GetWorld()
		? GetWorld()->GetSubsystem<URoomCombatSubsystem>() : nullptr;
	if (IsGroupInternalPassage())
	{
		SetBlocked(false);
		return;
	}
	bool bShouldBlock = Combat && (Combat->IsGroupExitBlocked(RoomTag)
		|| Combat->IsGroupExitBlocked(ConnectedRoomTag));
	if (Combat && !bShouldBlock)
	{
		for (const FGameplayTag& AdditionalTag : AdditionalRoomTags)
		{
			if (Combat->IsGroupExitBlocked(AdditionalTag))
			{
				bShouldBlock = true;
				break;
			}
		}
	}
	SetBlocked(bShouldBlock);
}

bool ARoomCombatBarrier::ServesRoom(FGameplayTag InRoomTag) const
{
	return InRoomTag.IsValid() && (RoomTag == InRoomTag || ConnectedRoomTag == InRoomTag
		|| AdditionalRoomTags.HasTagExact(InRoomTag));
}

bool ARoomCombatBarrier::IsGroupInternalPassage() const
{
	return RoomTag != ConnectedRoomTag && URoomTagComponent::AreRoomsInSameGroup(RoomTag, ConnectedRoomTag);
}

bool ARoomCombatBarrier::CanBlockRoom(FGameplayTag InRoomTag) const
{
	if (!InRoomTag.IsValid() || IsGroupInternalPassage())
	{
		return false;
	}
	if (ServesRoom(InRoomTag) || URoomTagComponent::AreRoomsInSameGroup(RoomTag, InRoomTag)
		|| URoomTagComponent::AreRoomsInSameGroup(ConnectedRoomTag, InRoomTag))
	{
		return true;
	}
	for (const FGameplayTag& Tag : AdditionalRoomTags)
	{
		if (URoomTagComponent::AreRoomsInSameGroup(Tag, InRoomTag))
		{
			return true;
		}
	}
	return false;
}

FVector ARoomCombatBarrier::GetJoinFallbackLocation() const
{
	// 인스턴스에서 조정한 로컬 지점을 월드 좌표로 바꾼다. 합류 전에 Room/충돌 검증을 다시 수행한다.
	return GetActorTransform().TransformPosition(JoinFallbackLocalOffset);
}

bool ARoomCombatBarrier::OverlapsJoinCapsule(const FVector& Location, float Radius, float HalfHeight) const
{
	if (!BlockingBox) return false;
	// NoCollision Box는 물리 Body가 없을 수 있다. 실제 OBB와 캡슐 중심 선분의
	// 최소 거리를 구해, 회전된 형상도 충돌을 켜지 않고 검사한다.
	const FTransform Transform = BlockingBox->GetComponentTransform();
	const FVector Extent = BlockingBox->GetScaledBoxExtent();
	const float SegmentHeight = FMath::Max(0.0f, HalfHeight - Radius);
	const FVector Start = Transform.InverseTransformPositionNoScale(Location - FVector(0, 0, SegmentHeight));
	const FVector End = Transform.InverseTransformPositionNoScale(Location + FVector(0, 0, SegmentHeight));
	const FVector Delta = End - Start;
	TArray<double, TInlineAllocator<8>> Times{0.0, 1.0};
	for (int32 Axis = 0; Axis < 3; ++Axis)
		if (!FMath::IsNearlyZero(Delta[Axis]))
			for (double Side : {-1.0, 1.0})
			{
				const double Time = (Side * Extent[Axis] - Start[Axis]) / Delta[Axis];
				if (Time > 0 && Time < 1) Times.Add(Time);
			}
	Times.Sort();
	auto DistanceSquared = [&](double Time)
	{
		const FVector Point = Start + Delta * Time;
		return FMath::Square(FMath::Max(0.0, FMath::Abs(Point.X) - Extent.X))
			+ FMath::Square(FMath::Max(0.0, FMath::Abs(Point.Y) - Extent.Y))
			+ FMath::Square(FMath::Max(0.0, FMath::Abs(Point.Z) - Extent.Z));
	};
	double Minimum = TNumericLimits<double>::Max();
	for (int32 Index = 1; Index < Times.Num(); ++Index)
	{
		const double Midpoint = (Times[Index - 1] + Times[Index]) * 0.5;
		double Quadratic = 0, Linear = 0;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const double Position = Start[Axis] + Delta[Axis] * Midpoint;
			if (FMath::Abs(Position) <= Extent[Axis]) continue;
			const double Offset = Start[Axis] - FMath::Sign(Position) * Extent[Axis];
			Quadratic += Delta[Axis] * Delta[Axis];
			Linear += Delta[Axis] * Offset;
		}
		const double BestTime = Quadratic > UE_DOUBLE_SMALL_NUMBER
			? FMath::Clamp(-Linear / Quadratic, Times[Index - 1], Times[Index]) : Times[Index - 1];
		Minimum = FMath::Min(Minimum, DistanceSquared(BestTime));
	}
	return Minimum <= FMath::Square(static_cast<double>(Radius));
}

void ARoomCombatBarrier::GetSafetyRoomTags(TArray<FGameplayTag>& OutTags) const
{
	if (RoomTag.IsValid()) OutTags.AddUnique(RoomTag);
	if (ConnectedRoomTag.IsValid()) OutTags.AddUnique(ConnectedRoomTag);
	for (FGameplayTag Tag : AdditionalRoomTags) OutTags.AddUnique(Tag);
}

void ARoomCombatBarrier::SetBlocked(bool bNewBlocked)
{
	if (HasAuthority() && bNewBlocked)
	{
		URoomCombatSubsystem* Combat = GetWorld()->GetSubsystem<URoomCombatSubsystem>();
		const FGameplayTag SafetyRoom = Combat ? Combat->FindBarrierClosureRoom(this) : FGameplayTag();
		const TWeakObjectPtr<ARoomCombatBarrier> WeakThis(this);
		if (!Combat || !SafetyRoom.IsValid() || !Combat->PrepareBarrierClosure(SafetyRoom,
			[WeakThis, SafetyRoom]
			{
				const ARoomCombatBarrier* Barrier = WeakThis.Get();
				const URoomCombatSubsystem* Current = Barrier && Barrier->GetWorld()
					? Barrier->GetWorld()->GetSubsystem<URoomCombatSubsystem>() : nullptr;
				return Current && Current->IsGroupExitBlocked(SafetyRoom);
			}, [WeakThis](bool bSafe)
			{
				if (bSafe && WeakThis.IsValid()) WeakThis->RefreshFromRoomCombat();
			}))
		{
			// 이미 닫힌 출입구 안으로 복원/재접속한 경우도 안전 분리 동안은 열어 둔다.
			// 진행 중 전투는 유지하고 이 차단막의 충돌/표시만 보류한다.
			SetBlocked(false);
			return;
		}
	}
	if (bBlocked == bNewBlocked)
	{
		return;
	}
	bBlocked = bNewBlocked;
	ApplyBlockedState();
	ForceNetUpdate();
}

void ARoomCombatBarrier::OnRep_Blocked()
{
	ApplyBlockedState();
}

void ARoomCombatBarrier::ApplyBlockedState()
{
	// 서버의 단일 차단 상태가 복제되면 클라이언트에서도 표시와 충돌을 함께 갱신한다.
	// 원본뿐 아니라 인스턴스에서 복제한 Plane도 같은 표시 상태를 사용한다.
	TInlineComponentArray<UStaticMeshComponent*> VisualMeshes;
	GetComponents(VisualMeshes);
	for (UStaticMeshComponent* VisualMesh : VisualMeshes)
	{
		VisualMesh->SetHiddenInGame(!bBlocked);
	}
	BlockingBox->SetCollisionEnabled(bBlocked
		? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	UE_LOG(LogTemp, Display,
		TEXT("[RoomCombatBarrier] World=%s Authority=%d NetMode=%d Room=%s AdditionalRooms=%s Blocked=%d Component=%s Collision=%s ActorCollision=%d PawnResponse=%s Location=%s"),
		*GetNameSafe(GetWorld()), HasAuthority(), static_cast<int32>(GetNetMode()),
		*RoomTag.ToString(), *AdditionalRoomTags.ToStringSimple(), bBlocked, *BlockingBox->GetPathName(),
		*UEnum::GetValueAsString(BlockingBox->GetCollisionEnabled()), GetActorEnableCollision(),
		*UEnum::GetValueAsString(BlockingBox->GetCollisionResponseToChannel(ECC_Pawn)),
		*BlockingBox->GetComponentLocation().ToString());
}
