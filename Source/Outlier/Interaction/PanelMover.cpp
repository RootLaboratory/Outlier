#include "Interaction/PanelMover.h"

#include "Audio/OutlierAudioSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "OutlierPlayerState.h"
#include "Save/OutlierSaveSubSystem.h"
#include "Shooter/ShooterCharacter.h"

APanelMover::APanelMover()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("PanelRoot"));

	Supporter = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Supporter"));
	Supporter->SetupAttachment(RootComponent);

	PanelMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PanelMesh"));
	PanelMesh->SetupAttachment(Supporter);
}

void APanelMover::BeginPlay()
{
	Super::BeginPlay();
	CacheMovingCollision();
	bPanelInitialized = true;

	if (!HasAuthority())
	{
		ApplyMotion(ReplicatedMotion);
		return;
	}

	if (UOutlierSaveSubSystem* SaveSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
		: nullptr)
	{
		bProgressIdRegistered = SaveSubsystem->RegisterWorldProgressId(
			EOutlierWorldProgressType::MovedPanel,
			PanelId,
			this);
		if (bProgressIdRegistered
			&& SaveSubsystem->HasWorldProgress(EOutlierWorldProgressType::MovedPanel, PanelId))
		{
			// 복원은 새 이동이 아니므로 보간 없이 도착 위치에 둔다.
			SnapToTarget();
		}
	}
}

