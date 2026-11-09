#include "Enemy/SelfDestructDrone.h"
#include "Audio/OutlierAudioFeedback.h"
#include "Audio/OutlierAudioSubsystem.h"
#include "Engine/GameInstance.h"
#include "GameFramework/GameStateBase.h"
#include "TimerManager.h"

#include "Components/PrimitiveComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/VECDroneMovementComponent.h"
#include "Drone/Partner/HackableComponent.h"
#include "GameFramework/Character.h"
#include "Explosion/ExplosionComponent.h"
#include "Explosion/ExplosiveProp.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Outlier.h"

DEFINE_LOG_CATEGORY_STATIC(LogEnemyChargeCollision, Log, All);

namespace
{
TAutoConsoleVariable<int32> CVarEnemyChargeDiagnostics(TEXT("outlier.Enemy.ChargeDiagnostics"), 1,
	TEXT("Log self-destruct charge lifecycle, collision and replication diagnostics. 0 disables diagnostics."));
TAutoConsoleVariable<float> CVarEnemyChargeDiagnosticInterval(TEXT("outlier.Enemy.ChargeDiagnosticInterval"), 0.25f,
	TEXT("Charge diagnostic sample interval in seconds (minimum 0.05)."));

void CollectMatchingExplosiveSockets(
	const USkeletalMeshComponent* Mesh,
	FName SocketPattern,
	TArray<FName>& OutSocketNames)
{
	if (!Mesh || SocketPattern.IsNone())
	{
		return;
	}

	const FString Pattern = SocketPattern.ToString();
	for (const FName SocketName : Mesh->GetAllSocketNames())
	{
		const FString Name = SocketName.ToString();
		if (Name.Equals(Pattern)
			|| Name.StartsWith(Pattern)
			|| Name.EndsWith(Pattern))
		{
			OutSocketNames.AddUnique(SocketName);
		}
	}

	OutSocketNames.Sort(
		[](const FName& Left, const FName& Right)
		{
			return Left.LexicalLess(Right);
		});
}
}

ASelfDestructDrone::ASelfDestructDrone()
{
	SightBones = { TEXT("RootBone"), TEXT("CoreBone"), TEXT("BodyBone") };
	SetDefaultEnemyType(EEnemyType::Melee);
	HitAudioContext = OutlierAudioFeedback::Tag(TEXT("Audio.Context.Enemy.Drone.Melee.State.Hit"));
	DeathAudioContext = OutlierAudioFeedback::Tag(TEXT("Audio.Context.Enemy.Drone.Melee.State.Death"));
	DetectAudioContext = OutlierAudioFeedback::Tag(TEXT("Audio.Context.Enemy.Drone.Melee.State.Detect"));
	HoverAudioContext = OutlierAudioFeedback::Tag(TEXT("Audio.Context.Enemy.Drone.Melee.Hovering.Loop"));
	AppearAudioContext = OutlierAudioFeedback::Tag(TEXT("Audio.Context.Enemy.Drone.Melee.State.Appear"));
	CombatLoopAudioContext = OutlierAudioFeedback::Tag(TEXT("Audio.Context.Enemy.Drone.Melee.State.Combat.Loop"));
	TelegraphAudioContext = OutlierAudioFeedback::Tag(TEXT("Audio.Context.Enemy.Drone.Melee.State.Exlode.sign"));
	SelfDestructAudioContext = OutlierAudioFeedback::Tag(TEXT("Audio.Context.Enemy.Drone.Melee.State.Exlode.Act"));


	ExplosionComponent = CreateDefaultSubobject<UExplosionComponent>(TEXT("ExplosionComponent"));
}

void ASelfDestructDrone::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASelfDestructDrone, MountedExplosives);
	DOREPLIFETIME(ASelfDestructDrone, ChargeState);
}

float ASelfDestructDrone::GetWeakPointDamageMultiplier(const UPrimitiveComponent* HitComponent) const
{
	const AExplosiveProp* HitExplosive = HitComponent
		? Cast<AExplosiveProp>(HitComponent->GetOwner())
		: nullptr;
	if (HitExplosive && HitExplosive->GetOwner() == this)
	{
		// 별도 Actor의 HitCollision 피격도 자폭 드론 본체 HP에 크리티컬 피해로 적용한다.
		return FMath::Max(GetRuntimeStat().ExplosiveWeakPointMultiplier, 1.0f);
	}

	return Super::GetWeakPointDamageMultiplier(HitComponent);
}

