#include "Room/RoomCombatSpawnPoint.h"

#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/TextRenderComponent.h"
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

	// 소환 중심은 Root에 유지한다. 메시와 충돌 위치는 따로 조절하며, 막힌 영역은 소환 탐색에서 제외된다.
	SpawnPointMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SpawnPointMesh"));
	SpawnPointMesh->SetupAttachment(SceneRoot);
	SpawnPointMesh->SetCollisionProfileName(TEXT("BlockAll"));
	SpawnPointMesh->SetGenerateOverlapEvents(false);

#if WITH_EDITORONLY_DATA
	SpawnRadiusPreview = CreateEditorOnlyDefaultSubobject<USphereComponent>(TEXT("SpawnRadiusPreview"));
	if (SpawnRadiusPreview)
	{
		SpawnRadiusPreview->SetupAttachment(SceneRoot);
		// 실제 탐색은 월드 XY 원 안에서 수행한다. Actor 회전/스케일과 무관하게 같은 반경을 표시한다.
		SpawnRadiusPreview->SetAbsolute(false, true, true);
		SpawnRadiusPreview->SetRelativeScale3D(FVector(1.0, 1.0, 0.01));
		SpawnRadiusPreview->InitSphereRadius(SpawnRadius);
		SpawnRadiusPreview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SpawnRadiusPreview->SetGenerateOverlapEvents(false);
		SpawnRadiusPreview->SetCanEverAffectNavigation(false);
		SpawnRadiusPreview->SetHiddenInGame(true);
		SpawnRadiusPreview->ShapeColor = FColor::Cyan;
	}
	SpawnInfoPreview = CreateEditorOnlyDefaultSubobject<UTextRenderComponent>(TEXT("SpawnInfoPreview"));
	if (SpawnInfoPreview)
	{
		SpawnInfoPreview->SetupAttachment(SceneRoot);
		SpawnInfoPreview->SetAbsolute(false, true, true);
		SpawnInfoPreview->SetRelativeLocation(FVector(0.0, 0.0, 100.0));
		SpawnInfoPreview->SetHorizontalAlignment(EHTA_Center);
		SpawnInfoPreview->SetWorldSize(24.0f);
		SpawnInfoPreview->SetTextRenderColor(FColor::Cyan);
		SpawnInfoPreview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SpawnInfoPreview->SetGenerateOverlapEvents(false);
		SpawnInfoPreview->SetCanEverAffectNavigation(false);
		SpawnInfoPreview->SetHiddenInGame(true);
	}
#endif
}

void ARoomCombatSpawnPoint::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
#if WITH_EDITORONLY_DATA
	if (SpawnRadiusPreview)
	{
		SpawnRadiusPreview->SetSphereRadius(FMath::Max(SpawnRadius, 1.0f), false);
		SpawnRadiusPreview->SetRelativeLocation(FVector(0.0f, 0.0f, SpawnHeightOffset));
	}
	if (SpawnInfoPreview)
	{
		SpawnInfoPreview->SetRelativeLocation(FVector(0.0f, 0.0f, SpawnHeightOffset + 100.0f));
		SpawnInfoPreview->SetText(FText::FromString(FString::Printf(
			TEXT("Weight: %.2f\nRadius: %.0f cm\nHeight: %.0f cm"),
			SpawnWeight,
			SpawnRadius,
			SpawnHeightOffset)));
	}
#endif
}

void ARoomCombatSpawnPoint::BeginPlay()
{
	Super::BeginPlay();

	// WP 재로드 시 배치 기본값에서 시작한다. 등록 과정에서 진행 중인 그룹의 상태를 다시 적용한다.
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

	// 한 호출의 탐색량만 제한한다. 전부 막혀도 Wave 요청을 버리지 않고 Subsystem이 다시 시도한다.
	constexpr int32 MaxLocationAttempts = 12;
	FRandomStream RandomStream(SearchSeed);
	const FVector Origin = GetActorLocation() + FVector::UpVector * SpawnHeightOffset;
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
		const bool bBlocked = World->OverlapBlockingTestByChannel(
			Candidate,
			Rotation,
			Capsule->GetCollisionObjectType(),
			CollisionShape,
			QueryParams,
			FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()));
		if (!bBlocked)
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
	// 공용 BP는 Room/그룹 없이 컴파일할 수 있다. 배치 위치별 귀속은 실제 인스턴스에서 필수 검사한다.
	const bool bValidatePlacement = !IsTemplate();
	if (bValidatePlacement && !RoomTag.IsValid())
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
	if (!FMath::IsFinite(SpawnHeightOffset) || SpawnHeightOffset < 0.0f)
	{
		Context.AddError(FText::FromString(
			TEXT("A RoomCombatSpawnPoint requires a non-negative SpawnHeightOffset.")));
		Result = EDataValidationResult::Invalid;
	}
	if (bValidatePlacement && !bInitiallyActive && !ActivationGroupTag.IsValid())
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