void APanelMover::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseFlyingSupports();
	MovingCollision.Reset();
	bRestorePending = false;
	SetMovingAudio(false);

	if (bProgressIdRegistered)
	{
		if (UOutlierSaveSubSystem* SaveSubsystem = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
			: nullptr)
		{
			SaveSubsystem->UnregisterWorldProgressId(
				EOutlierWorldProgressType::MovedPanel,
				PanelId,
				this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

bool APanelMover::MoveToTarget()
{
	if (!HasAuthority())
	{
		return false;
	}

	if (bIsMoved)
	{
		return false;
	}

	if (!TargetLocationActor)
	{
		return false;
	}

	CacheMovingCollision();
	StartLocation = GetActorLocation();
	TargetLocation = TargetLocationActor->GetActorLocation();
	if (MovingCollision.IsEmpty() || TargetLocation.ContainsNaN())
	{
		UE_LOG(LogTemp, Warning, TEXT("[PanelMover] %s rejected: missing collision or invalid target"), *GetName());
		return false;
	}

	// 스위치와 저장 진행을 확정하기 전에 첫 접촉 이동이 가능한지 확인한다.
	const float FirstRatio = MoveDuration > KINDA_SMALL_NUMBER
		? FMath::Min(1.0f / 30.0f / MoveDuration, 1.0f) : 1.0f;
	if (!TryMovePanel(FMath::Lerp(StartLocation, TargetLocation,
		FirstRatio >= 1.0f ? 1.0f : EvaluateProgress(FirstRatio)), false))
	{
		return false;
	}
	bIsMoved = true;
	if (bProgressIdRegistered)
	{
		if (UOutlierSaveSubSystem* SaveSubsystem = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
			: nullptr)
		{
			SaveSubsystem->SetWorldProgressState(
				EOutlierWorldProgressType::MovedPanel,
				PanelId,
				true);
		}
	}

	StartMove();
	ForceNetUpdate();
	return true;
}

void APanelMover::StartMove()
{
	if (bMoveStarted || !TargetLocationActor)
	{
		return;
	}

	bMoveStarted = true;
	StartLocation = GetActorLocation();
	TargetLocation = TargetLocationActor->GetActorLocation();
	MoveElapsed = 0.0f;
	bFinalPlacementPending = false;
	if (MoveDuration <= KINDA_SMALL_NUMBER)
	{
		if (TryMovePanel(TargetLocation, true)) FinishMove();
		else
		{
			SetActorTickEnabled(true);
			PublishMotion(true, true);
		}
		return;
	}
	ApplyMovingMaterials();
	SetMovingAudio(true);
	SetActorTickEnabled(true);
	PublishMotion(true, false);
}

void APanelMover::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!HasAuthority())
	{
		// 접촉 베이스의 로컬 예측만 돕는다. 안전 보류·완료는 서버 기준점을 따른다.
		UpdatePredictedSupport();
		if (!ReplicatedMotion.bPaused && ReplicatedMotion.bMoving)
		{
			MoveElapsed = FMath::Min(MoveElapsed + FMath::Max(DeltaTime, 0.0f), MoveDuration);
			const float Ratio = MoveDuration > KINDA_SMALL_NUMBER ? MoveElapsed / MoveDuration : 1.0f;
			SetActorLocation(Ratio >= 1.0f ? TargetLocation
				: FMath::Lerp(StartLocation, TargetLocation, EvaluateProgress(Ratio)));
		}
		return;
	}

	if (bRestorePending)
	{
		SnapToTarget();
		return;
	}
	if (!bMoveStarted || bMoveFinished) return;
	if (bFinalPlacementPending)
	{
		// 커브 끝의 보정이 막혔으면 이전 커브 구간으로 되돌아가지 않는다.
		// 다음 프레임 길이와 무관하게 같은 목적지만 재검사하며 보류 시간을 누적하지 않는다.
		if (TryMovePanel(TargetLocation, true))
		{
			bFinalPlacementPending = false;
			MoveElapsed = FMath::Max(MoveDuration, 0.0f);
			FinishMove();
		}
		return;
	}

	// 큰 프레임에서도 커브의 중간 접촉을 건너뛰지 않는다. 한 프레임의 작업량은
	// 제한하고, 사용하지 못한 시간은 버려 보류 해제 때 급격히 따라잡지 않게 한다.
	float Remaining = FMath::Max(DeltaTime, 0.0f);
	for (int32 Step = 0; Step < 16 && Remaining > SMALL_NUMBER; ++Step)
	{
		const float Slice = FMath::Min(Remaining, 1.0f / 30.0f);
		const float NextElapsed = FMath::Min(MoveElapsed + Slice, MoveDuration);
		const float Ratio = MoveDuration > KINDA_SMALL_NUMBER ? NextElapsed / MoveDuration : 1.0f;
		const FVector Desired = Ratio >= 1.0f ? TargetLocation
			: FMath::Lerp(StartLocation, TargetLocation, EvaluateProgress(Ratio));
		if (!TryMovePanel(Desired, true))
		{
			bFinalPlacementPending = Ratio >= 1.0f;
			if (!bPaused) PublishMotion(true, true);
			return;
		}
		MoveElapsed = NextElapsed;
		if (bPaused) PublishMotion(true, false);
		if (Ratio >= 1.0f)
		{
			FinishMove();
			return;
		}
		Remaining -= Slice;
	}
	if (GetServerTime() - LastPublishedAt >= 0.1) PublishMotion(true, false);
}

void APanelMover::FinishMove()
{
	// 최종 위치도 TryMovePanel 또는 복원 겹침 검사를 통과한 뒤에만 완료한다.
	bMoveFinished = true;
	SetActorTickEnabled(false);
	SetMovingAudio(false);
	RestoreOriginalMaterials();
	ReleaseFlyingSupports();
	if (HasAuthority()) PublishMotion(false, false);
}

void APanelMover::ApplyMovingMaterials()
{
	CachedMaterials.Reset();

	auto Apply = [this](UStaticMeshComponent* Mesh, const FPanelMoverMovingMaterial& Setting)
	{
		if (!Mesh || !Setting.MovingMaterial
			|| Setting.MaterialIndex < 0
			|| Setting.MaterialIndex >= Mesh->GetNumMaterials())
		{
			return;
		}

		FPanelMoverCachedMaterial& Cached = CachedMaterials.AddDefaulted_GetRef();
		Cached.Mesh = Mesh;
		Cached.MaterialIndex = Setting.MaterialIndex;
		Cached.OriginalMaterial = Mesh->GetMaterial(Setting.MaterialIndex);
		Mesh->SetMaterial(Setting.MaterialIndex, Setting.MovingMaterial);
	};

	Apply(Supporter, SupporterMovingMaterial);
	Apply(PanelMesh, LiftPlatformMovingMaterial);
}

void APanelMover::RestoreOriginalMaterials()
{
	// 체크포인트 스냅처럼 이동을 거치지 않은 도착은 캐시가 비어 있어 아무것도 바꾸지 않는다.
	for (const FPanelMoverCachedMaterial& Cached : CachedMaterials)
	{
		if (UStaticMeshComponent* Mesh = Cached.Mesh.Get())
		{
			Mesh->SetMaterial(Cached.MaterialIndex, Cached.OriginalMaterial.Get());
		}
	}
	CachedMaterials.Reset();
}

void APanelMover::SetMovingAudio(bool bEnabled)
{
	if (bMovingAudioActive == bEnabled)
	{
		return;
	}

	UWorld* World = GetWorld();
	UOutlierAudioSubsystem* Audio = World && World->GetGameInstance()
		? World->GetGameInstance()->GetSubsystem<UOutlierAudioSubsystem>()
		: nullptr;
	if (!Audio)
	{
		return;
	}

	const FGameplayTag TypeTag = MovingAudioTypeTag.IsValid()
		? MovingAudioTypeTag
		: FGameplayTag::RequestGameplayTag(TEXT("Audio.Type.Environment"));
	const FGameplayTag ContextTag = MovingAudioContextTag.IsValid()
		? MovingAudioContextTag
		: FGameplayTag::RequestGameplayTag(TEXT("Audio.Context.Ambience.Panel.Moving"));

	bMovingAudioActive = bEnabled;
	Audio->SetLocalEmitterLoop(this, TypeTag, ContextTag, bEnabled);
}

void APanelMover::SnapToTarget()
{
	if (!TargetLocationActor)
	{
		return;
	}

	bIsMoved = true;
	bMoveStarted = true;
	TargetLocation = TargetLocationActor->GetActorLocation();
	if (!CanRestoreAt(TargetLocation))
	{
		// 스트리밍 순서상 플레이어가 먼저 도착했으면 충돌체를 몸 안으로 스냅하지 않는다.
		bRestorePending = true;
		SetActorTickEnabled(true);
		return;
	}
	bRestorePending = false;
	SetActorLocation(TargetLocation);
	FinishMove();
	ForceNetUpdate();
}

float APanelMover::EvaluateProgress(float TimeRatio) const
{
	if (!MoveCurve)
	{
		return TimeRatio;
	}

	float MinTime = 0.0f;
	float MaxTime = 0.0f;
	MoveCurve->GetTimeRange(MinTime, MaxTime);
	if (MaxTime <= MinTime)
	{
		return TimeRatio;
	}
	return MoveCurve->GetFloatValue(FMath::Lerp(MinTime, MaxTime, TimeRatio));
}

void APanelMover::CacheMovingCollision()
{
	MovingCollision.Reset();
	TInlineComponentArray<UPrimitiveComponent*> Components(this);
	for (UPrimitiveComponent* Component : Components)
	{
		if (Component && Component->IsQueryCollisionEnabled() && Component->IsRegistered()
			&& (Component == GetRootComponent() || Component->IsAttachedTo(GetRootComponent())))
		{
			MovingCollision.Add(Component);
		}
	}
}

void APanelMover::GatherPlayerCharacters(TArray<ACharacter*, TInlineAllocator<2>>& Characters) const
{
	const auto AddCharacter = [&Characters](ACharacter* Character)
	{
		if (IsValid(Character) && !Character->IsActorBeingDestroyed()
			&& Character->GetActorEnableCollision() && Character->GetCapsuleComponent()->IsQueryCollisionEnabled())
		{
			Characters.AddUnique(Character);
		}
	};
	// 복원 중 아직 Possess 전인 Pair Pawn도 스냅 목적지 검사에서 빠지면 안 된다.
	// 기존 PlayerState 참조와 현재 Controller만 사용하고 월드 액터를 검색하지 않는다.
	if (const AGameStateBase* State = GetWorld()->GetGameState())
	{
		for (APlayerState* PlayerState : State->PlayerArray)
		{
			if (const AOutlierPlayerState* Pair = Cast<AOutlierPlayerState>(PlayerState))
			{
				AddCharacter(Pair->GetShooterCharacter());
				AddCharacter(Pair->GetPartnerCharacter());
			}
		}
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Controller = It->Get();
		ACharacter* Character = Controller ? Cast<ACharacter>(Controller->GetPawn()) : nullptr;
		if (Character && Character->GetController() == Controller) AddCharacter(Character);
	}
}

UPrimitiveComponent* APanelMover::FindSupport(ACharacter* Character) const
{
	UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	const FVector Location = Capsule->GetComponentLocation();
	for (const TWeakObjectPtr<UPrimitiveComponent>& WeakComponent : MovingCollision)
	{
		UPrimitiveComponent* Component = WeakComponent.Get();
		if (!Component || !Component->IsQueryCollisionEnabled()
			|| Component->GetCollisionResponseToChannel(Capsule->GetCollisionObjectType()) != ECR_Block
			|| Capsule->GetCollisionResponseToChannel(Component->GetCollisionObjectType()) != ECR_Block)
		{
			continue;
		}
		FHitResult Hit;
		// 캡슐 발 밑의 실제 메시 형상으로 지지를 판단한다. AABB 위에 있다는 이유로 운반하지 않는다.
		if (Component->SweepComponent(Hit, Location, Location - FVector(0.0f, 0.0f, 5.0f),
			Capsule->GetComponentQuat(), Capsule->GetCollisionShape(-0.1f))
			&& !Hit.bStartPenetrating && Hit.ImpactNormal.Z >= 0.5f)
		{
			return Component;
		}
	}
	return nullptr;
}

bool APanelMover::TryMovePanel(const FVector& DesiredLocation, bool bCommit)
{
	const FVector Delta = DesiredLocation - GetActorLocation();
	if (Delta.ContainsNaN()) return false;
	if (Delta.IsNearlyZero()) return true;

	struct FContactMove
	{
		ACharacter* Character = nullptr;
		UPrimitiveComponent* Support = nullptr;
		FVector Delta = FVector::ZeroVector;
	};
	TArray<ACharacter*, TInlineAllocator<2>> Characters;
	GatherPlayerCharacters(Characters);
	TArray<FContactMove, TInlineAllocator<2>> Moves;
	for (ACharacter* Character : Characters)
	{
		UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		const FVector Location = Capsule->GetComponentLocation();
		FContactMove Move;
		Move.Character = Character;
		Move.Support = FindSupport(Character);
		if (Move.Support)
		{
			Move.Delta = Delta;
		}
		else
		{
			for (const TWeakObjectPtr<UPrimitiveComponent>& WeakComponent : MovingCollision)
			{
				UPrimitiveComponent* Component = WeakComponent.Get();
				if (!Component || !Component->IsQueryCollisionEnabled()
					|| Component->GetCollisionResponseToChannel(Capsule->GetCollisionObjectType()) != ECR_Block
					|| Capsule->GetCollisionResponseToChannel(Component->GetCollisionObjectType()) != ECR_Block) continue;
				FHitResult Hit;
				// 패널을 움직이는 대신 캡슐을 반대 변위로 Sweep하여 자식 메시의 실제 형상을 검사한다.
				if (Component->SweepComponent(Hit, Location, Location - Delta, Capsule->GetComponentQuat(),
					Capsule->GetCollisionShape(-0.1f)))
				{
					if (!Hit.bStartPenetrating && FVector::DotProduct(Delta, Hit.Normal) <= 0.0f) continue;
					const FVector Push = Delta * (1.0f - Hit.Time)
						+ Hit.Normal * (FMath::Max(Hit.PenetrationDepth, 0.0f) + 0.5f);
					if (Push.SizeSquared() > Move.Delta.SizeSquared()) Move.Delta = Push;
				}
			}
		}
		Moves.Add(Move);
	}

	// 모든 몸체의 경로와 도착을 먼저 검증한다. 두 명 중 한 명만 옮긴 상태에서
	// 나머지 실패를 발견하고 패널을 계속 움직이는 부분 성공을 만들지 않는다.
	const auto IsMoveSafe = [&](const FContactMove& Move)
	{
		UCapsuleComponent* Capsule = Move.Character->GetCapsuleComponent();
		const FVector Start = Capsule->GetComponentLocation();
		const FVector End = Start + Move.Delta;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PanelContactMove), false, this);
		for (ACharacter* Character : Characters) Params.AddIgnoredActor(Character);
		const FCollisionResponseParams Responses(Capsule->GetCollisionResponseToChannels());
		FHitResult Hit;
		if (!Move.Delta.IsNearlyZero()
			&& (GetWorld()->SweepSingleByChannel(Hit, Start, End, Capsule->GetComponentQuat(),
				Capsule->GetCollisionObjectType(), Capsule->GetCollisionShape(-0.1f), Params, Responses)
				|| GetWorld()->OverlapBlockingTestByChannel(End, Capsule->GetComponentQuat(),
					Capsule->GetCollisionObjectType(), Capsule->GetCollisionShape(-0.1f), Params, Responses))) return false;

		for (const TWeakObjectPtr<UPrimitiveComponent>& WeakComponent : MovingCollision)
		{
			UPrimitiveComponent* Component = WeakComponent.Get();
			if (Component && Component->IsQueryCollisionEnabled()
				&& Component->GetCollisionResponseToChannel(Capsule->GetCollisionObjectType()) == ECR_Block
				&& Capsule->GetCollisionResponseToChannel(Component->GetCollisionObjectType()) == ECR_Block
				&& Component->OverlapComponent(End - Delta, Capsule->GetComponentQuat(), Capsule->GetCollisionShape(-0.1f)))
			{
				return false;
			}
		}
		for (const FContactMove& Other : Moves)
		{
			if (Other.Character == Move.Character
				|| (Move.Delta.IsNearlyZero() && Other.Delta.IsNearlyZero())) continue;
			UCapsuleComponent* OtherCapsule = Other.Character->GetCapsuleComponent();
			if (OtherCapsule->SweepComponent(Hit, Start, End - Other.Delta,
				Capsule->GetComponentQuat(), Capsule->GetCollisionShape(-0.1f))) return false;
		}
		return true;
	};
	for (FContactMove& Move : Moves)
	{
		if (IsMoveSafe(Move)) continue;
		if (Move.Delta.IsNearlyZero()) return false;
		// 정상 운반이 막힌 경우만 가까운 수평 분리를 시도한다. 후보의 경로와
		// 도착을 모두 검사하며, 제한된 근방에서도 공간이 없으면 시간을 멈춘다.
		const FVector OriginalDelta = Move.Delta;
		const float Radius = Move.Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
		bool bSeparated = false;
		for (int32 Ring = 1; Ring <= 3 && !bSeparated; ++Ring)
		{
			for (int32 Direction = 0; Direction < 8; ++Direction)
			{
				const float Angle = Direction * PI / 4.0f;
				Move.Delta = OriginalDelta + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * Radius * Ring;
				if (IsMoveSafe(Move))
				{
					Move.Support = nullptr;
					bSeparated = true;
					break;
				}
			}
		}
		if (!bSeparated) return false;
	}
	// 두 번째 몸체의 분리 후보가 바뀌었을 때 첫 번째 경로도 다시 확인한다.
	for (const FContactMove& Move : Moves) if (!IsMoveSafe(Move)) return false;
	if (!bCommit) return true;

	for (const FContactMove& Move : Moves)
	{
		UCharacterMovementComponent* Movement = Move.Character->GetCharacterMovement();
		if (Move.Support && Movement)
		{
			// Walking의 기존 베이스와 Flying의 임시 지지 모두 CMC의 외부 변위 추종을 쓴다.
			// 모드나 입력을 바꾸지 않고 매번 접촉을 재검사하므로 자유롭게 이탈할 수 있다.
			if (Move.Character->GetMovementBase() != Move.Support) Move.Character->SetBase(Move.Support);
			if (Movement->IsFlying()) FlyingSupports.AddUnique(Move.Character);
		}
		else
		{
			if (Movement && Move.Character->GetMovementBase()
				&& Move.Character->GetMovementBase()->GetOwner() == this) Move.Character->SetBase(nullptr);
			if (!Move.Delta.IsNearlyZero())
			{
				// 위에서 실제 캡슐 Sweep과 최종 공간을 검증했다. 아직 이전 위치에 있는
				// 패널만 무시하고 몸체를 먼저 분리한 뒤 패널을 옮긴다.
				Move.Character->SetActorLocation(Move.Character->GetActorLocation() + Move.Delta, false);
			}
		}
	}
	SetActorLocation(DesiredLocation);
	for (const FContactMove& Move : Moves)
	{
		if (Move.Support)
		{
			if (UCharacterMovementComponent* Movement = Move.Character->GetCharacterMovement())
			{
				Movement->UpdateBasedMovement(0.0f);
				// 같은 프레임의 일반 CMC Tick이 동일한 베이스 변위를 다시 적용하지 않는다.
				Movement->SaveBaseLocation();
			}
		}
	}
	return true;
}