void ASelfDestructDrone::LogChargeDiagnostics(const TCHAR* Event) const
{
	if (UE_BUILD_SHIPPING || CVarEnemyChargeDiagnostics.GetValueOnGameThread() == 0) return;
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	const double LocalTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const AGameStateBase* Clock = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	UE_LOG(LogEnemyChargeCollision, Log, TEXT("[ChargeDiag] Event=%s Drone=%s NetMode=%d Role=%d Authority=%d Time=%.3f ServerTime=%.3f Generation=%d Lease=%d Revision=%u Pool=%d Active=%d Dead=%d DeathHandling=%d Possessed=%d Location=%s Velocity=%s MeshLocation=%s MeshRelative=%s RepLocation=%s Controller=%s Hidden=%d Replicates=%d RepMovement=%d AlwaysRelevant=%d OwnerRelevancy=%d Dormancy=%d"),
		Event, *GetName(), static_cast<int32>(GetNetMode()), static_cast<int32>(GetLocalRole()), HasAuthority(),
		LocalTime, Clock ? Clock->GetServerWorldTimeSeconds() : LocalTime,
		GetPoolGameplayGeneration(), GetPoolLeaseSerial(), ChargeState.Revision, static_cast<int32>(GetEnemyPoolState()),
		ChargeState.bActive, IsDead(), bDeathHandling, IsEnemyPossessed(), *GetActorLocation().ToCompactString(),
		*GetVelocity().ToCompactString(), GetMesh() ? *GetMesh()->GetComponentLocation().ToCompactString() : TEXT("None"),
		GetMesh() ? *GetMesh()->GetRelativeLocation().ToCompactString() : TEXT("None"),
		*GetReplicatedMovement().Location.ToCompactString(), *GetNameSafe(GetController()), IsHidden(),
		GetIsReplicated(), IsReplicatingMovement(), bAlwaysRelevant, bNetUseOwnerRelevancy, static_cast<int32>(NetDormancy));
	UE_LOG(LogEnemyChargeCollision, Log, TEXT("[ChargeCollisionState] Event=%s Drone=%s Revision=%u ActorCollision=%d Capsule=%s Root=%s UpdatedComponent=%s Profile=%s Enabled=%d ObjectType=%d WorldStatic=%d WorldDynamic=%d Pawn=%d PhysicsBody=%d OverlapEvents=%d Radius=%.2f HalfHeight=%.2f Mode=%d MovementActive=%d Smoothing=%d MaxFlySpeed=%.1f Direction=%s ImpactVelocity=%s Elapsed=%.3f Duration=%.3f Exploded=%d"),
		Event, *GetName(), ChargeState.Revision, GetActorEnableCollision(), *GetNameSafe(Capsule), *GetNameSafe(GetRootComponent()),
		*GetNameSafe(Movement ? Movement->UpdatedComponent.Get() : nullptr),
		Capsule ? *Capsule->GetCollisionProfileName().ToString() : TEXT("None"),
		Capsule ? static_cast<int32>(Capsule->GetCollisionEnabled()) : -1,
		Capsule ? static_cast<int32>(Capsule->GetCollisionObjectType()) : -1,
		Capsule ? static_cast<int32>(Capsule->GetCollisionResponseToChannel(ECC_WorldStatic)) : -1,
		Capsule ? static_cast<int32>(Capsule->GetCollisionResponseToChannel(ECC_WorldDynamic)) : -1,
		Capsule ? static_cast<int32>(Capsule->GetCollisionResponseToChannel(ECC_Pawn)) : -1,
		Capsule ? static_cast<int32>(Capsule->GetCollisionResponseToChannel(ECC_PhysicsBody)) : -1,
		Capsule && Capsule->GetGenerateOverlapEvents(), Capsule ? Capsule->GetScaledCapsuleRadius() : 0.0f,
		Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 0.0f, Movement ? static_cast<int32>(Movement->MovementMode) : -1,
		Movement && Movement->IsActive(), Movement ? static_cast<int32>(Movement->NetworkSmoothingMode) : -1,
		Movement ? Movement->MaxFlySpeed : 0.0f, *CommittedChargeDirection.ToCompactString(),
		*AccumulatedImpactVelocity.ToCompactString(), GetCommittedChargeElapsedTime(), ChargeState.Playback.Duration,
		ExplosionComponent && ExplosionComponent->HasDetonated());
	if (Capsule && FCString::Strcmp(Event, TEXT("Sample")) != 0)
	{
		FString Ignored;
		for (const AActor* Actor : Capsule->GetMoveIgnoreActors()) Ignored += GetNameSafe(Actor) + TEXT(" ");
		for (const UPrimitiveComponent* Component : Capsule->GetMoveIgnoreComponents()) Ignored += GetNameSafe(Component) + TEXT(" ");
		UE_LOG(LogEnemyChargeCollision, Log, TEXT("[ChargeMoveIgnores] Event=%s Drone=%s Revision=%u Ignored=%s"),
			Event, *GetName(), ChargeState.Revision, *Ignored);
	}
}

void ASelfDestructDrone::RefreshChargeDiagnosticTimer()
{
	if (!GetWorld()) return;
	GetWorldTimerManager().ClearTimer(ChargeDiagnosticTimer);
	if (UE_BUILD_SHIPPING || CVarEnemyChargeDiagnostics.GetValueOnGameThread() == 0
		|| !ChargeState.bActive || IsDead()
		|| (IsPoolManaged() && GetEnemyPoolState() != EEnemyPoolState::CombatActive)) return;
	PreviousChargeDiagnosticLocation = GetActorLocation();
	PreviousChargeDiagnosticTime = GetWorld()->GetTimeSeconds();
	LastChargeHitDiagnosticTime = -1.0;
	GetWorldTimerManager().SetTimer(ChargeDiagnosticTimer, this, &ThisClass::SampleChargeDiagnostics,
		FMath::Max(CVarEnemyChargeDiagnosticInterval.GetValueOnGameThread(), 0.05f), true);
}

