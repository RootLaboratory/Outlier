#include "Network/ArenaTransitionPlacement.h"

#include "Components/CapsuleComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Interaction/InteractableDoor.h"
#include "Interaction/InteractableSwitch.h"
#include "Interaction/PanelMover.h"
#include "Interaction/PanelMoverSwitch.h"
#include "Level/Elevator/ElevatorShaftController.h"
#include "Level/Elevator/ElevatorShaftSection.h"
#include "OutlierArenaSettings.h"
#include "Room/RoomCombatBarrier.h"

namespace
{
	using FTarget = FArenaTransitionPlacementTarget;
	using FPart = FArenaTransitionPlacementPart;

	bool ReadActor(const AActor* Actor, const FString& Path, TArray<FPart>& Parts, FString& Pending,
		TArray<TWeakObjectPtr<AActor>>* Instances, const AInteractableDoor* CanonicalDoor = nullptr)
	{
		if (!IsValid(Actor) || !Actor->HasActorBegunPlay() || !Actor->GetRootComponent()
			|| !Actor->GetRootComponent()->IsRegistered())
		{
			Pending = Path;
			return false;
		}
		if (Instances)
		{
			Instances->AddUnique(const_cast<AActor*>(Actor));
		}
		Parts.Add({ Path, Actor->GetActorTransform(), 0 });
		TInlineComponentArray<UPrimitiveComponent*> Primitives;
		Actor->GetComponents(Primitives);
		for (const UPrimitiveComponent* Primitive : Primitives)
		{
			const ECollisionResponse Response = Primitive->GetCollisionResponseToChannel(ECC_Pawn);
			if (Response == ECR_Ignore)
			{
				continue;
			}
			const FString PartPath = Path / Primitive->GetName();
			if (!Primitive->IsRegistered())
			{
				Pending = PartPath;
				return false;
			}
			const bool bCanCollide = Actor->GetActorEnableCollision() && Primitive->IsQueryCollisionEnabled();
			Parts.Add({ PartPath, Primitive->GetComponentTransform(),
				static_cast<uint8>(bCanCollide ? Response : ECR_Ignore) });
		}

		TInlineComponentArray<UChildActorComponent*> Children;
		Actor->GetComponents(Children);
		for (const UChildActorComponent* Child : Children)
		{
			const UClass* ChildClass = Child->GetChildActorClass();
			if (!ChildClass || ChildClass->IsChildOf<AElevatorShaftSection>()
				|| (CanonicalDoor && (ChildClass->IsChildOf<AInteractableDoor>()
					|| ChildClass->IsChildOf<ARoomCombatBarrier>())))
			{
				// 문은 서버가 복제한 참조로 검사한다. 섹션의 전투 배리어는 방 전투가 따로 제어한다.
				continue;
			}
			if (!ReadActor(Child->GetChildActor(), Path / Child->GetName(), Parts, Pending, Instances))
			{
				return false;
			}
		}

		if (const AInteractableSwitchBase* Switch = Cast<AInteractableSwitchBase>(Actor))
		{
			Parts.Add({ Path / TEXT("SwitchState"), FTransform::Identity,
				static_cast<uint8>((Switch->bIsActivated ? 1 : 0) | (Switch->IsInteractionBlocked() ? 2 : 0)) });
		}
		return true;
	}

