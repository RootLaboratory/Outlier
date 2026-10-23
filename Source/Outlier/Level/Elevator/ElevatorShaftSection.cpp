#include "Level/Elevator/ElevatorShaftSection.h"

#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Interaction/InteractableDoor.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

AElevatorShaftSection::AElevatorShaftSection()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetReplicateMovement(false);
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	SceneRoot->SetMobility(EComponentMobility::Movable);
}

float AElevatorShaftSection::GetSectionHeight() const
{
	if (!HeightReferenceMesh)
	{
		// 미지정 높이를 임의의 수치로 대체하지 않는다. Controller의 시작 검증에서 거부한다.
		return 0.0f;
	}
	// 라이트 반경, 문틀, 다른 장식의 돌출은 층 간격에 영향을 주지 않는다.
	return HeightReferenceMesh->GetBoundingBox().GetSize().Z * HeightReferenceScaleZ;
}

void AElevatorShaftSection::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshChildActors();
}

void AElevatorShaftSection::BeginPlay()
{
	Super::BeginPlay();
	RefreshChildActors();
}

void AElevatorShaftSection::RefreshChildActors()
{
	TInlineComponentArray<USceneComponent*> Components;
	GetComponents(Components, false);
	for (USceneComponent* Component : Components)
	{
		Component->SetMobility(EComponentMobility::Movable);
	}

	SectionChildren.Reset();
	EntranceDoor.Reset();
	bHasMultipleDoors = false;
	TArray<AActor*> ChildActors;
	GetAllChildActors(ChildActors, true);
	for (AActor* Child : ChildActors)
	{
		SectionChildren.Add(Child);
		if (AInteractableDoor* Door = Cast<AInteractableDoor>(Child))
		{
			bHasMultipleDoors |= EntranceDoor.IsValid();
			EntranceDoor = Door;
		}
	}
}

AInteractableDoor* AElevatorShaftSection::GetEntranceDoor() const
{
	return bHasMultipleDoors ? nullptr : EntranceDoor.Get();
}

void AElevatorShaftSection::SetSectionVisible(bool bVisible)
{
	if (IsHidden() == !bVisible) return;
	SetActorHiddenInGame(!bVisible);
	// Actor 숨김은 Child Actor의 독립 렌더 컴포넌트에 전파되지 않으므로 초기화 캐시를 함께 갱신한다.
	for (const TWeakObjectPtr<AActor>& Child : SectionChildren)
	{
		if (AActor* Actor = Child.Get())
		{
			Actor->SetActorHiddenInGame(!bVisible);
		}
	}
}

#if WITH_EDITOR
EDataValidationResult AElevatorShaftSection::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	const float Height = GetSectionHeight();
	if (!HeightReferenceMesh)
	{
		Context.AddError(NSLOCTEXT("ElevatorShaft", "MissingHeightReferenceMesh", "Assign HeightReferenceMesh in the section Blueprint."));
		Result = EDataValidationResult::Invalid;
	}
	else if (!FMath::IsFinite(Height) || Height <= 0.0f)
	{
		Context.AddError(NSLOCTEXT("ElevatorShaft", "InvalidSectionHeight", "Reference mesh Z height multiplied by HeightReferenceScaleZ must be finite and greater than zero."));
		Result = EDataValidationResult::Invalid;
	}
	if (bHasMultipleDoors)
	{
		Context.AddError(NSLOCTEXT("ElevatorShaft", "MultipleSectionDoors", "An elevator section must contain at most one entrance door."));
		Result = EDataValidationResult::Invalid;
	}
	return Result == EDataValidationResult::NotValidated ? EDataValidationResult::Valid : Result;
}
#endif
