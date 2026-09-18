#include "Room/RoomCombatSpawnPoint.h"

#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Enemy/EnemyBase.h"
#include "Engine/World.h"
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

bool ARoomCombatSpawnPoint::FindSpawnTransform(
	TSubclassOf<AEnemyBase> EnemyClass,
	int32 SearchSeed,
	FTransform& OutSpawnTransform) const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (bForceSpawnLocationFailureForTesting)
	{
		return false;
	}
#endif

	const UWorld* World = GetWorld();
	const AEnemyBase* EnemyDefaults = EnemyClass
		? EnemyClass->GetDefaultObject<AEnemyBase>()
		: nullptr;
	const UCapsuleComponent* Capsule = EnemyDefaults
		? EnemyDefaults->GetCapsuleComponent()
		: nullptr;
	if (!World || !Capsule)
	{
		return false;
	}

	constexpr int32 MaxLocationAttempts = 12;
	FRandomStream RandomStream(SearchSeed);
	const FVector Origin = GetActorLocation();
	const FQuat Rotation = GetActorQuat();
	const FCollisionShape CollisionShape = FCollisionShape::MakeCapsule(
		Capsule->GetUnscaledCapsuleRadius(),
		Capsule->GetUnscaledCapsuleHalfHeight());
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RoomCombatSpawn), false);

	for (int32 AttemptIndex = 0; AttemptIndex < MaxLocationAttempts; ++AttemptIndex)
	{
		// 첫 시도는 배치 위치를 그대로 사용하고, 이후 시도만 원 안에 균일하게 퍼뜨린다.
		const float Radius = AttemptIndex == 0
			? 0.0f
			: SpawnRadius * FMath::Sqrt(RandomStream.FRand());
		const float Angle = RandomStream.FRandRange(0.0f, UE_TWO_PI);
		const FVector Candidate = Origin + FVector(
			FMath::Cos(Angle) * Radius,
			FMath::Sin(Angle) * Radius,
			0.0f);
		// BP가 Capsule 응답을 개별 수정하면 Profile 이름은 등록되지 않은 Custom이 된다.
		// Object Channel과 실제 응답 컨테이너를 넘겨 그런 Enemy도 같은 충돌 규칙으로 검사한다.
		if (!World->OverlapBlockingTestByChannel(
			Candidate,
			Rotation,
			Capsule->GetCollisionObjectType(),
			CollisionShape,
			QueryParams,
			FCollisionResponseParams(Capsule->GetCollisionResponseToChannels())))
		{
			OutSpawnTransform = FTransform(Rotation, Candidate, FVector::OneVector);
			return true;
		}
	}

	return false;
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
