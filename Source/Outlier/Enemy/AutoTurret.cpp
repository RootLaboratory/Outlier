#include "Enemy/AutoTurret.h"
#include "Weapon/LaserWeapon.h"
#include "GAS/Attributes/OutlierShieldAttributeSet.h"
#include "Drone/Partner/PartnerShieldRecipientComponent.h"
#include "Audio/OutlierAudioFeedback.h"
#include "Audio/OutlierAudioSubsystem.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StateTreeComponent.h"
#include "Drone/Partner/HackableComponent.h"
#include "Drone/Partner/HackGameplayTags.h"
#include "Enemy/EnemyAdaptationSubsystem.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/GameInstance.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"
#include "Network/OutlierArenaSubsystem.h"
#include "Outlier.h"
#include "OutlierArenaSettings.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Room/RoomCombatDefinition.h"
#include "Room/RoomCombatSubsystem.h"
#include "Room/RoomTagComponent.h"
#include "Save/OutlierSaveSubSystem.h"
#include "Shooter/ShooterCharacter.h"
#include "Team/OutlierTeamIds.h"
#include "TimerManager.h"
#include "Weapon/RangedWeaponBase.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "PostProcess/OutlierStencilSubsystem.h"

#if WITH_EDITOR
#include "EngineUtils.h"
#include "Misc/DataValidation.h"
#endif

namespace
{
	const FName HackTargetNeckBone(TEXT("TurretNeck"));

	TAutoConsoleVariable<int32> CVarTurretDiagnostics(
		TEXT("outlier.Enemy.TurretDiagnostics"),
		0,
		TEXT("Logs AutoTurret task failures, rejected damage, and applied impact reactions. 0: off, 1: on"),
		ECVF_Cheat);
}

AAutoTurret::AAutoTurret()
{
	ShieldAttributeSet = CreateDefaultSubobject<UOutlierShieldAttributeSet>(TEXT("ShieldAttributeSet"));
	ShieldAttributeSet->InitShield(0.0f);
	ShieldAttributeSet->InitMaxShield(0.0f);
	PartnerShieldRecipient = CreateDefaultSubobject<UPartnerShieldRecipientComponent>(TEXT("PartnerShieldRecipient"));
	DeathAudioContext = OutlierAudioFeedback::Tag(TEXT("Audio.Context.Enemy.Turret.Gun.State.Death"));
	DownAudioContext = OutlierAudioFeedback::Tag(TEXT("Audio.Context.Enemy.Turret.Gun.State.Down"));
	DeployAudioContext = OutlierAudioFeedback::Tag(TEXT("Audio.Context.Enemy.Turret.Gun.State.Appear"));
	HackedAudioContext = OutlierAudioFeedback::Tag(TEXT("Audio.Context.Enemy.Turret.Gun.State.Hacked"));
	GetOutlierAbilitySystemComponent()->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
	bUseCoreWeakPoint = false;
	ApplyClassStatOverrides();

	// 데디 서버에서도 전개 애니메이션의 본 위치를 갱신해야 Physics Asset이 열린 해치 위치를 따라간다.
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	// 몸체의 목 본은 전개 높이를 따라가고, 별도 Head 피벗의 조준 회전은 상속하지 않는다.
	HackTargetPoint->SetupAttachment(GetMesh(), HackTargetNeckBone);

	TurretHeadPivot = CreateDefaultSubobject<USceneComponent>(TEXT("TurretHeadPivot"));
	TurretHeadPivot->SetupAttachment(GetRootComponent());
	TurretHeadPivot->SetIsReplicated(true);

	TurretHeadPitchPivot = CreateDefaultSubobject<USceneComponent>(TEXT("TurretHeadPitchPivot"));
	TurretHeadPitchPivot->SetupAttachment(TurretHeadPivot);
	TurretHeadPitchPivot->SetIsReplicated(true);

	TurretHeadMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("TurretHeadMesh"));
	TurretHeadMesh->SetupAttachment(TurretHeadPitchPivot);
	TurretHeadMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TurretHeadMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	TurretHeadMesh->SetIsReplicated(true);

	// 터렛의 몸통 조준점은 Base가 아니라 회전하는 Head를 따라간다.
	AutoAimBodyAnchor->SetupAttachment(TurretHeadMesh);

	TurretBlockingBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TurretBlockingBox"));
	TurretBlockingBox->SetupAttachment(GetRootComponent());
	TurretBlockingBox->InitBoxExtent(FVector(50.0f));
	TurretBlockingBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TurretBlockingBox->SetCollisionObjectType(ECC_WorldDynamic);
	TurretBlockingBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	TurretBlockingBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	// 이동 차단용 Box가 총알을 먼저 가로채지 않도록 피해 Trace는 실제 Body/Head Physics Asset까지 통과시킨다.
	TurretBlockingBox->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);
	TurretBlockingBox->SetGenerateOverlapEvents(false);

	// ACharacter의 Root Capsule은 계층 유지를 위해 남기되 터렛 Collision에는 사용하지 않는다.
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
	{
		MovementComponent->DisableMovement();
		MovementComponent->MaxFlySpeed = 0.0f;
	}

	HackableComponent->HackTags.AddTag(HackGameplayTags::Info::Turret());
	ConfigureTurretHackPolicy();
}

bool AAutoTurret::IsTurretDiagnosticsEnabled()
{
	return CVarTurretDiagnostics.GetValueOnGameThread() != 0;
}

void AAutoTurret::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ConfigureHeadPivotAttachment();
}

void AAutoTurret::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// 단일 Actor가 해치 표현과 터렛을 모두 가지므로, EnemyBase BeginPlay보다 먼저
	// 대기 상태를 고정해야 일반 배치 Enemy 등록과 StateTree 시작이 발생하지 않는다.
	PrepareForRoomWaveActivation();
}

void AAutoTurret::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
	{
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UOutlierSaveSubSystem* SaveSubsystem = GameInstance
		? GameInstance->GetSubsystem<UOutlierSaveSubSystem>()
		: nullptr;
	if (!SaveSubsystem
		|| !SaveSubsystem->RegisterPersistentTurretId(PersistentTurretId, this))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomCombat] Wave turret Stable ID registration failed. Turret=%s Id=%s"),
			*GetNameSafe(this), *PersistentTurretId.ToString());
		return;
	}
	bPersistentTurretIdRegistered = true;
	if (SaveSubsystem->IsTurretDestroyed(PersistentTurretId))
	{
		// 체크포인트 복원은 사망 이벤트와 연출을 다시 발생시키지 않고 저장된 최종 자세를 직접 적용한다.
		if (!RestoreDeadPersistentState())
		{
			UE_LOG(LogTemp, Error,
				TEXT("[Checkpoint] Failed to restore destroyed Wave turret Turret=%s Id=%s"),
				*GetNameSafe(this), *PersistentTurretId.ToString());
		}
		else if (IsTurretDiagnosticsEnabled())
		{
			UE_LOG(LogOutlier, Display,
				TEXT("[TurretDiag][Checkpoint] Restored Turret=%s Id=%s State=%d"),
				*GetNameSafe(this), *PersistentTurretId.ToString(),
				static_cast<int32>(TurretLifecycleState));
		}
		return;
	}

	URoomCombatSubsystem* CombatSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<URoomCombatSubsystem>()
		: nullptr;
	const FGameplayTag RoomTag = GetDefaultRoomTag();
	if (CombatSubsystem
		&& CombatSubsystem->RegisterWaveTurret(
			this, RoomTag, CombatPhaseIndex, WaveIndex))
	{
		bWaveTurretRegistered = true;
		return;
	}

	// 두 등록은 하나의 배치 설정 계약이다. Room 등록이 실패하면 Stable ID 슬롯도 되돌린다.
	SaveSubsystem->UnregisterPersistentTurretId(PersistentTurretId, this);
	bPersistentTurretIdRegistered = false;
}

#if WITH_DEV_AUTOMATION_TESTS
void AAutoTurret::ConfigureWaveRegistrationForTesting(
	FGameplayTag InRoomTag,
	int32 InCombatPhaseIndex,
	int32 InWaveIndex,
	FName InPersistentTurretId)
{
	// 순수 C++ 테스트 Actor에는 BP의 EnemyStatRow가 없으므로 BeginPlay에서 체력 0으로 사망하지 않게 한다.
	RuntimeStat.Health = 100.0f;
	if (URoomTagComponent* RoomTagComp = GetRoomTagComp())
	{
		RoomTagComp->AssignDefaultRoomTag(InRoomTag);
	}
	CombatPhaseIndex = InCombatPhaseIndex;
	WaveIndex = InWaveIndex;
	PersistentTurretId = InPersistentTurretId;
}
#endif

void AAutoTurret::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAutoTurret, ReplicatedLifecycle);
	DOREPLIFETIME(AAutoTurret, bHackedToPlayerTeam);
}

float AAutoTurret::ReceiveOutlierDamage(const FOutlierDamageRequest& Request)
{
	const UPrimitiveComponent* HitComponent = Request.HitResult.GetComponent();
	const FName HitBoneName = Request.HitResult.BoneName;
	const FString DamageTagString = Request.DamageTag.ToString();
	if (IsTurretDiagnosticsEnabled())
	{
		UE_LOG(LogOutlier, Display, TEXT("[TurretDiag][Damage] Received Turret=%s Authority=%d Active=%d Lifecycle=%d Team=%d CanBeDamaged=%d Dead=%d HP=%.2f Damage=%.2f Component=%s Bone=%s Tag=%s Causer=%s"),
			*GetName(), HasAuthority(), IsCombatActive(), static_cast<int32>(TurretLifecycleState), GetGenericTeamId().GetId(), CanBeDamaged(), IsDead(), GetCurrentHealth(),
			Request.DamageAmount, *GetNameSafe(HitComponent), *HitBoneName.ToString(), *DamageTagString, *GetNameSafe(Request.DamageCauser));
	}

	if (!IsCombatActive())
	{
		if (IsTurretDiagnosticsEnabled())
		{
			UE_LOG(
				LogOutlier,
				Warning,
				TEXT("[TurretDiag][Damage] Rejected Reason=%s Turret=%s Authority=%s Damage=%.2f HP=%.2f Component=%s Bone=%s Tag=%s Causer=%s"),
				IsWaitingForRoomWaveActivation() ? TEXT("WaitingForRoomWave") : TEXT("NotActive"),
				*GetNameSafe(this),
				HasAuthority() ? TEXT("true") : TEXT("false"),
				Request.DamageAmount,
				GetCurrentHealth(),
				*GetNameSafe(HitComponent),
				*HitBoneName.ToString(),
				*DamageTagString,
				*GetNameSafe(Request.DamageCauser));
		}
		return 0.0f;
	}

	if (Request.DamageTag.IsValid())
	{
		if (Request.DamageTag.MatchesTag(OutlierGameplayTags::Damage::Weapon())
			&& HitComponent == GetMesh()
			&& IsNoDamageBone(HitBoneName))
		{
			if (IsTurretDiagnosticsEnabled())
			{
				UE_LOG(
					LogOutlier,
					Warning,
					TEXT("[TurretDiag][Damage] Rejected Reason=NoDamageBone Turret=%s Authority=%s Damage=%.2f HP=%.2f Component=%s Bone=%s Tag=%s Causer=%s"),
					*GetNameSafe(this),
					HasAuthority() ? TEXT("true") : TEXT("false"),
					Request.DamageAmount,
					GetCurrentHealth(),
					*GetNameSafe(HitComponent),
					*HitBoneName.ToString(),
					*DamageTagString,
					*GetNameSafe(Request.DamageCauser));
			}
			return 0.0f;
		}
	}

	const float HealthBefore = GetCurrentHealth();
	const float AppliedDamage = Super::ReceiveOutlierDamage(Request);
	if (IsTurretDiagnosticsEnabled())
	{
		UE_LOG(LogOutlier, Display, TEXT("[TurretDiag][Damage] Result Turret=%s Requested=%.2f Applied=%.2f HP=%.2f->%.2f Authority=%d CanBeDamaged=%d Dead=%d Tag=%s Causer=%s"),
			*GetName(), Request.DamageAmount, AppliedDamage, HealthBefore, GetCurrentHealth(), HasAuthority(), CanBeDamaged(), IsDead(), *DamageTagString, *GetNameSafe(Request.DamageCauser));
	}
	return AppliedDamage;
}

