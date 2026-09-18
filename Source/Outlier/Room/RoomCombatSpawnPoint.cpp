#include "Room/RoomCombatSpawnPoint.h"

#include "Components/SceneComponent.h"
#include "Room/RoomCombatSubsystem.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

ARoomCombatSpawnPoint::ARoomCombatSpawnPoint()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
}

void ARoomCombatSpawnPoint::BeginPlay()
{
	Super::BeginPlay();

	bRuntimeActive = bInitiallyActive;
	if (!HasAuthority())
	{
		return;
	}

	if (URoomCombatSubsystem* CombatSubsystem =
		GetWorld()->GetSubsystem<URoomCombatSubsystem>())
	{
		CombatSubsystem->RegisterSpawnPoint(
			this,
			RoomTag,
			SpawnPointTags,
			ActivationGroupTag);
	}
}

void ARoomCombatSpawnPoint::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		if (URoomCombatSubsystem* CombatSubsystem = GetWorld()
			? GetWorld()->GetSubsystem<URoomCombatSubsystem>()
			: nullptr)
		{
			CombatSubsystem->UnregisterSpawnPoint(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

void ARoomCombatSpawnPoint::SetRuntimeActive(bool bActive)
{
	if (!HasAuthority())
	{
		return;
	}

	bRuntimeActive = bActive;
}

#if WITH_EDITOR
EDataValidationResult ARoomCombatSpawnPoint::IsDataValid(
	FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (!RoomTag.IsValid())
	{
		Context.AddError(FText::FromString(
			TEXT("A RoomCombatSpawnPoint requires a valid RoomTag.")));
		Result = EDataValidationResult::Invalid;
	}
	if (SpawnWeight <= 0.0f)
	{
		Context.AddError(FText::FromString(
			TEXT("A RoomCombatSpawnPoint requires a SpawnWeight greater than zero.")));
		Result = EDataValidationResult::Invalid;
	}
	if (SpawnRadius <= 0.0f)
	{
		Context.AddError(FText::FromString(
			TEXT("A RoomCombatSpawnPoint requires a SpawnRadius greater than zero.")));
		Result = EDataValidationResult::Invalid;
	}
	if (!bInitiallyActive && !ActivationGroupTag.IsValid())
	{
		Context.AddError(FText::FromString(
			TEXT("An initially inactive RoomCombatSpawnPoint requires an ActivationGroupTag.")));
		Result = EDataValidationResult::Invalid;
	}

	return Result == EDataValidationResult::NotValidated
		? EDataValidationResult::Valid
		: Result;
}
#endif