void ASelfDestructDrone::SampleChargeDiagnostics()
{
	if (!ChargeState.bActive || IsDead() || CVarEnemyChargeDiagnostics.GetValueOnGameThread() == 0
		|| (IsPoolManaged() && GetEnemyPoolState() != EEnemyPoolState::CombatActive))
	{
		GetWorldTimerManager().ClearTimer(ChargeDiagnosticTimer);
		return;
	}
	LogChargeDiagnostics(TEXT("Sample"));
	const FVector Location = GetActorLocation();
	const double Now = GetWorld()->GetTimeSeconds();
	const double Elapsed = Now - PreviousChargeDiagnosticTime;
	UE_LOG(LogEnemyChargeCollision, Log, TEXT("[ChargeDisplacement] Drone=%s Authority=%d Revision=%u DeltaSeconds=%.3f From=%s To=%s Distance=%.2f ObservedSpeed=%.1f"),
		*GetName(), HasAuthority(), ChargeState.Revision, Elapsed, *PreviousChargeDiagnosticLocation.ToCompactString(),
		*Location.ToCompactString(), FVector::Dist(Location, PreviousChargeDiagnosticLocation),
		Elapsed > UE_DOUBLE_SMALL_NUMBER ? FVector::Dist(Location, PreviousChargeDiagnosticLocation) / Elapsed : 0.0);
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (Capsule && Capsule->IsQueryCollisionEnabled() && GetWorld()->GetPhysicsScene())
	{
		// 현재 채널/무시 목록 그대로 관찰 구간을 조회한다. 이동·충돌 결과를 수정하지 않는다.
		// 두 샘플 사이 직선이므로 곡선 이동/움직이는 장애물에서는 관통의 확정 증거가 아니다.
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ChargeDiagnostics), false, this);
		Params.AddIgnoredActors(Capsule->GetMoveIgnoreActors());
		Params.AddIgnoredComponents(Capsule->GetMoveIgnoreComponents());
		FHitResult Hit;
		if (GetWorld()->SweepSingleByChannel(Hit, PreviousChargeDiagnosticLocation, Location, Capsule->GetComponentQuat(),
			Capsule->GetCollisionObjectType(), FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()),
			Params, FCollisionResponseParams(Capsule->GetCollisionResponseToChannels())))
		{
			const UPrimitiveComponent* Other = Hit.GetComponent();
			UE_LOG(LogEnemyChargeCollision, Log, TEXT("[ChargeObservedPathBlocked] Drone=%s Authority=%d Revision=%u Other=%s Component=%s HitTime=%.3f StartPenetrating=%d Depth=%.2f Impact=%s Normal=%s OtherEnabled=%d OtherObjectType=%d CapsuleResponse=%d OtherResponse=%d"),
				*GetName(), HasAuthority(), ChargeState.Revision, *GetNameSafe(Hit.GetActor()), *GetNameSafe(Other),
				Hit.Time, Hit.bStartPenetrating, Hit.PenetrationDepth, *Hit.ImpactPoint.ToCompactString(), *Hit.ImpactNormal.ToCompactString(),
				Other ? static_cast<int32>(Other->GetCollisionEnabled()) : -1, Other ? static_cast<int32>(Other->GetCollisionObjectType()) : -1,
				Other ? static_cast<int32>(Capsule->GetCollisionResponseToChannel(Other->GetCollisionObjectType())) : -1,
				Other ? static_cast<int32>(Other->GetCollisionResponseToChannel(Capsule->GetCollisionObjectType())) : -1);
		}
	}
	PreviousChargeDiagnosticLocation = Location;
	PreviousChargeDiagnosticTime = Now;
}

void ASelfDestructDrone::BeginPlay()
{
	Super::BeginPlay();
	ChargeClock = GetWorld()->GetGameState();
	if (GetNetMode() != NM_DedicatedServer && GetGameInstance())
	{
		ChargeAudio = GetGameInstance()->GetSubsystem<UOutlierAudioSubsystem>();
	}
	RefreshChargePresentation();

	if (HasAuthority())
	{
		if (!IsPoolManaged())
		{
			SpawnAndAttachMountedExplosives();
		}
	}
	LogChargeDiagnostics(TEXT("BeginPlay"));
	RefreshChargeDiagnosticTimer();
}

void ASelfDestructDrone::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (!UE_BUILD_SHIPPING && CVarEnemyChargeDiagnostics.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogEnemyChargeCollision, Log, TEXT("[ChargeEndPlay] Drone=%s Authority=%d Revision=%u Active=%d Reason=%d"),
			*GetName(), HasAuthority(), ChargeState.Revision, ChargeState.bActive, static_cast<int32>(EndPlayReason));
	}
	LogChargeDiagnostics(TEXT("EndPlay"));
	GetWorldTimerManager().ClearTimer(ChargeDiagnosticTimer);
	StopChargePresentation();
	DestroyMountedExplosives();
	Super::EndPlay(EndPlayReason);
}

void ASelfDestructDrone::SpawnAndAttachMountedExplosives()
{
	if (!HasAuthority() || !MountedExplosiveClass || !MountedExplosives.IsEmpty() || !GetMesh())
	{
		return;
	}

	TArray<FName> MatchingSockets;
	CollectMatchingExplosiveSockets(
		GetMesh(),
		MountedExplosiveSocketPattern,
		MatchingSockets);
	if (!MountedExplosiveSocketName.IsNone())
	{
		MatchingSockets.Reset();
		if (GetMesh()->DoesSocketExist(MountedExplosiveSocketName))
		{
			MatchingSockets.Add(MountedExplosiveSocketName);
		}
	}
	if (MatchingSockets.IsEmpty())
	{
		UE_LOG(
			LogOutlier,
			Error,
			TEXT("[SelfDestructDrone] No mounted explosive sockets matched. Actor=%s Pattern=%s"),
			*GetNameSafe(this),
			*MountedExplosiveSocketPattern.ToString());
		return;
	}
	if (MatchingSockets.Num() != 1)
	{
		UE_LOG(LogOutlier, Error, TEXT("[SelfDestructDrone] Specify one MountedExplosiveSocketName. Actor=%s Matches=%d"),
			*GetName(), MatchingSockets.Num());
		return;
	}

	const FName SocketName = MatchingSockets[0];
	AExplosiveProp* MountedExplosive = GetWorld()->SpawnActorDeferred<AExplosiveProp>(
		MountedExplosiveClass,
		GetMesh()->GetSocketTransform(SocketName),
		this,
		this,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!MountedExplosive)
	{
		UE_LOG(LogOutlier, Error, TEXT("[SelfDestructDrone] Failed to spawn mounted explosive Actor=%s"), *GetName());
		return;
	}
	MountedExplosive->InitializeMountedSocket(SocketName);
	MountedExplosive->FinishSpawning(GetMesh()->GetSocketTransform(SocketName));
	MountedExplosives.Add(MountedExplosive);
	ForceNetUpdate();
}