FGenericTeamId AAutoTurret::GetGenericTeamId() const
{
	return FGenericTeamId(bHackedToPlayerTeam ? OutlierTeamIds::Player : OutlierTeamIds::Enemy);
}

FVector AAutoTurret::GetPawnViewLocation() const
{
	if (TurretHeadMesh)
	{
		FVector MuzzleLocationSum = FVector::ZeroVector;
		for (const FName SocketName : AimOriginMuzzleSockets)
		{
			MuzzleLocationSum += TurretHeadMesh->GetSocketLocation(SocketName);
		}

		if (!AimOriginMuzzleSockets.IsEmpty())
		{
			// 교대 총구 전체의 중심을 사용해 발사 그룹이 바뀌어도 조준 Pitch가 흔들리지 않게 한다.
			return MuzzleLocationSum / static_cast<float>(AimOriginMuzzleSockets.Num());
		}

		// 총구 그룹을 아직 설정하지 않은 터렛도 낮은 Base Pivot이 아니라 Head 중심에서 조준한다.
		if (TurretHeadMesh->GetSkeletalMeshAsset())
		{
			return TurretHeadMesh->Bounds.Origin;
		}
	}

	return TurretHeadPivot ? TurretHeadPivot->GetComponentLocation() : Super::GetPawnViewLocation();
}

FVector AAutoTurret::GetHackTargetLocation() const
{
	FVector TargetLocation;
	if (HackableComponent && HackableComponent->TryGetHackTargetLocation(TargetLocation))
	{
		return TargetLocation;
	}

	// 명시적 목표점이 없을 때만 기존 머리 시점을 사용한다. 전투용 시점 계산은 그대로 유지한다.
	return GetPawnViewLocation();
}

FVector AAutoTurret::GetCombatAimPoint(const AActor* TargetActor) const
{
	// 커밋 레이저는 기존 중앙/잠금 좌표를 유지한다. 일반 원거리는 공통 노출 조준 결과를 사용한다.
	if (!Cast<ALaserWeapon>(GetCurrentWeapon())) return Super::GetCombatAimPoint(TargetActor);
	if (const ACharacter* TargetCharacter = Cast<ACharacter>(TargetActor))
	{
		// ACharacter의 ActorLocation은 보통 Capsule 중심이지만, 상대 위치가 조정된 BP도 정확히 처리하도록
		// 실제 Collision Bounds 중심을 사용한다. HalfHeight를 다시 더하면 머리 위를 조준하게 된다.
		if (const UCapsuleComponent* TargetCapsule = TargetCharacter->GetCapsuleComponent())
		{
			return TargetCapsule->Bounds.Origin;
		}
	}

	return Super::GetCombatAimPoint(TargetActor);
}

FRotator AAutoTurret::GetLocalHeadRotation() const
{
	FRotator Rotation = CurrentAimOffset + ImpactRotationOffset;
	const float DownLimit = FMath::IsFinite(RuntimeTurretBehavior.MaxPitchDegrees)
		? FMath::Clamp(RuntimeTurretBehavior.MaxPitchDegrees, 0.0f, 90.0f) : 60.0f;
	Rotation.Pitch = FMath::Clamp(Rotation.Pitch, -DownLimit, 90.0f);
	Rotation.Roll = 0.0f;
	return Rotation;
}

FRotator AAutoTurret::GetViewRotation() const
{
	if (!TurretHeadPitchPivot)
	{
		return Super::GetViewRotation();
	}

	const FRotator LocalAimRotation = GetLocalHeadRotation();
	return (GetActorQuat() * LocalAimRotation.Quaternion()).Rotator();
}

void AAutoTurret::ClearHitClues()
{
	if (!HasAuthority()) return;
	Super::ClearHitClues();
	bHitGuardRotationActive = false;
	HitGuardElapsed = 0.0f;
}

void AAutoTurret::OnDirectSightForHitResponse(const FVector& ObservedLocation)
{
	if (!HasAuthority() || ObservedLocation.ContainsNaN()) return;
	// 시각 관측 콜백에서 즉시 양보해 같은 프레임의 옛 탐색이 전투 회전을 덮지 않게 한다.
	bHitGuardRotationActive = false;
	Super::OnDirectSightForHitResponse(ObservedLocation);
}

void AAutoTurret::TickHitResponse(float DeltaTime)
{
	if (!HasAuthority() || bProcessingDamageHitResponse) return;
	const bool bWasConfirming = IsHitDirectionConfirmationActive();
	const FVector ConfirmedLocation = ActiveHitClue.Location;
	Super::TickHitResponse(DeltaTime);
	if (!CanUseEnemyPerception() || GetCombatState() == EEnemyCombatState::Stun || HasActiveStunTag()
		|| IsPlayerCurrentlyVisible() || HasTrackableTarget() || IsHitResponseCommitted())
	{
		bHitGuardRotationActive = false;
		return;
	}
	if (HasHitResponse())
	{
		bHitGuardRotationActive = false;
		return;
	}
	if (bWasConfirming)
	{
		// 자연 완료한 확인만 경계 중심으로 인계한다. 팀/생명 정리는 ClearHitClues에서 폐기한다.
		const FVector Direction = GetActorQuat().UnrotateVector(ConfirmedLocation - GetPawnViewLocation());
		const double HorizontalDistance = FVector2D(Direction.X, Direction.Y).Size();
		HitGuardCenterYaw = HorizontalDistance > FMath::Max(1.0, Direction.Size() * 0.001)
			? FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X)) : CurrentAimOffset.Yaw;
		HitGuardElapsed = 0.0f;
		bHitGuardRotationActive = true;
	}
	if (!bHitGuardRotationActive) return;
	if (GetCombatState() == EEnemyCombatState::NonCombat)
	{
		const float NeutralPitch = FMath::Clamp(RuntimeTurretBehavior.SearchNeutralPitchDegrees,
			FMath::Min(RuntimeTurretBehavior.SearchMinPitchDegrees, RuntimeTurretBehavior.SearchMaxPitchDegrees),
			FMath::Max(RuntimeTurretBehavior.SearchMinPitchDegrees, RuntimeTurretBehavior.SearchMaxPitchDegrees));
		const FVector DefaultDirection = GetActorQuat().RotateVector(FRotator(NeutralPitch, 0.0f, 0.0f).Vector());
		if (UpdateTurretAimAtLocation(GetPawnViewLocation() + DefaultDirection * 10000.0f, DeltaTime, false, true))
			bHitGuardRotationActive = false;
		return;
	}
	// 탐색은 상태를 연장하지 않는다. Alert/Combat의 기존 종료 판단이 끝나면 기본 방향으로 복귀한다.
	HitGuardElapsed += FMath::Max(0.0f, DeltaTime);
	const float HalfCycle = FMath::Max(0.01f, RuntimeTurretBehavior.SearchHalfCycleDuration);
	const float Yaw = HitGuardCenterYaw + FMath::Sin(HitGuardElapsed * PI / HalfCycle) * RuntimeTurretBehavior.SearchYawDegrees;
	const FVector SearchDirection = GetActorQuat().RotateVector(FRotator(0.0f, Yaw, 0.0f).Vector());
	UpdateTurretAimAtLocation(GetPawnViewLocation() + SearchDirection * 10000.0f, DeltaTime, false, true);
}

bool AAutoTurret::CanUseEnemyPerception() const
{
	return IsCombatActive()
		&& !IsAIControlSuppressed()
		&& GetCurrentHealth() > 0.0f;
}

bool AAutoTurret::CanUseRoomTargetSharing() const
{
	return IsCombatActive()
		&& !bHackedToPlayerTeam
		&& !IsAIControlSuppressed();
}

USkeletalMeshComponent* AAutoTurret::GetWeaponMuzzleComponent(bool bFirstPerson) const
{
	(void)bFirstPerson;
	return TurretHeadMesh;
}

FName AAutoTurret::GetWeaponMuzzleSocketName(bool bFirstPerson) const
{
	(void)bFirstPerson;
	return MuzzleGroupPrefixes.IsValidIndex(CurrentMuzzleGroupIndex)
		? MuzzleGroupPrefixes[CurrentMuzzleGroupIndex]
		: NAME_None;
}

void AAutoTurret::GetWeaponMuzzleSocketNames(
	bool bFirstPerson, TArray<FName>& OutSocketNames) const
{
	USkeletalMeshComponent* MuzzleMesh = GetWeaponMuzzleComponent(bFirstPerson);
	const FName GroupPrefix = GetWeaponMuzzleSocketName(bFirstPerson);
	if (!MuzzleMesh || GroupPrefix.IsNone())
	{
		return;
	}

	const FString PrefixString = GroupPrefix.ToString();
	for (const FName SocketName : MuzzleMesh->GetAllSocketNames())
	{
		const FString SocketString = SocketName.ToString();
		// L_/R_처럼 방향 접두사가 붙은 총구도 Muzzle_Up 그룹 하나로 함께 수집한다.
		if (SocketName == GroupPrefix
			|| SocketString.StartsWith(PrefixString)
			|| SocketString.EndsWith(PrefixString))
		{
			OutSocketNames.Add(SocketName);
		}
	}

	OutSocketNames.Sort(
		[](const FName& Left, const FName& Right)
		{
			return Left.LexicalLess(Right);
		});
}

