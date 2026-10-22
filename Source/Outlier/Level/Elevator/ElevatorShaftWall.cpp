#include "Level/Elevator/ElevatorShaftWall.h"

#include "Components/LightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"

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

	// 자식 액터까지 순회하면 실제 문짝과 안전 영역이 시각용 설정으로 덮인다.
	TInlineComponentArray<USceneComponent*> Components;
	GetComponents(Components, false);
	for (USceneComponent* Component : Components)
	{
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
