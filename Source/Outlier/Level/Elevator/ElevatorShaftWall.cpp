#include "Level/Elevator/ElevatorShaftWall.h"

#include "Components/LightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Interaction/InteractableDoor.h"
#include "Room/RoomCombatBarrier.h"

AElevatorShaftWall::AElevatorShaftWall()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetReplicateMovement(false);
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	SceneRoot->SetMobility(EComponentMobility::Movable);
}

void AElevatorShaftWall::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyPresentationDefaults();
}

void AElevatorShaftWall::BeginPlay()
{
	Super::BeginPlay();
	ApplyPresentationDefaults();
}

void AElevatorShaftWall::ApplyPresentationDefaults()
{
	if (!bApplyPresentationDefaults)
	{
		return;
	}

	// 메시/라이트가 Child Actor 안에 있어도 시각용 설정을 적용한다.
	TInlineComponentArray<USceneComponent*> Components;
	GetComponents(Components, true);
	for (USceneComponent* Component : Components)
	{
		// 문과 전투 차단막의 하위 액터도 포함해 게임플레이 충돌은 건드리지 않는다.
		bool bGameplayComponent = false;
		for (AActor* ComponentActor = Component->GetOwner(); ComponentActor && ComponentActor != this;
			ComponentActor = ComponentActor->GetParentActor())
		{
			if (ComponentActor->IsA<AInteractableDoor>() || ComponentActor->IsA<ARoomCombatBarrier>())
			{
				bGameplayComponent = true;
				break;
			}
		}
		if (bGameplayComponent) continue;

		Component->SetMobility(EComponentMobility::Movable);
		if (UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Component))
		{
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Mesh->SetGenerateOverlapEvents(false);
			Mesh->SetLightingChannels(true, true, false);
		}
		else if (ULightComponent* Light = Cast<ULightComponent>(Component))
		{
			Light->SetLightingChannels(false, true, false);
			Light->SetCastShadows(false);
		}
	}
}