bool AAutoTurret::UsesIndependentMuzzleShots() const
{
	return MuzzleGroupPrefixes.Num() > 0;
}

void AAutoTurret::ResetWeaponMuzzleSequence()
{
	CurrentMuzzleGroupIndex = 0;
}

void AAutoTurret::AdvanceWeaponMuzzleSequence()
{
	if (!MuzzleGroupPrefixes.IsEmpty())
	{
		CurrentMuzzleGroupIndex = (CurrentMuzzleGroupIndex + 1) % MuzzleGroupPrefixes.Num();
	}
}

void AAutoTurret::PrepareForStateTreeStart()
{
	// BP에 저장된 부모 기본값이 C++ 생성자 값을 덮어써도 터렛은 빙의 대신 팀 전환만 수행한다.
	ConfigureTurretHackPolicy();
	if (const FAutoTurretBehaviorRow* Behavior =
		TurretBehaviorRow.GetRow<FAutoTurretBehaviorRow>(TEXT("AutoTurretBehaviorRow")))
	{
		RuntimeTurretBehavior = *Behavior;
	}
	ConfigureHeadPivotAttachment();
	ApplyTurretLifecycleState();
}

bool AAutoTurret::ShouldActivateAsPreplacedEnemy() const
{
	return IsCombatActive();
}

void AAutoTurret::ConfigureHeadPivotAttachment()
{
	CacheAimOriginMuzzleSockets();

	USkeletalMeshComponent* BodyMesh = GetMesh();
	if (HackTargetPoint && BodyMesh && BodyMesh->DoesSocketExist(HackTargetNeckBone)
		&& (HackTargetPoint->GetAttachParent() != BodyMesh || HackTargetPoint->GetAttachSocketName() != HackTargetNeckBone))
	{
		// 기존 BP의 루트 부착을 갱신하되, BP·인스턴스에서 조절한 목 기준 상대 Transform은 보존한다.
		// 목표점 참조는 건드리지 않으므로 별도로 지정한 사용자 컴포넌트도 계속 사용할 수 있다.
		HackTargetPoint->AttachToComponent(BodyMesh, FAttachmentTransformRules::KeepRelativeTransform, HackTargetNeckBone);
	}
	if (!TurretHeadPivot || !TurretHeadPitchPivot || !TurretHeadMesh || !BodyMesh
		|| HeadPivotSocketName.IsNone()
		|| !BodyMesh->DoesSocketExist(HeadPivotSocketName))
	{
		return;
	}

	TurretHeadPivot->AttachToComponent(
		BodyMesh,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale,
		HeadPivotSocketName);
	// 소켓 회전에는 FBX의 축 차이를 보정해 헤드를 수평으로 눕히는 중립 자세가 포함되어 있다.
	// 이 값은 장착 기준으로만 보존하고 조준 회전은 Actor 축에서 별도로 계산한다.
	HeadMountBasisRotation = GetActorQuat().Inverse() * TurretHeadPivot->GetComponentQuat();
	TurretHeadPivot->SetWorldRotation(GetActorRotation());
	TurretHeadPitchPivot->AttachToComponent(
		TurretHeadPivot,
		FAttachmentTransformRules::KeepRelativeTransform);
	TurretHeadPitchPivot->SetRelativeLocationAndRotation(
		FVector::ZeroVector,
		FRotator::ZeroRotator);

	// 기존 BP에서 조정한 Head Mesh의 상대 Transform은 보존한 채 Pitch 피벗 아래로 옮긴다.
	if (TurretHeadMesh->GetAttachParent() != TurretHeadPitchPivot)
	{
		TurretHeadMesh->AttachToComponent(
			TurretHeadPitchPivot,
			FAttachmentTransformRules::KeepRelativeTransform);
	}

}

void AAutoTurret::CacheAimOriginMuzzleSockets()
{
	AimOriginMuzzleSockets.Reset();
	if (!TurretHeadMesh)
	{
		return;
	}

	for (const FName SocketName : TurretHeadMesh->GetAllSocketNames())
	{
		const FString SocketString = SocketName.ToString();
		for (const FName GroupPrefix : MuzzleGroupPrefixes)
		{
			const FString PrefixString = GroupPrefix.ToString();
			if (SocketName == GroupPrefix
				|| SocketString.StartsWith(PrefixString)
				|| SocketString.EndsWith(PrefixString))
			{
				AimOriginMuzzleSockets.Add(SocketName);
				break;
			}
		}
	}
}

void AAutoTurret::ConfigureTurretHackPolicy()
{
	if (!HackableComponent)
	{
		return;
	}

	HackableComponent->HackTags.RemoveTag(HackGameplayTags::Target::Possessable());
	HackableComponent->HackTags.AddTag(HackGameplayTags::Target::NonPossessable());
	HackableComponent->HackTags.AddTag(HackGameplayTags::Info::Turret());
	HackableComponent->SuccessEffectTags.RemoveTag(HackGameplayTags::Effect::Possess());
	HackableComponent->SuccessEffectTags.AddTag(HackGameplayTags::Effect::ChangeTeam());
}

bool AAutoTurret::BeginRoomWaveDeployment(int32 GameplayGeneration)
{
	// 배치 Wave 터렛은 AnimNotify가 실제 전개 완료를 증명해야 한다.
	// 시간 기반 자동 완료는 Wave 기준값을 연출보다 먼저 확정할 수 있으므로 사용하지 않는다.
	if (!HasAuthority() || GameplayGeneration < 0
		|| !IsWaitingForRoomWaveActivation()
		|| bRoomWaveDeploymentRequested)
	{
		return false;
	}

	RoomWaveGameplayGeneration = GameplayGeneration;
	RoomWaveActivationSerial = RoomWaveActivationSerial == MAX_int32
		? 1
		: RoomWaveActivationSerial + 1;
	bRoomWaveDeploymentRequested = true;
	if (!BeginTurretDeploymentInternal())
	{
		bRoomWaveDeploymentRequested = false;
		InvalidateRoomWaveLifecycle();
		return false;
	}
	return true;
}

bool AAutoTurret::BeginTurretDeploymentInternal()
{
	if (!HasAuthority()
		|| TurretLifecycleState == EAutoTurretLifecycleState::DeadPresentation
		|| TurretLifecycleState == EAutoTurretLifecycleState::DeadPersistent)
	{
		return false;
	}
	if (TurretLifecycleState == EAutoTurretLifecycleState::Deploying
		|| TurretLifecycleState == EAutoTurretLifecycleState::Active)
	{
		return true;
	}

	// 현재 Room Wave에 해당하는지는 호출자가 판정하고, 여기서는 승인된 전개의 상태와 연출만 시작한다.
	SetTurretLifecycleState(EAutoTurretLifecycleState::Deploying);
	MulticastBeginTurretDeployment(RoomWaveGameplayGeneration, RoomWaveActivationSerial);
	ForceNetUpdate();
	return true;
}

bool AAutoTurret::PrepareForRoomWaveActivation()
{
	// 배치 참조는 Server와 Client의 Level Actor에 모두 존재한다. Client는 BeginPlay 이전
	// 닫힘 표현만 먼저 맞추고, 런타임 상태 변경과 Subsystem 정리는 Server만 수행한다.
	if (!HasAuthority() && HasActorBegunPlay())
	{
		return false;
	}

	bRoomWaveDeploymentRequested = false;
	RoomWaveGameplayGeneration = INDEX_NONE;
	SetTurretLifecycleState(EAutoTurretLifecycleState::WaitingForWave);
	return true;
}

void AAutoTurret::PlayFireMontage()
{
	if (HasAuthority())
	{
		MulticastPlayFireMontage();
	}
}

void AAutoTurret::StopFireMontage()
{
	if (HasAuthority())
	{
		MulticastStopFireMontage();
	}
}

void AAutoTurret::NotifyDeploySequenceFinished(int32 GameplayGeneration, int32 ActivationSerial)
{
	if (!HasAuthority())
	{
		return;
	}

	if (!IsDeploying() || !bRoomWaveDeploymentRequested
		|| !MatchesRoomWaveLifecycle(GameplayGeneration, ActivationSerial))
	{
		if (IsTurretDiagnosticsEnabled())
		{
			UE_LOG(LogOutlier, Warning,
				TEXT("[TurretDiag][Deploy] CompletionRejected Turret=%s State=%d SuppliedGeneration=%d CurrentGeneration=%d SuppliedSerial=%d CurrentSerial=%d Owned=%s"),
				*GetNameSafe(this), static_cast<int32>(TurretLifecycleState),
				GameplayGeneration, RoomWaveGameplayGeneration,
				ActivationSerial, RoomWaveActivationSerial,
				bRoomWaveDeploymentRequested ? TEXT("true") : TEXT("false"));
		}
		return;
	}

	// Room Wave가 Pending 소유권까지 다시 확인해야 완료 콜백이 생존 집계에 들어간다.
	if (URoomCombatSubsystem* CombatSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<URoomCombatSubsystem>()
		: nullptr)
	{
		CombatSubsystem->NotifyWaveTurretDeploymentFinished(
			this, GameplayGeneration, ActivationSerial);
	}
}

void AAutoTurret::NotifyDeathSequenceFinished(int32 GameplayGeneration, int32 ActivationSerial)
{
	if (!HasAuthority()
		|| TurretLifecycleState != EAutoTurretLifecycleState::DeadPresentation
		|| !MatchesRoomWaveLifecycle(GameplayGeneration, ActivationSerial))
	{
		if (HasAuthority() && IsTurretDiagnosticsEnabled())
		{
			UE_LOG(LogOutlier, Warning,
				TEXT("[TurretDiag][Death] CompletionRejected Turret=%s State=%d SuppliedGeneration=%d CurrentGeneration=%d SuppliedSerial=%d CurrentSerial=%d"),
				*GetNameSafe(this), static_cast<int32>(TurretLifecycleState),
				GameplayGeneration, RoomWaveGameplayGeneration,
				ActivationSerial, RoomWaveActivationSerial);
		}
		return;
	}

	// 사망 연출 완료는 Actor 제거가 아니라 최종 자세 고정으로 끝난다.
	SetTurretLifecycleState(EAutoTurretLifecycleState::DeadPersistent);
	if (IsTurretDiagnosticsEnabled())
	{
		UE_LOG(LogOutlier, Display,
			TEXT("[TurretDiag][Death] CompletionAccepted Turret=%s Generation=%d Serial=%d State=%d"),
			*GetNameSafe(this), GameplayGeneration, ActivationSerial,
			static_cast<int32>(TurretLifecycleState));
	}
}