void ASelfDestructDrone::DestroyMountedExplosives()
{
	if (HasAuthority())
	{
		for (AExplosiveProp* MountedExplosive : MountedExplosives)
		{
			if (IsValid(MountedExplosive))
			{
				MountedExplosive->Destroy();
			}
		}
	}
	MountedExplosives.Reset();
}

void ASelfDestructDrone::TriggerSelfDestruct()
{
	if (!HasAuthority() || bDeathHandling)
	{
		LogChargeDiagnostics(TEXT("DetonateRejected"));
		return;
	}
	LogChargeDiagnostics(TEXT("DetonateRequested"));
	UE_LOG(LogEnemyChargeCollision, Log, TEXT("[Charge] DetonateRequested Drone=%s Committed=%d Health=%.1f"),
		*GetNameSafe(this), ChargeState.bActive, GetCurrentHealth());

	bTriggeredSelfDestruct = true;
	ApplyDamageInternal(
		FMath::Max(GetCurrentHealth(), 1.0f),
		GetController(),
		this,
		OutlierGameplayTags::Damage::Explosion());
	if (!IsDead()) bTriggeredSelfDestruct = false;
	LogChargeDiagnostics(TEXT("DetonateDamageResult"));
}

void ASelfDestructDrone::NotifyHit(UPrimitiveComponent* MyComp, AActor* Other,
	UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation, FVector HitNormal,
	FVector NormalImpulse, const FHitResult& Hit)
{
	Super::NotifyHit(MyComp, Other, OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);
	// 거절된 충돌도 남겨야 메시 접촉/비차단/클라이언트 접촉과 서버 캡슐 충돌을 구분할 수 있다.
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (ChargeState.bActive && CVarEnemyChargeDiagnostics.GetValueOnGameThread() != 0 && !UE_BUILD_SHIPPING
		&& (LastChargeHitDiagnosticTime < 0.0 || Now - LastChargeHitDiagnosticTime >= 0.25 || Cast<ACharacter>(Other)))
	{
		LastChargeHitDiagnosticTime = Now;
		UE_LOG(LogEnemyChargeCollision, Log, TEXT("[ChargeHit] Drone=%s Authority=%d Revision=%u MyComp=%s IsCapsule=%d Other=%s OtherComp=%s Character=%d Blocking=%d SelfMoved=%d InitialOverlap=%d HitTime=%.3f Location=%s Normal=%s OtherEnabled=%d OtherObjectType=%d MyResponse=%d OtherResponse=%d Dead=%d DeathHandling=%d"),
			*GetName(), HasAuthority(), ChargeState.Revision, *GetNameSafe(MyComp), MyComp == GetCapsuleComponent(),
			*GetNameSafe(Other), *GetNameSafe(OtherComp), Cast<ACharacter>(Other) != nullptr, Hit.bBlockingHit, bSelfMoved,
			Hit.bStartPenetrating, Hit.Time, *HitLocation.ToCompactString(), *HitNormal.ToCompactString(),
			OtherComp ? static_cast<int32>(OtherComp->GetCollisionEnabled()) : -1,
			OtherComp ? static_cast<int32>(OtherComp->GetCollisionObjectType()) : -1,
			MyComp && OtherComp ? static_cast<int32>(MyComp->GetCollisionResponseToChannel(OtherComp->GetCollisionObjectType())) : -1,
			MyComp && OtherComp ? static_cast<int32>(OtherComp->GetCollisionResponseToChannel(MyComp->GetCollisionObjectType())) : -1,
			IsDead(), bDeathHandling);
	}
	// 1. 커밋된 본체의 실제 차단 충돌만 검사한다. 메시/부착물 접촉은 제외한다.
	if (!HasAuthority() || !ChargeState.bActive || bDeathHandling || IsDead()
		|| MyComp != GetCapsuleComponent() || !Hit.bBlockingHit
		|| !IsValid(Other) || Other == this || Other->GetOwner() == this || Other->IsAttachedTo(this))
	{
		return;
	}
	// 2. 돌진 중에는 진영/빙의 여부와 무관하게 캐릭터 접촉으로 폭발한다. Pawn 채널만으로 판단하지 않는다.
	// 벽 접촉은 물리 이동만 막는다. Commit과 시작 시각은 유지해 기존 StateTree 시간 만료로 폭발한다.
	if (!Cast<ACharacter>(Other)) return;
	UE_LOG(LogEnemyChargeCollision, Log, TEXT("[Charge] CollisionDetonate Drone=%s Other=%s Component=%s Location=%s"),
		*GetNameSafe(this), *GetNameSafe(Other), *GetNameSafe(OtherComp), *HitLocation.ToCompactString());
	TriggerSelfDestruct();
}

