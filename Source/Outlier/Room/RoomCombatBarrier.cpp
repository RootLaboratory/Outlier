#include "Room/RoomCombatBarrier.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Room/RoomCombatSubsystem.h"

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
	Super::BeginPlay();
	ApplyBlockedState();
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
	if (EventRoomTag == RoomTag || AdditionalRoomTags.HasTagExact(EventRoomTag))
	{
		// 이벤트 종류만으로 열고 닫지 않는다. 공유 출입구는 다른 Room이 아직 차단 중일 수 있다.
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
	bool bShouldBlock = Combat && RoomTag.IsValid() && Combat->IsExitBlocked(RoomTag);
	if (Combat && !bShouldBlock)
	{
		for (const FGameplayTag& AdditionalTag : AdditionalRoomTags)
		{
			if (Combat->IsExitBlocked(AdditionalTag))
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
	return InRoomTag.IsValid() && (RoomTag == InRoomTag || AdditionalRoomTags.HasTagExact(InRoomTag));
}

FVector ARoomCombatBarrier::GetJoinFallbackLocation() const
{
	// 인스턴스에서 조정한 로컬 지점을 월드 좌표로 바꾼다. 합류 전에 Room/충돌 검증을 다시 수행한다.
	return GetActorTransform().TransformPosition(JoinFallbackLocalOffset);
}

bool ARoomCombatBarrier::OverlapsJoinCapsule(const FVector& Location, float Radius, float HalfHeight) const
{
	const FVector Local = BlockingBox->GetComponentTransform().InverseTransformPositionNoScale(Location);
	const FVector Extent = BlockingBox->GetScaledBoxExtent();
	return FMath::Abs(Local.X) <= Extent.X + Radius
		&& FMath::Abs(Local.Y) <= Extent.Y + Radius
		&& FMath::Abs(Local.Z) <= Extent.Z + HalfHeight;
}

void ARoomCombatBarrier::SetBlocked(bool bNewBlocked)
{
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
	VisualPlane->SetHiddenInGame(!bBlocked);
	BlockingBox->SetCollisionEnabled(bBlocked
		? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
}