bool AAutoTurret::MatchesRoomWaveLifecycle(int32 GameplayGeneration, int32 ActivationSerial) const
{
	const UOutlierArenaSubsystem* ArenaSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaSubsystem>()
		: nullptr;
	const bool bMatchesArenaGeneration = !ArenaSubsystem
		|| static_cast<int32>(ArenaSubsystem->GetGameplayGeneration()) == GameplayGeneration;
	return GameplayGeneration >= 0
		&& GameplayGeneration == RoomWaveGameplayGeneration
		&& ActivationSerial != 0
		&& ActivationSerial == RoomWaveActivationSerial
		&& bMatchesArenaGeneration;
}

void AAutoTurret::InvalidateRoomWaveLifecycle()
{
	RoomWaveGameplayGeneration = INDEX_NONE;
	RoomWaveActivationSerial = RoomWaveActivationSerial == MAX_int32
		? 1
		: RoomWaveActivationSerial + 1;
}

void AAutoTurret::ResetRoomWaveLifecycle(const TCHAR* ResetReason)
{
	if (!HasAuthority())
	{
		return;
	}

	const EAutoTurretLifecycleState PreviousState = TurretLifecycleState;
	const int32 PreviousGeneration = RoomWaveGameplayGeneration;
	const int32 PreviousSerial = RoomWaveActivationSerial;
	InvalidateRoomWaveLifecycle();
	if (PartnerShieldRecipient) PartnerShieldRecipient->ClearShield();
	bRoomWaveDeploymentRequested = false;
	bHackedToPlayerTeam = false;
	// 풀 리셋은 ApplyHackedTeamState를 거치지 않으므로 외곽선 등록을 여기서 직접 해제한다.
	RefreshTeammateOutline();
	StopImpactRecovery();
	DestroyPoolAIController();
	ResetReusableCombatRuntime(ResetReason);
	InitializeFromEnemyStatRow();
	ResetPoolRuntimeState();
	SetTurretLifecycleState(EAutoTurretLifecycleState::WaitingForWave);
	ApplyHackedTeamState();
	ForceNetUpdate();

	if (IsTurretDiagnosticsEnabled())
	{
		UE_LOG(LogOutlier, Display,
			TEXT("[TurretDiag][Reset] Turret=%s Reason=%s PreviousState=%d PreviousGeneration=%d PreviousSerial=%d CurrentSerial=%d"),
			*GetNameSafe(this), ResetReason, static_cast<int32>(PreviousState),
			PreviousGeneration, PreviousSerial, RoomWaveActivationSerial);
	}
}

void AAutoTurret::CompleteTurretDeployment()
{
	if (!HasAuthority() || !IsDeploying())
	{
		return;
	}
	SetTurretLifecycleState(EAutoTurretLifecycleState::Active);
}

