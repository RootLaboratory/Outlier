#include "OutlierStableIdValidator.h"

#include "Drone/Partner/HackableComponent.h"
#include "Enemy/AutoTurret.h"
#include "Enemy/SelfDestructDrone.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Explosion/ExplosiveProp.h"
#include "Interaction/InteractableDoor.h"
#include "Interaction/InteractableSwitchBase.h"
#include "Interaction/InteractionNode.h"
#include "Interaction/PanelMover.h"
#include "Interaction/PassageHatch.h"
#include "OutlierArenaSettings.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomVolume.h"
#include "Save/OutlierCheckpoint.h"
#include "UObject/UnrealType.h"

namespace OutlierEditor
{
TArray<FStableIdIssue> FindStableIdIssues(const TArray<FStableIdEntry>& Entries)
{
	TMap<FName, int32> Counts;
	for (const FStableIdEntry& Entry : Entries)
	{
		if (!Entry.Id.IsNone())
		{
			++Counts.FindOrAdd(Entry.Id);
		}
	}
	TArray<FStableIdIssue> Issues;
	for (const FStableIdEntry& Entry : Entries)
	{
		if (Entry.Id.IsNone() || Counts.FindRef(Entry.Id) > 1)
		{
			Issues.Add({Entry, !Entry.Id.IsNone()});
		}
	}
	Issues.Sort([](const FStableIdIssue& A, const FStableIdIssue& B)
	{
		if (A.Entry.LevelName != B.Entry.LevelName)
		{
			return A.Entry.LevelName < B.Entry.LevelName;
		}
		if (A.Entry.ActorName != B.Entry.ActorName)
		{
			return A.Entry.ActorName < B.Entry.ActorName;
		}
		return A.Entry.Property < B.Entry.Property;
	});
	return Issues;
}

FStableIdReport ScanStableIds(UWorld* World)
{
	FStableIdReport Report;
	if (!World)
	{
		return Report;
	}
	TArray<FStableIdEntry> Entries;
	const URoomCombatDefinition* CombatDefinition =
		GetDefault<UOutlierArenaSettings>()->RoomCombatDefinition.LoadSynchronous();

	for (ULevel* Level : World->GetLevels())
	{
		if (!IsValid(Level))
		{
			continue;
		}
		for (AActor* Actor : Level->Actors)
		{
			if (!IsValid(Actor) || Actor->IsActorBeingDestroyed() || Actor->IsEditorOnly())
			{
				continue;
			}
			++Report.ActorCount;
			auto AddId = [&](FName Id, const FString& Property)
			{
				FStableIdEntry& Entry = Entries.AddDefaulted_GetRef();
				Entry.Actor = Actor;
				Entry.ActorName = FString::Printf(TEXT("%s (%s)"), *Actor->GetActorLabel(), *Actor->GetName());
				Entry.LevelName = Level->GetOutermost()->GetName();
				Entry.Property = Property;
				Entry.Id = Id;
			};
			// Some IDs are private/protected UPROPERTYs. Reflection keeps this editor tool
			// read-only without adding editor accessors to the gameplay API.
			auto AddProperty = [&](const TCHAR* Name)
			{
				const FNameProperty* Property = FindFProperty<FNameProperty>(Actor->GetClass(), Name);
				checkf(Property, TEXT("Stable ID validation property missing: %s.%s"), *Actor->GetClass()->GetName(), Name);
				AddId(Property->GetPropertyValue_InContainer(Actor), Name);
			};

			if (Actor->IsA<AInteractableDoor>()) AddProperty(TEXT("DoorId"));
			if (Actor->IsA<AInteractableSwitchBase>()) AddProperty(TEXT("SwitchId"));
			if (Actor->IsA<AInteractionNode>()) AddProperty(TEXT("PickupId"));
			if (Actor->IsA<APanelMover>()) AddProperty(TEXT("PanelId"));
			if (Actor->IsA<APassageHatch>()) AddProperty(TEXT("HatchId"));
			if (const AOutlierCheckpoint* Checkpoint = Cast<AOutlierCheckpoint>(Actor))
			{
				AddId(Checkpoint->GetCheckpointId(), TEXT("CheckpointId"));
			}
			if (const AAutoTurret* Turret = Cast<AAutoTurret>(Actor))
			{
				AddId(Turret->GetPersistentTurretId(), TEXT("PersistentTurretId"));
			}
			if (const AExplosiveProp* Prop = Cast<AExplosiveProp>(Actor))
			{
				const FBoolProperty* SaveState = FindFProperty<FBoolProperty>(Prop->GetClass(), TEXT("bSaveCheckpointState"));
				if (SaveState && SaveState->GetPropertyValue_InContainer(Prop)
					&& !Cast<ASelfDestructDrone>(Prop->GetOwner())
					&& !Cast<ASelfDestructDrone>(Prop->GetAttachParentActor()))
				{
					AddProperty(TEXT("ExplosivePropId"));
				}
			}
			TInlineComponentArray<UHackableComponent*> Hackables;
			Actor->GetComponents(Hackables);
			for (const UHackableComponent* Hackable : Hackables)
			{
				// None explicitly opts out of checkpoint persistence on this component.
				if (!Hackable->CheckpointProgressId.IsNone())
				{
					AddId(Hackable->CheckpointProgressId, Hackable->GetName() + TEXT(".CheckpointProgressId"));
				}
			}
			if (const ARoomVolume* Room = Cast<ARoomVolume>(Actor))
			{
				// RegisterRoom only registers encounter IDs that have a combat definition.
				if (CombatDefinition && Room->GetRoomTag().IsValid()
					&& CombatDefinition->FindRoomDefinition(Room->GetRoomTag()))
				{
					AddId(Room->GetRoomTag().GetTagName(), TEXT("RoomTag (EncounterId)"));
				}
			}
		}
	}
	Report.IdCount = Entries.Num();
	Report.Issues = FindStableIdIssues(Entries);
	return Report;
}
}