bool APanelMover::CanRestoreAt(const FVector& Location) const
{
	if (Location.ContainsNaN()) return false;
	const FVector Delta = Location - GetActorLocation();
	TArray<ACharacter*, TInlineAllocator<2>> Characters;
	GatherPlayerCharacters(Characters);
	for (ACharacter* Character : Characters)
	{
		UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		for (const TWeakObjectPtr<UPrimitiveComponent>& WeakComponent : MovingCollision)
		{
			UPrimitiveComponent* Component = WeakComponent.Get();
			if (Component && Component->GetCollisionResponseToChannel(Capsule->GetCollisionObjectType()) == ECR_Block
				&& Capsule->GetCollisionResponseToChannel(Component->GetCollisionObjectType()) == ECR_Block
				&& Component->OverlapComponent(Capsule->GetComponentLocation() - Delta,
					Capsule->GetComponentQuat(), Capsule->GetCollisionShape(-0.1f))) return false;
		}
	}
	return true;
}

void APanelMover::UpdatePredictedSupport()
{
	TArray<ACharacter*, TInlineAllocator<2>> Characters;
	GatherPlayerCharacters(Characters);
	for (ACharacter* Character : Characters)
	{
		UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
		if (!Character->IsLocallyControlled() || !Movement || !Movement->IsFlying()) continue;
		UPrimitiveComponent* Support = FindSupport(Character);
		if (Support)
		{
			Character->SetBase(Support);
			FlyingSupports.AddUnique(Character);
		}
		else if (Character->GetMovementBase() && Character->GetMovementBase()->GetOwner() == this)
		{
			Character->SetBase(nullptr);
		}
	}
}