bool AAutoTurret::CompleteRoomWaveDeployment()
{
	if (!HasAuthority() || !IsDeploying())
	{
		return false;
	}

	CompleteTurretDeployment();
	bRoomWaveDeploymentRequested = false;
	SpawnDefaultController();
	RefreshPerceptionConfigForCurrentState();
	if (StateTreeComponent)
	{
		StateTreeComponent->StartLogic();
	}
	if (UEnemyAdaptationSubsystem* AdaptationSubsystem = GetEnemyAdaptationSubsystem())
	{
		AdaptationSubsystem->RegisterEnemy(this);
	}
	if (UEnemyRoomSubsystem* RoomSubsystem = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
	{
		// StateTree가 준비된 뒤 등록해야 현재 Room의 전투 상태와 공유 타깃 이벤트를 즉시 받을 수 있다.
		RoomSubsystem->RegisterEnemy(this);
	}
	ForceNetUpdate();
	return true;
}

bool AAutoTurret::IsCombatActive() const
{
	return TurretLifecycleState == EAutoTurretLifecycleState::Active;
}

void AAutoTurret::SetTurretLifecycleState(EAutoTurretLifecycleState NewState)
{
	TurretLifecycleState = NewState;
	// 상태와 식별자를 따로 복제하면 이전 활성화의 Active와 새 식별자가 섞일 수 있다.
	// BP 호환용 상태 값은 유지하되 복제 수신은 이 스냅샷 한 경로에서만 적용한다.
	ReplicatedLifecycle.State = NewState;
	ReplicatedLifecycle.GameplayGeneration = RoomWaveGameplayGeneration;
	ReplicatedLifecycle.ActivationSerial = RoomWaveActivationSerial;
	if (NewState == EAutoTurretLifecycleState::WaitingForWave)
	{
		ApplyWaitingForWaveState();
	}
	else
	{
		ApplyTurretLifecycleState();
	}
	RefreshEnemyAudioState();

	if (HasAuthority() && HasActorBegunPlay())
	{
		ForceNetUpdate();
	}
}

void AAutoTurret::ApplyTurretLifecycleState()
{
	const bool bUsesBodyQueryCollision = IsDeploying() || IsDeployed();
	SetActorEnableCollision(bUsesBodyQueryCollision);
	SetCanBeDamaged(IsCombatActive());
	ApplyTurretCollisionState();
	if (IsDeployed() && TurretLifecycleState != EAutoTurretLifecycleState::Active && DeployMontage)
	{
		// 사망은 Head의 별도 몽타주가 담당한다. 몸체는 사망 중에도 전개된 채로 남는다.
		HoldDeploymentPose(FMath::Max(DeployMontage->GetPlayLength() - KINDA_SMALL_NUMBER, 0.0f));
	}
	if (TurretLifecycleState == EAutoTurretLifecycleState::DeadPresentation)
	{
		// 멀티캐스트가 누락되거나 먼저 도착해도 복제 상태에서 연출을 복구한다.
		PlayDeathMontageIfNeeded();
	}
	else if (TurretLifecycleState == EAutoTurretLifecycleState::DeadPersistent)
	{
		ApplyDeadPersistentPose();
	}

	if (HackableComponent)
	{
		if (IsCombatActive())
		{
			HackableComponent->HackTags.RemoveTag(OutlierGameplayTags::State::Locked());
		}
		else
		{
			HackableComponent->HackTags.AddTag(OutlierGameplayTags::State::Locked());
			// 사망 후에도 Actor가 남으므로 EndPlay를 기다리지 않고 진행 중인 해킹을 종료한다.
			HackableComponent->NotifyHackTargetUnavailable();
		}
	}

	if (AEnemyAIController* EnemyController = Cast<AEnemyAIController>(GetController()))
	{
		EnemyController->SetEnemyPerceptionEnabled(CanUseEnemyPerception());
	}

	// BP 이벤트는 충돌·해킹·인지 상태를 먼저 맞춘 뒤 활성화마다 한 번만 전달한다.
	if (IsDeploying())
	{
		PlayDeploymentIfNeeded();
	}
	else if (TurretLifecycleState == EAutoTurretLifecycleState::Active)
	{
		ApplyDeploymentCompletedPresentation();
	}
}

void AAutoTurret::ApplyWaitingForWaveState()
{
	if (!IsWaitingForRoomWaveActivation())
	{
		ApplyTurretLifecycleState();
		return;
	}

	ClearHitClues();
	bInCombat = false;
	CombatState = EEnemyCombatState::NonCombat;
	bPlayerCurrentlyVisible = false;
	bHasSharedTargetContact = false;
	StopImpactRecovery();
	CurrentAimOffset = FRotator::ZeroRotator;
	CurrentAimLocation = FVector::ZeroVector;
	ResetTurretImpactRecovery();
	ResetWeaponMuzzleSequence();
	PresentedDeploymentGeneration = INDEX_NONE;
	PresentedDeploymentSerial = 0;
	bDeploymentCompletionPresented = false;
	StopMontageOnMesh(GetMesh(), DeployMontage);
	StopMontageOnMesh(TurretHeadMesh, FireMontage);
	StopMontageOnMesh(TurretHeadMesh, DeathMontage);
	// 명시적인 Waiting 진입에서만 닫힌 시작 자세로 되돌린다.
	HoldDeploymentPose(0.0f);

	// 초기화 순서상 전투 참여 흔적이 먼저 생겼더라도 대기 진입 경로에서 모두 되돌린다.
	if (HasActorBegunPlay())
	{
		StopCurrentAttack();
		RemoveRoomTargetObserver();
		ClearSharedTargetContact();
		ReleaseSearchDestinations();
		if (StateTreeComponent)
		{
			StateTreeComponent->StopLogic(TEXT("Turret waiting for Room Wave activation"));
		}

		if (HasAuthority())
		{
			if (UEnemyAdaptationSubsystem* AdaptationSubsystem = GetEnemyAdaptationSubsystem())
			{
				AdaptationSubsystem->UnregisterEnemy(this);
			}
			if (URoomCombatSubsystem* CombatSubsystem =
				GetWorld()->GetSubsystem<URoomCombatSubsystem>())
			{
				CombatSubsystem->UnregisterEnemy(this);
			}
			if (UEnemyRoomSubsystem* RoomSubsystem = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
			{
				RoomSubsystem->UnregisterEnemy(this);
			}
		}
	}

	ApplyTurretLifecycleState();
}

void AAutoTurret::PlayDeploymentIfNeeded()
{
	if (!IsDeploying() || RoomWaveGameplayGeneration < 0 || RoomWaveActivationSerial == 0
		|| (PresentedDeploymentGeneration == RoomWaveGameplayGeneration
			&& PresentedDeploymentSerial == RoomWaveActivationSerial))
	{
		return;
	}

	PresentedDeploymentGeneration = RoomWaveGameplayGeneration;
	PresentedDeploymentSerial = RoomWaveActivationSerial;
	bDeploymentCompletionPresented = false;
	PlayMontageOnMesh(GetMesh(), DeployMontage);
	if (UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
	{
		if (FAnimMontageInstance* Instance = AnimInstance->GetActiveInstanceForMontage(DeployMontage))
		{
			// 공유 에셋을 바꾸지 않고 이번 전개만 끝 자세를 유지한다.
			Instance->bEnableAutoBlendOut = false;
		}
	}

	if (GetNetMode() != NM_DedicatedServer)
	{
		if (UGameInstance* GameInstance = GetGameInstance())
		{
			if (UOutlierAudioSubsystem* Audio = GameInstance->GetSubsystem<UOutlierAudioSubsystem>())
			{
				FOutlierAudioPlayRequest Request;
				Request.EventTag = OutlierAudioFeedback::Tag(TEXT("Audio.Type.Enemy"));
				Request.ContextTags.AddTag(DeployAudioContext);
				Request.EmitterActor = this;
				Audio->PlayLocalAtLocation(Request);
			}
		}
	}
	OnTurretDeploymentStarted(RoomWaveGameplayGeneration, RoomWaveActivationSerial);
}

void AAutoTurret::ApplyDeploymentCompletedPresentation()
{
	if (!bDeploymentCompletionPresented
		|| PresentedDeploymentGeneration != RoomWaveGameplayGeneration
		|| PresentedDeploymentSerial != RoomWaveActivationSerial)
	{
		PresentedDeploymentGeneration = RoomWaveGameplayGeneration;
		PresentedDeploymentSerial = RoomWaveActivationSerial;
		bDeploymentCompletionPresented = true;
		OnTurretDeploymentCompleted();
	}

	// 완료 BP가 기존 몽타주를 중단하더라도 Active의 최종 자세는 C++에서 확정한다.
	if (IsDeployed() && DeployMontage)
	{
		HoldDeploymentPose(FMath::Max(DeployMontage->GetPlayLength() - KINDA_SMALL_NUMBER, 0.0f));
	}
}

void AAutoTurret::HoldDeploymentPose(float Position)
{
	UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance || !DeployMontage)
	{
		return;
	}

	FAnimMontageInstance* Instance = AnimInstance->GetActiveInstanceForMontage(DeployMontage);
	if (!Instance || Instance->GetWeight() < 1.0f - KINDA_SMALL_NUMBER
		|| Instance->GetDesiredWeight() < 1.0f - KINDA_SMALL_NUMBER)
	{
		// 늦은 관련성 복귀나 blend-out 뒤에는 시작 프레임을 거치지 않고 최종 위치에서 복원한다.
		// blend-in 중 pause하면 기본 자세가 섞인 채 남으므로 고정 자세는 즉시 가중치 1로 적용한다.
		// 기존 인스턴스를 먼저 끝내야 같은 몽타주의 이전 재생이 Notify를 계속 발생시키지 않는다.
		AnimInstance->Montage_Stop(0.0f, DeployMontage);
		AnimInstance->Montage_PlayWithBlendIn(DeployMontage, FAlphaBlendArgs(0.0f), 1.0f,
			EMontagePlayReturnType::MontageLength, Position, false);
		Instance = AnimInstance->GetActiveInstanceForMontage(DeployMontage);
		if (Instance)
		{
			// 0초 blend도 다음 애니메이션 갱신 전에는 가중치가 0일 수 있다.
			// 재생 시간을 진행하지 않고 확정해 같은 프레임의 중복 적용도 재생을 교체하지 않게 한다.
			Instance->UpdateWeight(0.0f);
		}
	}
	if (Instance)
	{
		Instance->bEnableAutoBlendOut = false;
		AnimInstance->Montage_SetPosition(DeployMontage, Position);
		AnimInstance->Montage_Pause(DeployMontage);
	}
}

void AAutoTurret::PlayDeathMontageIfNeeded()
{
	UAnimInstance* AnimInstance = TurretHeadMesh ? TurretHeadMesh->GetAnimInstance() : nullptr;
	if (!AnimInstance || !DeathMontage || AnimInstance->Montage_IsActive(DeathMontage))
	{
		if (IsTurretDiagnosticsEnabled())
		{
			UE_LOG(LogOutlier, Display, TEXT("[TurretDiag][DeathAnim] PlaySkipped Turret=%s NetMode=%d Reason=%s Mesh=%s AnimInstance=%s Montage=%s State=%d"),
				*GetName(), static_cast<int32>(GetNetMode()), !AnimInstance ? TEXT("NoAnimInstance") : !DeathMontage ? TEXT("NoDeathMontage") : TEXT("AlreadyActive"),
				*GetNameSafe(TurretHeadMesh), *GetNameSafe(AnimInstance), *GetNameSafe(DeathMontage), static_cast<int32>(TurretLifecycleState));
		}
		return;
	}
	StopMontageOnMesh(TurretHeadMesh);
	// 기존 종료 delegate를 교체하지 않고 관측만 추가한다.
	AnimInstance->OnMontageEnded.AddUniqueDynamic(this, &AAutoTurret::LogDeathMontageEnded);
	const float PlayResult = AnimInstance->Montage_Play(DeathMontage);
	if (IsTurretDiagnosticsEnabled())
	{
		UE_LOG(LogOutlier, Display, TEXT("[TurretDiag][DeathAnim] PlayResult Turret=%s Authority=%d NetMode=%d Mesh=%s AnimInstance=%s Montage=%s Result=%.3f Active=%d Playing=%d Position=%.3f MeshTick=%d AnimTickOption=%d Slot=%s Generation=%d Serial=%d"),
			*GetName(), HasAuthority(), static_cast<int32>(GetNetMode()), *GetNameSafe(TurretHeadMesh), *GetNameSafe(AnimInstance), *GetNameSafe(DeathMontage),
			PlayResult, AnimInstance->Montage_IsActive(DeathMontage), AnimInstance->Montage_IsPlaying(DeathMontage), AnimInstance->Montage_GetPosition(DeathMontage),
			TurretHeadMesh->IsComponentTickEnabled(), static_cast<int32>(TurretHeadMesh->VisibilityBasedAnimTickOption),
			DeathMontage->SlotAnimTracks.IsEmpty() ? TEXT("None") : *DeathMontage->SlotAnimTracks[0].SlotName.ToString(), RoomWaveGameplayGeneration, RoomWaveActivationSerial);
	}
}

void AAutoTurret::LogDeathMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (Montage == DeathMontage && IsTurretDiagnosticsEnabled())
	{
		UE_LOG(LogOutlier, Display, TEXT("[TurretDiag][DeathAnim] Ended Turret=%s Authority=%d NetMode=%d Montage=%s Interrupted=%d State=%d Generation=%d Serial=%d"),
			*GetName(), HasAuthority(), static_cast<int32>(GetNetMode()), *GetNameSafe(Montage), bInterrupted,
			static_cast<int32>(TurretLifecycleState), RoomWaveGameplayGeneration, RoomWaveActivationSerial);
	}
}

void AAutoTurret::ApplyDeadPersistentPose()
{
	if (!TurretHeadMesh || !DeathMontage)
	{
		if (IsTurretDiagnosticsEnabled())
		{
			UE_LOG(LogOutlier, Warning, TEXT("[TurretDiag][DeathAnim] FinalPoseSkipped Turret=%s NetMode=%d Reason=MissingMeshOrMontage Mesh=%s Montage=%s"),
				*GetName(), static_cast<int32>(GetNetMode()), *GetNameSafe(TurretHeadMesh), *GetNameSafe(DeathMontage));
		}
		return;
	}

	UAnimInstance* AnimInstance = TurretHeadMesh->GetAnimInstance();
	if (!AnimInstance)
	{
		if (IsTurretDiagnosticsEnabled())
		{
			UE_LOG(LogOutlier, Warning, TEXT("[TurretDiag][DeathAnim] FinalPoseSkipped Turret=%s NetMode=%d Reason=NoAnimInstance"), *GetName(), static_cast<int32>(GetNetMode()));
		}
		return;
	}

	// 실시간 사망 연출에서는 현재 몽타주를 마지막 프레임에 고정하고,
	// 체크포인트 직접 복원에서는 같은 최종 프레임만 즉시 구성한다.
	float PlayResult = -1.0f;
	if (!AnimInstance->Montage_IsActive(DeathMontage))
	{
		PlayResult = AnimInstance->Montage_Play(DeathMontage);
	}
	const float FinalPoseTime = FMath::Max(DeathMontage->GetPlayLength() - KINDA_SMALL_NUMBER, 0.0f);
	AnimInstance->Montage_SetPosition(DeathMontage, FinalPoseTime);
	AnimInstance->Montage_Pause(DeathMontage);
	if (IsTurretDiagnosticsEnabled())
	{
		UE_LOG(LogOutlier, Display, TEXT("[TurretDiag][DeathAnim] FinalPose Turret=%s NetMode=%d Montage=%s PlayResult=%.3f RequestedPosition=%.3f Position=%.3f Active=%d Playing=%d"),
			*GetName(), static_cast<int32>(GetNetMode()), *GetNameSafe(DeathMontage), PlayResult, FinalPoseTime,
			AnimInstance->Montage_GetPosition(DeathMontage), AnimInstance->Montage_IsActive(DeathMontage), AnimInstance->Montage_IsPlaying(DeathMontage));
	}
}

bool AAutoTurret::RestoreDeadPersistentState()
{
	if (!HasAuthority() || PersistentTurretId.IsNone()
		|| (!IsDead()
			&& (!OutlierAbilitySystemComponent
				|| !OutlierAbilitySystemComponent->ApplyDeadStateToSelf())))
	{
		return false;
	}

	StopImpactRecovery();
	bRoomWaveDeploymentRequested = false;
	SetTurretLifecycleState(EAutoTurretLifecycleState::DeadPersistent);
	return true;
}

void AAutoTurret::ApplyTurretCollisionState()
{
	const bool bUsesBodyQueryCollision = IsDeploying() || IsDeployed();
	USkeletalMeshComponent* BodyMesh = GetMesh();
	if (BodyMesh)
	{
		BodyMesh->SetCollisionEnabled(bUsesBodyQueryCollision
			? ECollisionEnabled::QueryOnly
			: ECollisionEnabled::NoCollision);
		BodyMesh->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);

		const ECollisionEnabled::Type HiddenBodyCollision = bUsesBodyQueryCollision
			? ECollisionEnabled::QueryOnly
			: ECollisionEnabled::NoCollision;
		for (const FName BoneRoot : HiddenCollisionBoneRoots)
		{
			if (BoneRoot.IsNone())
			{
				continue;
			}

			BodyMesh->ForEachBodyBelow(
				BoneRoot,
				true,
				false,
				[HiddenBodyCollision](FBodyInstance* BodyInstance)
				{
					if (BodyInstance)
					{
						BodyInstance->SetCollisionEnabled(HiddenBodyCollision);
					}
				});
		}
	}

	// BP에서 TurretHeadPivot 아래에 추가한 Head Skeletal Mesh도 같은 단계로 전환한다.
	TArray<USceneComponent*> HeadChildren;
	if (TurretHeadPivot)
	{
		TurretHeadPivot->GetChildrenComponents(true, HeadChildren);
	}
	const ECollisionEnabled::Type HeadCollision = bUsesBodyQueryCollision
		? ECollisionEnabled::QueryOnly
		: ECollisionEnabled::NoCollision;
	for (USceneComponent* Child : HeadChildren)
	{
		if (USkeletalMeshComponent* HeadMesh = Cast<USkeletalMeshComponent>(Child))
		{
			HeadMesh->SetCollisionEnabled(HeadCollision);
			HeadMesh->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);

			// Physics Asset에서 개별 Body가 NoCollision로 저장되면 컴포넌트만 QueryOnly로 바꿔도 총알에 맞지 않는다.
			// 터렛 Head는 모든 Physics Body를 피해 판정용 Query Collision으로 통일한다.
			for (FBodyInstance* BodyInstance : HeadMesh->Bodies)
			{
				if (BodyInstance)
				{
					BodyInstance->SetCollisionEnabled(HeadCollision);
				}
			}
		}
	}

	// 상속 Capsule은 고정형 터렛의 형상과 맞지 않으므로 모든 상태에서 Collision을 사용하지 않는다.
	if (UCapsuleComponent* BlockingCapsule = GetCapsuleComponent())
	{
		BlockingCapsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// 전개 애니메이션 중에는 플레이어를 밀지 않고 완료된 뒤에만 Box로 이동을 차단한다.
	if (TurretBlockingBox)
	{
		// Box는 전개 후 Pawn 이동만 막고, Shooter Hitscan은 Body/Head의 Physics Asset이 받는다.
		TurretBlockingBox->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);
		TurretBlockingBox->SetCollisionEnabled(
			IsDeployed()
				? ECollisionEnabled::QueryAndPhysics
				: ECollisionEnabled::NoCollision);
	}
}