bool ASelfDestructDrone::BeginCommittedSelfDestruct(
	AActor* TargetActor,
	float TelegraphDuration,
	float ChargeSpeed)
{
	if (!IsValid(TargetActor))
	{
		LogChargeDiagnostics(TEXT("CommitTargetInvalid"));
		return false;
	}
	if (!UE_BUILD_SHIPPING && CVarEnemyChargeDiagnostics.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogEnemyChargeCollision, Log, TEXT("[ChargeTarget] Drone=%s Target=%s Location=%s TargetLocation=%s"),
			*GetName(), *GetNameSafe(TargetActor), *GetActorLocation().ToCompactString(), *TargetActor->GetActorLocation().ToCompactString());
	}

	const FVector ToTarget = TargetActor->GetActorLocation() - GetActorLocation();
	// AI는 목표 앞에서 멈추지 않는다. 충돌 또는 StateTree의 전조 만료까지 계속 돌진한다.
	return BeginCommittedSelfDestructInternal(
		ToTarget,
		TelegraphDuration,
		ChargeSpeed,
		false,
		0.0f);
}

bool ASelfDestructDrone::BeginCommittedSelfDestructDirection(
	const FVector& ChargeDirection,
	float TelegraphDuration,
	float ChargeSpeed,
	float MaxChargeDistance)
{
	return BeginCommittedSelfDestructInternal(
		ChargeDirection,
		TelegraphDuration,
		ChargeSpeed,
		true,
		FMath::Max(MaxChargeDistance, 0.0f));
}

bool ASelfDestructDrone::BeginCommittedSelfDestructInternal(
	const FVector& ChargeDirection,
	float TelegraphDuration,
	float ChargeSpeed,
	bool bLimitChargeDistance,
	float ChargeDistanceLimit)
{
	if (!HasAuthority()
		|| bDeathHandling
		|| IsDead()
		|| ChargeState.bActive)
	{
		LogChargeDiagnostics(TEXT("CommitRejected"));
		return false;
	}
	LogChargeDiagnostics(TEXT("BeforeCommit"));
	bChargeMovementDiagnosticStarted = false;

	CommittedChargeDirection = ChargeDirection.GetSafeNormal();
	if (CommittedChargeDirection.IsNearlyZero())
	{
		CommittedChargeDirection = GetActorForwardVector().GetSafeNormal();
	}
	CommittedChargeTargetDirection = CommittedChargeDirection;
	CommittedChargeStartLocation = GetActorLocation();
	ChargeSpeedState.Reset();
	CommittedChargeSpeed = FMath::Max(ChargeSpeed, 0.0f);
	bCommittedChargeDistanceLimited = bLimitChargeDistance;
	CommittedChargeDistanceLimit = FMath::Max(ChargeDistanceLimit, 0.0f);
	CommittedImpactElapsedTime = 0.0f;

	// 이동/해킹의 commit을 먼저 확정하고 아래에서 클라이언트 연출까지 게시한다.
	ChargeState.bActive = true;
	if (HackableComponent)
	{
		// 전조가 시작된 자폭은 더 이상 취소 가능한 해킹 대상으로 노출하지 않는다.
		HackableComponent->HackTags.AddTag(OutlierGameplayTags::State::Locked());
	}
	if (VECMovementComponent)
	{
		VECMovementComponent->ClearFlightInput();
	}
	StopCurrentAttack();
	ReleaseSearchDestinations();
	if (AEnemyAIController* EnemyAIController = Cast<AEnemyAIController>(GetController()))
	{
		EnemyAIController->StopMovement();
	}

	FRotator ChargeRotation = CommittedChargeDirection.Rotation();
	ChargeRotation.Roll = 0.0f;
	if (AController* ActiveController = GetController())
	{
		ActiveController->SetControlRotation(ChargeRotation);
	}
	SetActorRotation(FRotator(0.0f, ChargeRotation.Yaw, 0.0f));
	PublishChargeState(true, TelegraphDuration);
	UE_LOG(LogEnemyChargeCollision, Log, TEXT("[Charge] Commit Drone=%s Direction=%s Speed=%.1f DistanceLimited=%d Distance=%.1f Duration=%.2f"),
		*GetNameSafe(this), *CommittedChargeDirection.ToCompactString(), ChargeSpeed,
		bCommittedChargeDistanceLimited, ChargeDistanceLimit, TelegraphDuration);
	ForceNetUpdate();
	return true;
}