void APanelMover::ReleaseFlyingSupports()
{
	for (const TWeakObjectPtr<ACharacter>& WeakCharacter : FlyingSupports)
	{
		ACharacter* Character = WeakCharacter.Get();
		if (Character && Character->GetMovementBase() && Character->GetMovementBase()->GetOwner() == this)
		{
			Character->SetBase(nullptr);
		}
	}
	FlyingSupports.Reset();
}

double APanelMover::GetServerTime() const
{
	const AGameStateBase* State = GetWorld()->GetGameState();
	return State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

void APanelMover::PublishMotion(bool bMoving, bool bNewPaused)
{
	const bool bStateChanged = ReplicatedMotion.Revision == 0
		|| ReplicatedMotion.bMoving != bMoving || bPaused != bNewPaused;
	bPaused = bNewPaused;
	ReplicatedMotion.Start = StartLocation;
	ReplicatedMotion.Target = TargetLocation;
	ReplicatedMotion.Location = GetActorLocation();
	ReplicatedMotion.Elapsed = MoveElapsed;
	ReplicatedMotion.ServerTime = LastPublishedAt = GetServerTime();
	ReplicatedMotion.bMoving = bMoving;
	ReplicatedMotion.bPaused = bPaused;
	++ReplicatedMotion.Revision;
	if (bStateChanged)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[PanelMover] Panel=%s Revision=%u Moving=%d Paused=%d Location=%s Elapsed=%.3f Reason=%s"),
			*GetPathName(), ReplicatedMotion.Revision, bMoving, bPaused,
			*GetActorLocation().ToCompactString(), MoveElapsed,
			bPaused ? TEXT("UnsafeCapsulePathOrPlacement") : bMoving ? TEXT("SafeMotion") : TEXT("Arrived"));
	}
	ForceNetUpdate();
	SetMovingAudio(bMoving && !bPaused);
}

