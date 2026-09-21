#include "Room/TurretReinforcementHatch.h"

#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Enemy/AutoTurret.h"
#include "OutlierArenaSettings.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSubsystem.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

ATurretReinforcementHatch::ATurretReinforcementHatch()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	HatchMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("HatchMesh"));
	HatchMesh->SetupAttachment(SceneRoot);
	HatchMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HatchMesh->SetGenerateOverlapEvents(false);
	HatchMesh->SetIsReplicated(true);
}

void ATurretReinforcementHatch::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
	{
		return;
	}

	if (URoomCombatSubsystem* CombatSubsystem =
		GetWorld()->GetSubsystem<URoomCombatSubsystem>())
	{
		CombatSubsystem->RegisterTurretHatch(
			this,
			RoomTag,
			CombatPhaseIndex,
			WaveIndex,
			LinkedTurret);
	}
}

void ATurretReinforcementHatch::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		if (URoomCombatSubsystem* CombatSubsystem = GetWorld()
			? GetWorld()->GetSubsystem<URoomCombatSubsystem>()
			: nullptr)
		{
			CombatSubsystem->UnregisterTurretHatch(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
EDataValidationResult ATurretReinforcementHatch::IsDataValid(
	FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (IsTemplate())
	{
		return Result == EDataValidationResult::NotValidated
			? EDataValidationResult::Valid
			: Result;
	}

	auto AddValidationError = [&Context, &Result](const FString& Message)
	{
		Context.AddError(FText::FromString(Message));
		Result = EDataValidationResult::Invalid;
	};

	if (!RoomTag.IsValid())
	{
		AddValidationError(TEXT("A TurretReinforcementHatch requires a valid RoomTag."));
	}
	if (CombatPhaseIndex < 0 || WaveIndex < 0)
	{
		AddValidationError(TEXT("A TurretReinforcementHatch requires non-negative phase and Wave indices."));
	}
	if (!IsValid(LinkedTurret))
	{
		AddValidationError(TEXT("A TurretReinforcementHatch requires a linked AutoTurret instance."));
	}

	const UOutlierArenaSettings* Settings = GetDefault<UOutlierArenaSettings>();
	const URoomCombatDefinition* Definition = Settings
		? Settings->RoomCombatDefinition.LoadSynchronous()
		: nullptr;
	const FRoomCombatRoomDefinition* RoomDefinition = Definition && RoomTag.IsValid()
		? Definition->FindRoomDefinition(RoomTag)
		: nullptr;
	const FRoomCombatWaveDefinition* Wave = RoomDefinition
		? RoomDefinition->FindWave(CombatPhaseIndex, WaveIndex)
		: nullptr;
	if (!Definition)
	{
		AddValidationError(TEXT("A TurretReinforcementHatch requires the integrated RoomCombatDefinition setting."));
	}
	else if (!RoomDefinition)
	{
		AddValidationError(FString::Printf(
			TEXT("A TurretReinforcementHatch has no RoomCombatDefinition entry for %s."),
			*RoomTag.ToString()));
	}
	else if (!Wave)
	{
		AddValidationError(TEXT("A TurretReinforcementHatch points to an invalid combat phase or Wave."));
	}
	else
	{
		if (!Wave->IsSpawnFromObjects())
		{
			AddValidationError(TEXT("A TurretReinforcementHatch must target a SpawnFromObjects Wave."));
		}
		else if (!Wave->HasTurretHatches())
		{
			AddValidationError(TEXT("A TurretReinforcementHatch requires a positive ExpectedTurretHatchCount on its Wave."));
		}
	}

	return Result == EDataValidationResult::NotValidated
		? EDataValidationResult::Valid
		: Result;
}
#endif