bool ASelfDestructDrone::UpdateCommittedSelfDestructMovement(
	float DeltaTime,
	float ChargeSpeed)
{
	if (!HasAuthority()
		|| !ChargeState.bActive
		|| bDeathHandling
		|| IsDead())
	{
		LogChargeDiagnostics(TEXT("MovementUpdateRejected"));
		return false;
	}

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement)
	{
		LogChargeDiagnostics(TEXT("MovementComponentMissing"));
		return false;
	}
	const bool bFirstMovementUpdate = !bChargeMovementDiagnosticStarted;
	if (bFirstMovementUpdate || Movement->MovementMode != MOVE_Flying)
	{
		LogChargeDiagnostics(TEXT("BeforeChargeMovement"));
	}
	if (Movement->MovementMode != MOVE_Flying)
	{
		Movement->SetMovementMode(MOVE_Flying);
	}

	CommittedChargeSpeed = FMath::Max(ChargeSpeed, 0.0f);
	CommittedChargeDirection = FMath::RInterpTo(
		CommittedChargeDirection.Rotation(),
		CommittedChargeTargetDirection.Rotation(),
		FMath::Max(DeltaTime, 0.0f),
		FMath::Max(RuntimeImpactReactionProfile.ChargeImpactTurnInterpSpeed, 0.0f)).Vector();

	CommittedImpactElapsedTime += FMath::Max(DeltaTime, 0.0f);
	if (CommittedImpactElapsedTime >= CurrentPhysicalKnockbackDuration)
	{
		AccumulatedImpactVelocity = FVector::ZeroVector;
	}
	else
	{
		AccumulatedImpactVelocity *= FMath::Exp(
			-FMath::Max(RuntimeImpactReactionProfile.KnockbackDamping, 0.0f)
			* FMath::Max(DeltaTime, 0.0f));
	}
	CurrentImpactStrength = AccumulatedImpactVelocity.Size();

	// AI는 가속만 적용하며 거리로 감속하지 않는다. 빙의 돌진의 거리 제한은 아래에서 별도로 적용한다.
	const float ProfiledChargeSpeed = IsEnemyPossessed() ? FMath::Max(ChargeSpeed, 0.0f)
		: ChargeSpeedState.Update(AIChargeSpeedProfile, ChargeSpeed, Movement->Velocity.Size(),
			0.0f, DeltaTime, false, this, TEXT("Charge"));
	float ForwardChargeSpeed = ProfiledChargeSpeed;
	if (bCommittedChargeDistanceLimited)
	{
		const float TraveledChargeDistance = FMath::Max(
			FVector::DotProduct(GetActorLocation() - CommittedChargeStartLocation,
				CommittedChargeTargetDirection), 0.0f);
		const float RemainingChargeDistance = FMath::Max(
			CommittedChargeDistanceLimit - TraveledChargeDistance, 0.0f);
		ForwardChargeSpeed = FMath::Min(ProfiledChargeSpeed,
			RemainingChargeDistance / FMath::Max(DeltaTime, SMALL_NUMBER));
	}
	const FVector ChargeVelocity = CommittedChargeDirection * ForwardChargeSpeed;
	const FVector FinalVelocity = ChargeVelocity + AccumulatedImpactVelocity;
	Movement->MaxFlySpeed = FMath::Max(
		GetRuntimeStat().MoveSpeed,
		FinalVelocity.Size());
	Movement->Velocity = FinalVelocity;

	FRotator ChargeRotation = CommittedChargeDirection.Rotation();
	ChargeRotation.Roll = 0.0f;
	if (AController* ActiveController = GetController())
	{
		ActiveController->SetControlRotation(ChargeRotation);
	}
	SetActorRotation(FRotator(0.0f, ChargeRotation.Yaw, 0.0f));
	if (bFirstMovementUpdate)
	{
		bChargeMovementDiagnosticStarted = true;
		LogChargeDiagnostics(TEXT("FirstChargeVelocityApplied"));
	}
	return true;
}

void ASelfDestructDrone::CancelCommittedSelfDestruct()
{
	if (!HasAuthority() || !ChargeState.bActive || bDeathHandling)
	{
		return;
	}
	UE_LOG(LogEnemyChargeCollision, Log, TEXT("[Charge] Cancel Drone=%s"), *GetNameSafe(this));
	LogChargeDiagnostics(TEXT("CancelRequested"));

	PublishChargeState(false);
	ChargeSpeedState.Reset();
	if (HackableComponent)
	{
		// 기절 등으로 자폭 커밋이 취소되면 살아 있는 드론은 다시 해킹 후보가 될 수 있다.
		HackableComponent->HackTags.RemoveTag(OutlierGameplayTags::State::Locked());
	}
	CommittedChargeDirection = FVector::ForwardVector;
	CommittedChargeTargetDirection = FVector::ForwardVector;
	CommittedChargeStartLocation = FVector::ZeroVector;
	CommittedChargeSpeed = 0.0f;
	CommittedChargeDistanceLimit = 0.0f;
	bCommittedChargeDistanceLimited = false;
	CommittedImpactElapsedTime = 0.0f;
	AccumulatedImpactVelocity = FVector::ZeroVector;
	CurrentImpactStrength = 0.0f;
	CurrentPhysicalKnockbackDuration = 0.0f;
	CurrentControlRecoveryDuration = 0.0f;
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->MaxFlySpeed = FMath::Max(GetRuntimeStat().MoveSpeed, 0.0f);
	}
	LogChargeDiagnostics(TEXT("CancelCompleted"));
	ForceNetUpdate();
}