bool AAutoTurret::IsNoDamageBone(FName BoneName) const
{
	if (BoneName.IsNone())
	{
		return false;
	}

	const USkeletalMeshComponent* BodyMesh = GetMesh();
	for (const FName BoneRoot : NoDamageBoneRoots)
	{
		if (BoneName == BoneRoot
			|| (BodyMesh && !BoneRoot.IsNone() && BodyMesh->BoneIsChildOf(BoneName, BoneRoot)))
		{
			return true;
		}
	}
	return false;
}

void AAutoTurret::OnRep_TurretLifecycleState()
{
	TurretLifecycleState = ReplicatedLifecycle.State;
	RoomWaveGameplayGeneration = ReplicatedLifecycle.GameplayGeneration;
	RoomWaveActivationSerial = ReplicatedLifecycle.ActivationSerial;
	if (IsWaitingForRoomWaveActivation())
	{
		ApplyWaitingForWaveState();
	}
	else
	{
		ApplyTurretLifecycleState();
	}
	RefreshEnemyAudioState();
}

bool AAutoTurret::ShouldPlayCombatLoopAudio() const
{
	// 사망 연출 중이거나 전개 전인 터렛은 전투음을 내지 않는다.
	return Super::ShouldPlayCombatLoopAudio() && TurretLifecycleState == EAutoTurretLifecycleState::Active;
}

void AAutoTurret::PlayAttackAudio()
{
	if (AttackAudioContext.IsValid())
		OutlierAudioFeedback::World(this, *AttackAudioContext.ToString(), TEXT("Audio.Type.Enemy"));
}

void AAutoTurret::MulticastBeginTurretDeployment_Implementation(
	int32 GameplayGeneration,
	int32 ActivationSerial)
{
	// RPC가 속성 복제보다 먼저 도착하면 아직 재생하지 않는다. Deploying 복제 수신도
	// 같은 함수를 호출하므로 정상 전개는 누락되지 않고, 이전 활성화 RPC는 자세를 되돌리지 못한다.
	if (GameplayGeneration == RoomWaveGameplayGeneration && ActivationSerial == RoomWaveActivationSerial)
	{
		PlayDeploymentIfNeeded();
	}
}

void AAutoTurret::MulticastPlayFireMontage_Implementation()
{
	PlayMontageOnMesh(TurretHeadMesh, FireMontage);
}

void AAutoTurret::MulticastStopFireMontage_Implementation()
{
	StopMontageOnMesh(TurretHeadMesh, FireMontage);
}

void AAutoTurret::MulticastPlayDeathMontage_Implementation(
	int32 GameplayGeneration,
	int32 ActivationSerial)
{
	// 늦은 RPC가 복제된 최종 자세를 다시 사망 연출 처음으로 되돌리지 않게 한다.
	if (TurretLifecycleState == EAutoTurretLifecycleState::DeadPersistent)
	{
		return;
	}
	PlayDeathMontageIfNeeded();
	OnTurretDeathPresentationStarted(GameplayGeneration, ActivationSerial);
}

void AAutoTurret::PlayMontageOnMesh(USkeletalMeshComponent* TargetMesh, UAnimMontage* Montage)
{
	if (!TargetMesh || !Montage)
	{
		return;
	}

	if (UAnimInstance* AnimInstance = TargetMesh->GetAnimInstance())
	{
		AnimInstance->Montage_Play(Montage);
	}
}

void AAutoTurret::StopMontageOnMesh(USkeletalMeshComponent* TargetMesh, UAnimMontage* Montage)
{
	if (UAnimInstance* AnimInstance = TargetMesh ? TargetMesh->GetAnimInstance() : nullptr)
	{
		AnimInstance->Montage_Stop(0.0f, Montage);
	}
}

bool AAutoTurret::UpdateTurretAimAtActor(AActor* TargetActor, float DeltaTime, bool bAttackRotation)
{
	return IsValid(TargetActor)
		&& UpdateTurretAimAtLocation(GetCombatAimPoint(TargetActor), DeltaTime, bAttackRotation);
}

bool AAutoTurret::UsesNormalTurretAttackPolicy() const
{
	return !IsEnemyPossessed() && IsValid(GetCurrentWeapon()) && !Cast<ALaserWeapon>(GetCurrentWeapon());
}

bool AAutoTurret::IsTurretAimLocationReachable(const FVector& Location) const
{
	const FVector Direction = GetActorQuat().UnrotateVector(Location - GetPawnViewLocation());
	if (Direction.ContainsNaN() || Direction.IsNearlyZero())
	{
		return false;
	}
	const float DownLimit = FMath::IsFinite(RuntimeTurretBehavior.MaxPitchDegrees)
		? FMath::Clamp(RuntimeTurretBehavior.MaxPitchDegrees, 0.0f, 90.0f) : 60.0f;
	const double Pitch = FMath::RadiansToDegrees(FMath::Atan2(Direction.Z, FVector2D(Direction.X, Direction.Y).Size()));
	// 회전 함수는 불가능한 각도도 한계까지 추적한다. 공격 후보는 clamp 이전 방향으로 판정한다.
	return Pitch >= -DownLimit - KINDA_SMALL_NUMBER;
}

bool AAutoTurret::TryGetAttackableVisibleTargetAim(const AActor* Target, FVector& OutAimPoint) const
{
	const AEnemyAIController* AI = Cast<AEnemyAIController>(GetController());
	if (!HasAuthority() || !UsesNormalTurretAttackPolicy() || IsDead() || IsAIControlSuppressed()
		|| !AI || !AI->IsTargetDirectlyVisible(Target))
	{
		return false;
	}

	TArray<FVector, TInlineAllocator<16>> Points;
	if (const AShooterCharacter* Shooter = Cast<AShooterCharacter>(Target))
	{
		FOutlierSightObservation Observation;
		if (!Shooter->GetObservedAimPoints(this, Observation))
		{
			return false;
		}
		int32 SeenIndex = INDEX_NONE;
		auto Append = [&](const auto& Source, const auto& Indices)
		{
			for (int32 Index = 0; Index < Source.Num(); ++Index)
			{
				if (Indices[Index] == Observation.SeenBoneIndex)
				{
					SeenIndex = Points.Num();
				}
				Points.Add(Source[Index]);
			}
		};
		Append(Observation.BodyPoints, Observation.BodyBoneIndices);
		Append(Observation.HeadPoints, Observation.HeadBoneIndices);
		Append(Observation.OtherPoints, Observation.OtherBoneIndices);
		if (SeenIndex > 0)
		{
			Points.Swap(0, SeenIndex);
		}
		// 현재 조준 부위도 최신 자기 관측에 남아 있을 때만 후보 우선권을 유지한다.
		if (Target == GetTrackedTarget())
		{
			const FVector CurrentAim = GetCombatAimPoint(Target);
			const int32 Current = Points.IndexOfByPredicate([&](const FVector& Point) { return Point.Equals(CurrentAim, 1.0); });
			if (Current > 0)
			{
				Points.Swap(0, Current);
			}
		}
	}
	else
	{
		// Partner 등은 기존 조준 계약을 사용한다. 새로운 노출 본 정책을 확대하지 않는다.
		Points.Add(GetCombatAimPoint(Target));
	}

	int32 Checks = 16;
	for (const FVector& Point : Points)
	{
		if (IsTurretAimLocationReachable(Point) && GetCurrentWeapon()->HasClearTargetAttackPath(Target, Point, Checks))
		{
			OutAimPoint = Point;
			return true;
		}
	}
	return false;
}

bool AAutoTurret::IsTurretAttackAimReady(const AActor* Target) const
{
	if (!Target || Target != GetTrackedTarget())
	{
		return false;
	}
	const FVector AimPoint = GetCombatAimPoint(Target);
	if (!IsTurretAimLocationReachable(AimPoint))
	{
		return false;
	}
	const FVector Direction = (AimPoint - GetPawnViewLocation()).GetSafeNormal();
	const float Tolerance = FMath::IsFinite(RuntimeTurretBehavior.AimToleranceDegrees)
		? FMath::Clamp(RuntimeTurretBehavior.AimToleranceDegrees, 0.01f, 45.0f) : 2.0f;
	return FVector::DotProduct(GetViewRotation().Vector(), Direction) >= FMath::Cos(FMath::DegreesToRadians(Tolerance));
}