	bool ReadBinding(const AActor* Actor, const FString& Path, TArray<FPart>& Parts, FString& Pending,
		TArray<TWeakObjectPtr<AActor>>* Instances)
	{
		if (!ReadActor(Actor, Path, Parts, Pending, Instances))
		{
			return false;
		}
		if (const AElevatorShaftController* Elevator = Cast<AElevatorShaftController>(Actor))
		{
			if (!Elevator->IsInitialArenaPlacementReady())
			{
				Pending = Path / TEXT("InitialElevatorPlacement");
				return false;
			}
			return ReadActor(Elevator->EntranceSection, Path / TEXT("Entrance"), Parts, Pending, Instances, Elevator->EntranceDoor)
				&& ReadActor(Elevator->EntranceDoor, Path / TEXT("EntranceDoor"), Parts, Pending, Instances)
				&& ReadActor(Elevator->ArrivalSection, Path / TEXT("Arrival"), Parts, Pending, Instances, Elevator->ArrivalDoor)
				&& ReadActor(Elevator->ArrivalDoor, Path / TEXT("ArrivalDoor"), Parts, Pending, Instances);
		}
		if (const AInteractableSwitch* Switch = Cast<AInteractableSwitch>(Actor))
		{
			const AInteractableDoor* Door = nullptr;
			FString BindingError;
			bool bPendingConstruction = false;
			switch (Switch->DoorSource)
			{
			case EInteractableSwitchDoorSource::DirectDoor:
				Door = Switch->TargetDoor;
				break;
			case EInteractableSwitchDoorSource::ElevatorEntrance:
				if (IsValid(Switch->TargetElevator))
				{
					// 클라이언트에서 서버 문은 ChildActorComponent에 연결되지 않고 복제될 수 있다.
					Door = Switch->HasAuthority()
						? Switch->TargetElevator->FindSwitchDoor(false, BindingError, bPendingConstruction)
						: Switch->TargetElevator->EntranceDoor.Get();
				}
				break;
			case EInteractableSwitchDoorSource::ElevatorArrival:
				if (IsValid(Switch->TargetElevator))
				{
					Door = Switch->HasAuthority()
						? Switch->TargetElevator->FindSwitchDoor(true, BindingError, bPendingConstruction)
						: Switch->TargetElevator->ArrivalDoor.Get();
				}
				break;
			case EInteractableSwitchDoorSource::ElevatorSection:
				if (IsValid(Switch->TargetSection))
				{
					Door = Switch->TargetSection->FindCurrentDoor(BindingError, bPendingConstruction);
				}
				break;
			}
			return ReadActor(Door, Path / TEXT("SwitchTarget"), Parts, Pending, Instances);
		}
		if (const APanelMoverSwitch* Switch = Cast<APanelMoverSwitch>(Actor))
		{
			return ReadActor(Switch->TargetPanel, Path / TEXT("SwitchTarget"), Parts, Pending, Instances);
		}
		return true;
	}
}

const AActor* FArenaTransitionPlacementTarget::ResolveActor(const UWorld& World, FString& OutPendingTarget) const
{
	ULevel* CurrentLevel = nullptr;
	for (const ULevelStreaming* Streaming : World.GetStreamingLevels())
	{
		ULevel* Level = IsValid(Streaming) ? Streaming->GetLoadedLevel() : nullptr;
		if (!IsValid(Level) || !Level->bIsVisible)
		{
			continue;
		}
		// PIE와 Listen 인스턴스에서도 원본 패키지가 같은 현재 레벨만 대상으로 삼는다.
		const FString SourcePackage = Streaming->PackageNameToLoad.IsNone()
			? Streaming->GetWorldAssetPackageName() : Streaming->PackageNameToLoad.ToString();
		if (FName(UWorld::RemovePIEPrefix(SourcePackage)) != LevelPackage)
		{
			continue;
		}
		if (CurrentLevel && CurrentLevel != Level)
		{
			OutPendingTarget = FString::Printf(TEXT("%s/DuplicateLevel"), *LevelPackage.ToString());
			return nullptr;
		}
		CurrentLevel = Level;
	}

	if (!CurrentLevel)
	{
		OutPendingTarget = FString::Printf(TEXT("%s/LevelNotVisible"), *LevelPackage.ToString());
		return nullptr;
	}

	// 같은 인스턴스에서 배치가 유지되는 동안에는 준비를 재검사할 때 액터 배열을 다시 돌지 않는다.
	const AActor* Actor = CachedActor.Get();
	if (CachedLevel.Get() == CurrentLevel && CachedLevelActorCount == CurrentLevel->Actors.Num()
		&& IsValid(Actor) && Actor->GetLevel() == CurrentLevel && Actor->ActorHasTag(ActorTag))
	{
		return Actor;
	}

	CachedActor.Reset();
	AActor* Match = nullptr;
	for (AActor* Candidate : CurrentLevel->Actors)
	{
		if (!IsValid(Candidate) || !Candidate->ActorHasTag(ActorTag))
		{
			continue;
		}
		if (Match)
		{
			OutPendingTarget = FString::Printf(TEXT("%s/DuplicateActorTag"), *ActorTag.ToString());
			return nullptr;
		}
		Match = Candidate;
	}
	if (!Match)
	{
		OutPendingTarget = FString::Printf(TEXT("%s/MissingActorTag"), *ActorTag.ToString());
		return nullptr;
	}

	CachedActor = Match;
	CachedLevel = CurrentLevel;
	CachedLevelActorCount = CurrentLevel->Actors.Num();
	return Match;
}