bool ASelfDestructDrone::TryApplyCommittedImpactVelocity(const FVector& ImpactVelocity)
{
	if (!ChargeState.bActive)
	{
		return false;
	}

	AccumulateImpactVelocity(ImpactVelocity);
	LogChargeDiagnostics(TEXT("ImpactVelocityApplied"));
	CommittedImpactElapsedTime = 0.0f;

	const FVector CurrentDirection = CommittedChargeDirection.GetSafeNormal();
	FVector DesiredDirection = (
		CurrentDirection * FMath::Max(CommittedChargeSpeed, 1.0f)
		+ AccumulatedImpactVelocity).GetSafeNormal();
	if (DesiredDirection.IsNearlyZero())
	{
		DesiredDirection = CurrentDirection;
	}

	const float MaxTurnRadians = FMath::DegreesToRadians(FMath::Clamp(
		RuntimeImpactReactionProfile.MaxChargeImpactTurnAngleDegrees,
		0.0f,
		180.0f));
	const float TurnRadians = FMath::Acos(FMath::Clamp(
		FVector::DotProduct(CurrentDirection, DesiredDirection),
		-1.0f,
		1.0f));
	if (TurnRadians > MaxTurnRadians && MaxTurnRadians > 0.0f)
	{
		FVector TurnAxis = FVector::CrossProduct(CurrentDirection, DesiredDirection).GetSafeNormal();
		if (TurnAxis.IsNearlyZero())
		{
			TurnAxis = FVector::UpVector;
		}
		DesiredDirection = FQuat(TurnAxis, MaxTurnRadians).RotateVector(CurrentDirection);
	}

	FRotator LimitedRotation = DesiredDirection.Rotation();
	LimitedRotation.Pitch = FMath::Clamp(
		FRotator::NormalizeAxis(LimitedRotation.Pitch),
		-RuntimeImpactReactionProfile.MaxChargePitchDegrees,
		RuntimeImpactReactionProfile.MaxChargePitchDegrees);
	LimitedRotation.Roll = 0.0f;
	CommittedChargeTargetDirection = LimitedRotation.Vector();
	return true;
}

void ASelfDestructDrone::CancelCommittedAction()
{
	CancelCommittedSelfDestruct();
}

void ASelfDestructDrone::PublishChargeState(bool bActive, float Duration)
{
	if (!HasAuthority() || !GetWorld()) return;
	++ChargeState.Revision;
	ChargeState.bActive = bActive;
	ChargeState.Playback.ServerStartTime = GetWorld()->GetTimeSeconds();
	ChargeState.Playback.Duration = Duration;
	ChargeState.Playback.SoundDuration = ChargeSoundDuration;
	if (bActive && Duration != 0.0f && !ChargeState.Playback.IsValid())
	{
		UE_LOG(LogOutlier, Warning, TEXT("[SelfDestructDrone] Invalid charge audio window Actor=%s Duration=%f SoundDuration=%f"),
			*GetName(), Duration, ChargeSoundDuration);
	}
	RefreshChargePresentation();
	LogChargeDiagnostics(bActive ? TEXT("ChargeStatePublishedActive") : TEXT("ChargeStatePublishedInactive"));
	RefreshChargeDiagnosticTimer();
	ForceNetUpdate();
}

void ASelfDestructDrone::SetCommittedChargeDuration(float Duration)
{
	if (!HasAuthority() || !ChargeState.bActive) return;
	// Begin/Update Task가 다른 설정을 갖더라도 실제 카운트다운 시작 시각과 기간을 한 번에 게시한다.
	PublishChargeState(true, Duration);
}

float ASelfDestructDrone::GetCommittedChargeElapsedTime() const
{
	return GetWorld() ? static_cast<float>(FMath::Max(0.0,
		GetWorld()->GetTimeSeconds() - ChargeState.Playback.ServerStartTime)) : 0.0f;
}

void ASelfDestructDrone::OnRep_ChargeState()
{
	LogChargeDiagnostics(TEXT("ChargeStateReplicated"));
	RefreshChargeDiagnosticTimer();
	RefreshChargePresentation();
}

void ASelfDestructDrone::OnRep_MountedExplosives()
{
	LogChargeDiagnostics(TEXT("MountedExplosivesReplicated"));
	RefreshChargePresentation();
}

void ASelfDestructDrone::OnPoolStateApplied(EEnemyPoolState PreviousState)
{
	LogChargeDiagnostics(TEXT("BeforePoolStateApplied"));
	Super::OnPoolStateApplied(PreviousState);
	LogChargeDiagnostics(TEXT("AfterPoolStateApplied"));
	RefreshChargeDiagnosticTimer();
	RefreshChargePresentation();
}