bool AAutoTurret::UpdateTurretAimAtLocation(
	const FVector& TargetLocation, float DeltaTime, bool bAttackRotation, bool bUseSearchPitch)
{
	if (!HasAuthority() || !IsCombatActive() || !TurretHeadPivot || TargetLocation.ContainsNaN()) return false;
	const FVector Direction = GetActorQuat().UnrotateVector(TargetLocation - GetPawnViewLocation());
	CurrentAimLocation = TargetLocation;
	const double HorizontalDistance = FVector2D(Direction.X, Direction.Y).Size();
	FRotator DesiredOffset = CurrentAimOffset;
	// 수직/근접 출처는 Yaw를 정할 수 없다. 현재 방향을 유지한 채 도달 가능한 Pitch만 확인한다.
	if (HorizontalDistance > FMath::Max(1.0, Direction.Size() * 0.001))
		DesiredOffset.Yaw = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
	if (bUseSearchPitch)
	{
		DesiredOffset.Pitch = FMath::Clamp(RuntimeTurretBehavior.SearchNeutralPitchDegrees,
			FMath::Min(RuntimeTurretBehavior.SearchMinPitchDegrees, RuntimeTurretBehavior.SearchMaxPitchDegrees),
			FMath::Max(RuntimeTurretBehavior.SearchMinPitchDegrees, RuntimeTurretBehavior.SearchMaxPitchDegrees));
	}
	else if (!Direction.IsNearlyZero())
	{
		DesiredOffset.Pitch = FMath::RadiansToDegrees(FMath::Atan2(Direction.Z, HorizontalDistance));
	}
	const float DownLimit = FMath::IsFinite(RuntimeTurretBehavior.MaxPitchDegrees)
		? FMath::Clamp(RuntimeTurretBehavior.MaxPitchDegrees, 0.0f, 90.0f) : 60.0f;
	DesiredOffset.Pitch = FMath::Clamp(DesiredOffset.Pitch, -DownLimit, 90.0f);
	DesiredOffset.Roll = 0.0f;
	const float YawError = FMath::FindDeltaAngleDegrees(CurrentAimOffset.Yaw, DesiredOffset.Yaw);
	const float PitchError = DesiredOffset.Pitch - CurrentAimOffset.Pitch;
	float Speed = bAttackRotation
		? RuntimeTurretBehavior.AttackRotationSpeedDegrees : RuntimeTurretBehavior.DefaultRotationSpeedDegrees;
	const float Tolerance = FMath::Max(0.01f, RuntimeTurretBehavior.AimToleranceDegrees);
	// 확인/복귀는 목표 근처에서 감속한다. 공격 추적은 기존 종류별 속도를 그대로 사용한다.
	if (!bAttackRotation)
	{
		const float AngularError = FMath::Max(FMath::Abs(YawError), FMath::Abs(PitchError));
		Speed = FMath::Min(Speed, FMath::Max(Tolerance * 2.0f, AngularError * 6.0f));
	}
	const float Step = FMath::Max(0.0f, DeltaTime) * FMath::Max(0.0f, Speed);
	// Euler 전체 보간 대신 축별 최단 각도를 사용해 ±180도 횡단과 +90도에서 축이 뒤집히지 않게 한다.
	CurrentAimOffset.Yaw = FMath::UnwindDegrees(CurrentAimOffset.Yaw + FMath::Clamp(YawError, -Step, Step));
	CurrentAimOffset.Pitch += FMath::Clamp(PitchError, -Step, Step);
	CurrentAimOffset.Roll = 0.0f;
	ApplyHeadRotation();
	return FMath::Abs(FMath::FindDeltaAngleDegrees(CurrentAimOffset.Yaw, DesiredOffset.Yaw)) <= Tolerance
		&& FMath::Abs(CurrentAimOffset.Pitch - DesiredOffset.Pitch) <= Tolerance;
}
bool AAutoTurret::UpdateAttackLocation(const FVector& TargetLocation)
{
	if (!HasAuthority() || !IsCombatActive() || IsAIControlSuppressed()
		|| !IsValid(CurrentWeapon) || !TurretHeadPivot)
	{
		return false;
	}

	CurrentAimLocation = TargetLocation;
	// 공격 요청이 들어와도 목표 좌표로 즉시 회전하지 않고, Task가 계산한 실제 머리 방향으로 발사한다.
	if (AController* ActiveController = GetController())
	{
		ActiveController->SetControlRotation(GetViewRotation());
		return true;
	}
	return false;
}

void AAutoTurret::ApplyExplosionReaction(const FVector& ExplosionOrigin, float EnemyImpulseScale,
	float TurretReactionScale, float EffectRatio)
{
	(void)EnemyImpulseScale;
	if (!bEnableImpactReaction || !HasAuthority() || !IsCombatActive()
		|| EffectRatio <= 0.0f || GetCurrentHealth() <= 0.0f)
	{
		return;
	}

	const FVector Direction = (GetActorLocation() - ExplosionOrigin).GetSafeNormal();
	const float ReactionAlpha = FMath::Clamp(FMath::Max(TurretReactionScale, 0.0f) * EffectRatio, 0.0f, 1.0f);
	const float Side = FVector::DotProduct(Direction, GetActorRightVector());
	const float Forward = FVector::DotProduct(Direction, GetActorForwardVector());
	const float Vertical = FVector::DotProduct(Direction, GetActorUpVector());
	// 정면과 후면의 폭발도 눈에 보이는 상하 반동이 되도록 변환한다.
	// 높이 차이가 더 크면 수직 충격을 우선하여 서로 상쇄되는 현상을 막는다.
	const float PitchDirection = FMath::Abs(Vertical) > FMath::Abs(Forward)
		? Vertical
		: -Forward;
	const FRotator PreviousImpactOffset = ImpactRotationOffset;
	ImpactRotationOffset.Yaw = FMath::Clamp(
		ImpactRotationOffset.Yaw + Side * RuntimeTurretBehavior.MaxImpactYawDegrees * ReactionAlpha,
		-RuntimeTurretBehavior.MaxImpactYawDegrees, RuntimeTurretBehavior.MaxImpactYawDegrees);
	ImpactRotationOffset.Pitch = FMath::Clamp(
		ImpactRotationOffset.Pitch + PitchDirection * RuntimeTurretBehavior.MaxImpactPitchDegrees * ReactionAlpha,
		-RuntimeTurretBehavior.MaxImpactPitchDegrees, RuntimeTurretBehavior.MaxImpactPitchDegrees);
	const FRotator AppliedImpactDelta = (ImpactRotationOffset - PreviousImpactOffset).GetNormalized();
	if (!AppliedImpactDelta.IsNearlyZero(0.01f) && IsTurretDiagnosticsEnabled())
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[TurretDiag][Impact] ReactionApplied Turret=%s Strength=%.3f Delta=%s Offset=%s Mode=%s"),
			*GetNameSafe(this),
			ReactionAlpha,
			*AppliedImpactDelta.ToString(),
			*ImpactRotationOffset.ToString(),
			ImpactReactionMode == EAutoTurretImpactReactionMode::StateTreeInterrupt
				? TEXT("StateTreeInterrupt") : TEXT("ConcurrentRecovery"));
	}

	ApplyHeadRotation();
	if (ImpactReactionMode == EAutoTurretImpactReactionMode::StateTreeInterrupt)
	{
		StopImpactRecovery();
		SendEnemyStateTreeEvent(FGameplayTag::RequestGameplayTag(
			TEXT("Enemy.Event.Status.ImpactStarted")));
	}
	else
	{
		StartImpactRecovery();
	}
	MulticastExplosionReaction(Direction, ReactionAlpha);
}

void AAutoTurret::StartImpactRecovery()
{
	if (!HasAuthority())
	{
		return;
	}

	// 연속 폭발을 받으면 마지막 충격 방향에서 유지 시간을 다시 시작한다.
	ImpactRecoveryHoldRemaining = FMath::Max(RuntimeTurretBehavior.ImpactHoldDuration, 0.0f);
	LastImpactRecoveryUpdateTimeSeconds = GetWorld()->GetTimeSeconds();
	if (GetWorldTimerManager().IsTimerActive(ImpactRecoveryTimerHandle))
	{
		return;
	}

	GetWorldTimerManager().SetTimer(
		ImpactRecoveryTimerHandle,
		this,
		&AAutoTurret::TickImpactRecovery,
		1.0f / 60.0f,
		true);
}

void AAutoTurret::TickImpactRecovery()
{
	if (!HasAuthority() || GetCurrentHealth() <= 0.0f)
	{
		StopImpactRecovery();
		return;
	}

	const double CurrentTimeSeconds = GetWorld()->GetTimeSeconds();
	const float DeltaTime = static_cast<float>(FMath::Max(
		CurrentTimeSeconds - LastImpactRecoveryUpdateTimeSeconds,
		0.0));
	LastImpactRecoveryUpdateTimeSeconds = CurrentTimeSeconds;
	if (ImpactRecoveryHoldRemaining > 0.0f)
	{
		// Hold 중에도 ApplyHeadRotation이 ControlRotation을 갱신하므로 실제 탄환이 휘청인 방향으로 발사된다.
		ImpactRecoveryHoldRemaining = FMath::Max(ImpactRecoveryHoldRemaining - DeltaTime, 0.0f);
		ApplyHeadRotation();
		return;
	}
	if (UpdateTurretImpactRecovery(DeltaTime))
	{
		StopImpactRecovery();
	}
}

void AAutoTurret::StopImpactRecovery()
{
	GetWorldTimerManager().ClearTimer(ImpactRecoveryTimerHandle);
	LastImpactRecoveryUpdateTimeSeconds = 0.0;
	ImpactRecoveryHoldRemaining = 0.0f;
}

bool AAutoTurret::UpdateTurretImpactRecovery(float DeltaTime)
{
	const float RecoverySpeed = FMath::Max(RuntimeTurretBehavior.ImpactRecoverySpeedDegrees, 0.0f);
	if (RecoverySpeed <= KINDA_SMALL_NUMBER)
	{
		ResetTurretImpactRecovery();
		return true;
	}

	ImpactRotationOffset.Pitch = FMath::FInterpConstantTo(ImpactRotationOffset.Pitch, 0.0f, DeltaTime, RecoverySpeed);
	ImpactRotationOffset.Yaw = FMath::FInterpConstantTo(ImpactRotationOffset.Yaw, 0.0f, DeltaTime, RecoverySpeed);
	ImpactRotationOffset.Roll = 0.0f;
	if (ImpactRotationOffset.IsNearlyZero(0.01f))
	{
		ResetTurretImpactRecovery();
		return true;
	}
	ApplyHeadRotation();
	return false;
}

void AAutoTurret::ResetTurretImpactRecovery()
{
	ImpactRotationOffset = FRotator::ZeroRotator;
	ApplyHeadRotation();
}