bool OutlierArenaTransitionPlacement::IsSpawnCapsuleReady(const APawn& Pawn, const FVector& Position)
{
	const ACharacter* Character = Cast<ACharacter>(&Pawn);
	const UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	const UWorld* World = Pawn.GetWorld();
	if (!Capsule || !Capsule->IsRegistered() || !World)
	{
		return false;
	}

	const FCollisionQueryParams Query(SCENE_QUERY_STAT(ArenaTransitionSpawn), false, &Pawn);
	const FCollisionResponseParams Response(Capsule->GetCollisionResponseToChannels());
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(
		Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight());
	const ECollisionChannel Channel = Capsule->GetCollisionObjectType();
	if (World->OverlapBlockingTestByChannel(Position, Capsule->GetComponentQuat(), Channel, Shape, Query, Response))
	{
		return false;
	}

	FHitResult Floor;
	const FVector Below = Position - FVector(0, 0, Capsule->GetScaledCapsuleHalfHeight() + 150.0);
	return World->LineTraceSingleByChannel(Floor, Position, Below, Channel, Query, Response)
		&& Floor.ImpactNormal.Z >= 0.5;
}

bool OutlierArenaTransitionPlacement::ResolveTargets(FName PresetId, FName CheckpointId,
	TArray<FArenaTransitionPlacementTarget>& OutTargets)
{
	OutTargets.Reset();
	if (PresetId.IsNone() && CheckpointId.IsNone())
	{
		// 개발용 현재 위치 리로드에는 별도로 지정된 필수 대상이 없다.
		return true;
	}
	const FArenaTransitionPlacementBinding* Match = nullptr;
	for (const FArenaTransitionPlacementBinding& Binding : GetDefault<UOutlierArenaSettings>()->TransitionPlacementBindings)
	{
		const bool bMatches = CheckpointId.IsNone()
			? Binding.PresetIds.Contains(PresetId) : Binding.CheckpointIds.Contains(CheckpointId);
		if (bMatches)
		{
			if (Match)
			{
				return false;
			}
			Match = &Binding;
		}
	}
	if (!Match)
	{
		return false;
	}
	TSet<FName> Tags;
	for (const FTarget& Target : Match->Targets)
	{
		if (Target.LevelPackage.IsNone() || Target.ActorTag.IsNone() || Tags.Contains(Target.ActorTag))
		{
			return false;
		}
		Tags.Add(Target.ActorTag);
	}
	OutTargets = Match->Targets;
	return true;
}

bool OutlierArenaTransitionPlacement::ReadTargets(const UWorld& World, const TArray<FArenaTransitionPlacementTarget>& Targets,
	TArray<FArenaTransitionPlacementPart>& OutParts, FString& OutPendingTarget, TArray<TWeakObjectPtr<AActor>>* OutInstances)
{
	OutParts.Reset();
	OutPendingTarget.Reset();
	if (OutInstances)
	{
		OutInstances->Reset();
	}
	for (const FTarget& Target : Targets)
	{
		const AActor* Actor = Target.ResolveActor(World, OutPendingTarget);
		if (!Actor || !ReadBinding(Actor, Target.ActorTag.ToString(), OutParts, OutPendingTarget, OutInstances))
		{
			return false;
		}
	}
	OutParts.Sort([](const FPart& A, const FPart& B)
	{
		return A.Path < B.Path;
	});
	return true;
}

bool OutlierArenaTransitionPlacement::Matches(const TArray<FArenaTransitionPlacementPart>& Expected,
	const TArray<FArenaTransitionPlacementPart>& Actual, FString& OutPendingTarget)
{
	if (Expected.Num() != Actual.Num())
	{
		OutPendingTarget = FString::Printf(TEXT("ComponentCount:%d/%d"), Actual.Num(), Expected.Num());
		return false;
	}
	for (int32 Index = 0; Index < Expected.Num(); ++Index)
	{
		const FPart& Server = Expected[Index];
		const FPart& Client = Actual[Index];
		if (Server.Path != Client.Path || Server.PawnCollision != Client.PawnCollision
			|| !Server.Transform.GetLocation().Equals(Client.Transform.GetLocation(), 1.0)
			|| !Server.Transform.GetRotation().Equals(Client.Transform.GetRotation(), 0.001)
			|| !Server.Transform.GetScale3D().Equals(Client.Transform.GetScale3D(), 0.001))
		{
			OutPendingTarget = Server.Path;
			return false;
		}
	}
	return true;
}