void ASelfDestructDrone::RefreshChargePresentation()
{
	if (!HasActorBegunPlay() || !GetWorld() || GetNetMode() == NM_DedicatedServer) return;
	if (!ChargeState.bActive || !ChargeState.Playback.IsValid() || IsDead()
		|| (IsPoolManaged() && GetEnemyPoolState() != EEnemyPoolState::CombatActive))
	{
		StopChargePresentation();
		return;
	}
	// 복제 상태, 서버 시계, 부착 Actor는 서로 다른 순서로 준비될 수 있다. 활성 시도에서만 재시도한다.
	if (!GetWorldTimerManager().IsTimerActive(ChargePresentationTimer))
	{
		GetWorldTimerManager().SetTimer(ChargePresentationTimer, this,
			&ASelfDestructDrone::RefreshChargePresentation, 1.0f / 60.0f, true);
	}
	if (!ChargeClock.IsValid()) ChargeClock = GetWorld()->GetGameState();
	if (!ChargeClock.IsValid()) return;
	float PlaybackTime = 0.0f;
	if (!ChargeState.Playback.GetPlaybackTime(ChargeClock->GetServerWorldTimeSeconds(), PlaybackTime))
	{
		// 정상 종료 뒤 폭발 복제가 도착할 때까지 마지막 밝기를 유지한다.
		// 만료된 상태로 처음 진입한 클라이언트는 사운드/이벤트를 뒤늦게 시작하지 않는다.
		if (bChargePresentationStarted && PresentedChargeRevision == ChargeState.Revision)
		{
			GetWorldTimerManager().ClearTimer(ChargePresentationTimer);
			if (ChargeAudio.IsValid()) ChargeAudio->StopLocalEmitterTimedSound(this, TelegraphAudioContext);
			if (PresentationExplosive.IsValid())
			{
				PresentationExplosive->ApplyChargeEmissive(
					ChargeState.Playback.SoundDuration, ChargeState.Playback.Duration);
			}
		}
		else
		{
			StopChargePresentation();
		}
		return;
	}
	if (PresentedChargeRevision != ChargeState.Revision || !bChargePresentationStarted)
	{
		StopChargePresentation();
		PresentedChargeRevision = ChargeState.Revision;
		bChargePresentationStarted = true;
		if (ChargeAudio.IsValid())
		{
			ChargeAudio->SetLocalEmitterTimedSound(this, OutlierAudioFeedback::Tag(TEXT("Audio.Type.Enemy")),
				TelegraphAudioContext, ChargeState.Playback);
		}
		OnSelfDestructTelegraphStarted(ChargeState.Playback.Duration);
		GetWorldTimerManager().SetTimer(ChargePresentationTimer, this,
			&ASelfDestructDrone::RefreshChargePresentation, 1.0f / 60.0f, true);
	}
	AExplosiveProp* Explosive = GetMountedExplosive();
	if (PresentationExplosive.Get() != Explosive)
	{
		if (PresentationExplosive.IsValid()) PresentationExplosive->ResetChargeEmissive();
		PresentationExplosive = Explosive;
	}
	if (IsValid(Explosive))
	{
		// 로컬 누적 시간이 아닌 같은 서버 시계로 평가해 늦게 보인 드론도 현재 밝기에 합류한다.
		const float ChargeElapsedTime = static_cast<float>(FMath::Max(
			0.0, ChargeClock->GetServerWorldTimeSeconds() - ChargeState.Playback.ServerStartTime));
		Explosive->ApplyChargeEmissive(PlaybackTime, ChargeElapsedTime);
	}
}

void ASelfDestructDrone::StopChargePresentation()
{
	if (GetWorld()) GetWorldTimerManager().ClearTimer(ChargePresentationTimer);
	if (ChargeAudio.IsValid()) ChargeAudio->StopLocalEmitterTimedSound(this, TelegraphAudioContext);
	if (PresentationExplosive.IsValid()) PresentationExplosive->ResetChargeEmissive();
	PresentationExplosive.Reset();
	if (bChargePresentationStarted)
	{
		bChargePresentationStarted = false;
		OnSelfDestructTelegraphCancelled();
	}
}

void ASelfDestructDrone::HandleDeath()
{
	if (!HasAuthority() || bDeathHandling)
	{
		return;
	}

	bDeathHandling = true;
	LogChargeDiagnostics(TEXT("DeathBeforeExplosion"));
	PublishChargeState(false);
	bCommittedChargeDistanceLimited = false;
	// EnemyBase가 Actor를 제거하기 전에 폭발 Queue와 클라이언트 연출을 먼저 확정한다.
	if (ExplosionComponent)
	{
		ExplosionComponent->SetAudioContextOverride(bTriggeredSelfDestruct ? SelfDestructAudioContext : FGameplayTag());
		// 충돌/시간 만료/피격 사망이 모두 모이는 지점에서 아군 피해를 허용한다.
		// 피해량/차폐/반동은 공통 폭발 경로를 그대로 사용한다.
		ExplosionComponent->DetonateAt(
			GetActorLocation(),
			GetController(),
			IsEnemyPossessed()
				? EOutlierAdaptationDamageCategory::NonGun
				: EOutlierAdaptationDamageCategory::Ignore,
			true);
	}
	DestroyMountedExplosives();

	LogChargeDiagnostics(TEXT("DeathExplosionResult"));
	Super::HandleDeath();
}

void ASelfDestructDrone::ResetPoolRuntimeState()
{
	LogChargeDiagnostics(TEXT("BeforePoolReset"));
	ChargeSpeedState.Reset();
	// 본체는 재사용하지만 부착 폭발물은 Idle 진입 때 제거한다. 새 대여마다 폭발 가능한 부품을 다시 만든다.
	Super::ResetPoolRuntimeState();
	// 본체의 폭발 컴포넌트도 재사용한다. 새 대여에서만 재무장하고 사망/Idle 동안에는 중복 폭발 방지를 유지한다.
	if (ExplosionComponent)
	{
		ExplosionComponent->ResetExplosion();
	}
	bDeathHandling = false;
	bTriggeredSelfDestruct = false;
	PublishChargeState(false);
	CommittedChargeDirection = FVector::ForwardVector;
	CommittedChargeTargetDirection = FVector::ForwardVector;
	CommittedChargeStartLocation = FVector::ZeroVector;
	CommittedChargeSpeed = 0.0f;
	CommittedChargeDistanceLimit = 0.0f;
	bCommittedChargeDistanceLimited = false;
	CommittedImpactElapsedTime = 0.0f;
	SpawnAndAttachMountedExplosives();
	LogChargeDiagnostics(TEXT("AfterPoolReset"));
}

void ASelfDestructDrone::PrepareForPoolIdle()
{
	LogChargeDiagnostics(TEXT("BeforePoolIdle"));
	PublishChargeState(false);
	StopChargePresentation();
	ChargeSpeedState.Reset();
	Super::PrepareForPoolIdle();
	DestroyMountedExplosives();
	LogChargeDiagnostics(TEXT("AfterPoolIdle"));
}