void AAutoTurret::ApplyHeadRotation()
{
	if (!TurretHeadPivot || !TurretHeadPitchPivot)
	{
		return;
	}

	const FRotator LocalAimRotation = GetLocalHeadRotation();
	// Actor 회전에 로컬 조준 회전을 Quaternion으로 합성해 Yaw/Pitch가 월드 Roll로 뒤집히지 않게 한다.
	const FQuat YawRotation = FRotator(0.0f, LocalAimRotation.Yaw, 0.0f).Quaternion();
	TurretHeadPivot->SetWorldRotation(GetActorQuat() * YawRotation);
	// Mesh의 X 90도는 FBX 축 보정값으로 유지하고, 상하 조준은 전용 Pivot에 Pitch로만 적용한다.
	const float VisualPitch = LocalAimRotation.Pitch * HeadVisualPitchDirection;
	TurretHeadPitchPivot->SetRelativeRotation(FRotator(VisualPitch, 0.0f, 0.0f));
	if (AController* ActiveController = GetController())
	{
		// 실제 Hitscan 방향도 머리의 반동과 복구를 그대로 따라가도록 ControlRotation을 맞춘다.
		ActiveController->SetControlRotation(GetViewRotation());
	}
}

void AAutoTurret::HandleHackStarted(const FHackQueryContext& Context)
{
	(void)Context;
	if (!HasAuthority() || !IsCombatActive() || bHackedToPlayerTeam)
	{
		return;
	}
	if (CurrentWeapon && GetAttackPhase() == EEnemyAttackPhase::Firing)
	{
		CurrentWeapon->ForcePostBurstCooldown();
	}
	StopCurrentAttack();
	EnterStun();
}

void AAutoTurret::HandleHackEffect(FGameplayTag EffectTag, const FHackResultContext& Context)
{
	if (!HasAuthority() || EffectTag != HackGameplayTags::Effect::ChangeTeam()
		|| Context.Result != EHackResult::Success || !IsCombatActive())
	{
		return;
	}

	if (bHackedToPlayerTeam) return;
	bHackedToPlayerTeam = true;
	ApplyHackedTeamState();
	// 팀 전환이 적용된 뒤에만 시간을 시작한다. 시작이 거부되면 전환도 확정하지 않는다.
	if (!BeginHackEffect())
	{
		bHackedToPlayerTeam = false;
		ApplyHackedTeamState();
		return;
	}
	OutlierAudioFeedback::World(this, *HackedAudioContext.ToString(), TEXT("Audio.Type.Enemy"));
	if (HackableComponent)
	{
		HackableComponent->MarkAsHackedOnce();
	}
	if (URoomCombatSubsystem* Combat = GetWorld()->GetSubsystem<URoomCombatSubsystem>())
	{
		Combat->NotifyEnemyAllied(this);
	}
}

void AAutoTurret::HandleHackCompleted(const FHackResultContext& Context)
{
	(void)Context;
	if (!HasAuthority() || !IsCombatActive())
	{
		return;
	}
	if (GetCombatState() == EEnemyCombatState::Stun)
	{
		RestoreStateAfterStun();
	}
}

void AAutoTurret::RefreshTeammateOutline()
{
	if (UOutlierStencilSubsystem* Stencils = GetWorld() ? GetWorld()->GetSubsystem<UOutlierStencilSubsystem>() : nullptr)
	{
		Stencils->SetActorTeammate(this, bHackedToPlayerTeam && !IsActorBeingDestroyed());
	}
}

void AAutoTurret::ApplyHackedTeamState()
{
	// Controller가 아직 없거나 교체 중이어도 이전 팀의 단서·예약 회차는 이월하지 않는다.
	ClearAlertClue();
	ClearHitClues();
	RefreshTeammateOutline();
	StopCurrentAttack();
	SetPlayerCurrentlyVisible(false);
	RemoveRoomTargetObserver();
	ClearSharedTargetContact();
	ReleaseSearchDestinations();
	if (AEnemyAIController* EnemyController = Cast<AEnemyAIController>(GetController()))
	{
		EnemyController->RefreshTeamAndPerceptionFromPawn();
	}

	if (HasAuthority())
	{
		// 팀이 바뀐 액터를 기존 우호 대상으로 기억한 AI도 즉시 적대 대상으로 다시 평가한다.
		if (UEnemyRoomSubsystem* RoomSubsystem = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
		{
			RoomSubsystem->RefreshDetectionTarget(this);
		}
	}
	ForceNetUpdate();
}

void AAutoTurret::OnRep_HackedTeam()
{
	ApplyHackedTeamState();
}

void AAutoTurret::HandleEMPStarted(FGameplayTag EffectTag)
{
	if (IsCombatActive())
	{
		Super::HandleEMPStarted(EffectTag);
	}
}

void AAutoTurret::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		if (bWaveTurretRegistered)
		{
			if (URoomCombatSubsystem* CombatSubsystem = GetWorld()
				? GetWorld()->GetSubsystem<URoomCombatSubsystem>()
				: nullptr)
			{
				CombatSubsystem->UnregisterWaveTurret(this);
			}
			bWaveTurretRegistered = false;
		}

		if (bPersistentTurretIdRegistered)
		{
			UGameInstance* GameInstance = GetGameInstance();
			if (UOutlierSaveSubSystem* SaveSubsystem = GameInstance
				? GameInstance->GetSubsystem<UOutlierSaveSubSystem>()
				: nullptr)
			{
				SaveSubsystem->UnregisterPersistentTurretId(PersistentTurretId, this);
			}
			bPersistentTurretIdRegistered = false;
		}
	}

	StopImpactRecovery();
	if (UOutlierStencilSubsystem* Stencils = GetWorld() ? GetWorld()->GetSubsystem<UOutlierStencilSubsystem>() : nullptr)
	{
		Stencils->SetActorTeammate(this, false);
	}
	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
EDataValidationResult AAutoTurret::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (IsTemplate())
	{
		return Result == EDataValidationResult::NotValidated
			? EDataValidationResult::Valid
			: Result;
	}
	if (!UOutlierArenaSubsystem::ValidateGameplayActorPlacement(this, Context))
	{
		Result = EDataValidationResult::Invalid;
	}

	auto AddValidationError = [&Context, &Result](const FString& Message)
	{
		Context.AddError(FText::FromString(Message));
		Result = EDataValidationResult::Invalid;
	};

	const FGameplayTag RoomTag = GetDefaultRoomTag();
	if (!RoomTag.IsValid())
	{
		AddValidationError(TEXT("A wave AutoTurret requires a valid DefaultRoomTag."));
	}
	if (CombatPhaseIndex < 0 || WaveIndex < 0)
	{
		AddValidationError(TEXT("A wave AutoTurret requires non-negative phase and Wave indices."));
	}
	if (PersistentTurretId.IsNone())
	{
		AddValidationError(TEXT("A wave AutoTurret requires a PersistentTurretId."));
	}
	else if (UWorld* World = GetWorld())
	{
		// 에디터 검증에서 현재 로드된 터렛끼리의 중복을 먼저 보여준다.
		// 서브레벨 전환 중의 런타임 중복은 SaveSubsystem의 전역 등록부가 다시 막는다.
		for (TActorIterator<AAutoTurret> It(World); It; ++It)
		{
			const AAutoTurret* OtherTurret = *It;
			if (OtherTurret != this
				&& OtherTurret->PersistentTurretId == PersistentTurretId)
			{
				AddValidationError(FString::Printf(
					TEXT("A wave AutoTurret has a duplicate PersistentTurretId '%s' with %s."),
					*PersistentTurretId.ToString(), *GetNameSafe(OtherTurret)));
				break;
			}
		}
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
		AddValidationError(TEXT("A wave AutoTurret requires the integrated RoomCombatDefinition setting."));
	}
	else if (!RoomDefinition)
	{
		AddValidationError(FString::Printf(
			TEXT("A wave AutoTurret has no RoomCombatDefinition entry for %s."),
			*RoomTag.ToString()));
	}
	else if (!Wave)
	{
		AddValidationError(TEXT("A wave AutoTurret points to an invalid combat phase or Wave."));
	}
	else if (!Wave->IsSpawnFromObjects() || !Wave->HasWaveTurrets())
	{
		AddValidationError(TEXT("A wave AutoTurret must target a SpawnFromObjects Wave with ExpectedWaveTurretCount."));
	}

	return Result == EDataValidationResult::NotValidated
		? EDataValidationResult::Valid
		: Result;
}
#endif

void AAutoTurret::HandleDeath()
{
	if (TurretLifecycleState == EAutoTurretLifecycleState::DeadPresentation
		|| TurretLifecycleState == EAutoTurretLifecycleState::DeadPersistent)
	{
		return;
	}

	StopImpactRecovery();
	SetTurretLifecycleState(EAutoTurretLifecycleState::DeadPresentation);
	bRoomWaveDeploymentRequested = false;
	MulticastPlayDeathMontage(RoomWaveGameplayGeneration, RoomWaveActivationSerial);
	if (IsTurretDiagnosticsEnabled())
	{
		UE_LOG(LogOutlier, Display,
			TEXT("[TurretDiag][Death] PresentationStarted Turret=%s Generation=%d Serial=%d State=%d"),
			*GetNameSafe(this), RoomWaveGameplayGeneration, RoomWaveActivationSerial,
			static_cast<int32>(TurretLifecycleState));
	}

	// Room 정리 중 차수 완료가 이어져도 사망 상태가 Snapshot 원본에 먼저 남도록 기록 순서를 고정한다.
	if (bPersistentTurretIdRegistered)
	{
		UGameInstance* GameInstance = GetGameInstance();
		if (UOutlierSaveSubSystem* SaveSubsystem = GameInstance
			? GameInstance->GetSubsystem<UOutlierSaveSubSystem>()
			: nullptr)
		{
			SaveSubsystem->SetDestroyedTurretState(PersistentTurretId, true);
		}
	}

	// 공통 정리는 Room 생존 집계와 AI를 즉시 제거한다. 부모 HandleDeath의 Destroy 단계만 건너뛴다.
	PerformDeathCleanup();
}

float AAutoTurret::GetDeathDestroyDelay() const
{
	// 사망 몽타주를 끝까지 재생하되, 몽타주가 없거나 아주 짧아도 베이스의 최소 지연은 지킨다.
	// 그래야 사망 GameplayCue 멀티캐스트가 액터 채널이 닫히기 전에 전송된다.
	const float MontageLength = DeathMontage ? DeathMontage->GetPlayLength() : 0.0f;
	return FMath::Max(MontageLength, Super::GetDeathDestroyDelay());
	// 기획이 터렛 죽으면 메시 안사라지고 Death 애니메이션 이후 엄폐물로 변경돼서 Destroy안할듯
}

void AAutoTurret::ApplyClassStatOverrides()
{
	SetDefaultEnemyType(EEnemyType::Turret);
	RuntimeStat.MoveSpeed = 0.0f;
}

void AAutoTurret::ApplyMovementFromRuntimeStat()
{
	if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
	{
		MovementComponent->DisableMovement();
		MovementComponent->MaxFlySpeed = 0.0f;
	}
}