void APanelMover::OnRep_Motion()
{
	if (bPanelInitialized) ApplyMotion(ReplicatedMotion);
}

void APanelMover::ApplyMotion(const FPanelMoverMotionState& Motion)
{
	if (Motion.Revision == 0 || Motion.Revision <= LastAppliedRevision) return;
	LastAppliedRevision = Motion.Revision;
	StartLocation = Motion.Start;
	TargetLocation = Motion.Target;
	MoveElapsed = Motion.Elapsed;
	bMoveStarted = true;
	bMoveFinished = !Motion.bMoving;
	bPaused = Motion.bPaused;
	// 보류와 마지막 위치는 정확히 수신한 서버 결과를 적용한다. 활성 상태만
	// 작은 네트워크 지연을 보간하며, 다음 서버 기준점으로 다시 수렴한다.
	SetActorLocation(Motion.Location);
	if (Motion.bMoving && !Motion.bPaused && MoveDuration > KINDA_SMALL_NUMBER)
	{
		MoveElapsed = FMath::Min(MoveElapsed + static_cast<float>(FMath::Clamp(
			GetServerTime() - Motion.ServerTime, 0.0, 0.1)), MoveDuration);
		const float Ratio = MoveElapsed / MoveDuration;
		SetActorLocation(Ratio >= 1.0f ? TargetLocation
			: FMath::Lerp(StartLocation, TargetLocation, EvaluateProgress(Ratio)));
	}
	if (Motion.bMoving && CachedMaterials.IsEmpty()) ApplyMovingMaterials();
	if (!Motion.bMoving)
	{
		RestoreOriginalMaterials();
		ReleaseFlyingSupports();
	}
	SetMovingAudio(Motion.bMoving && !Motion.bPaused);
	SetActorTickEnabled(Motion.bMoving);
}

void APanelMover::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APanelMover, ReplicatedMotion);
}
