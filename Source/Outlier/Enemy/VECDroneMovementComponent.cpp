#include "Enemy/VECDroneMovementComponent.h"

#include "Components/SceneComponent.h"
#include "Components/StateTreeComponent.h"
#include "Enemy/VECDrone.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/IConsoleManager.h"
#include "ReferenceSkeleton.h"

namespace
{
TAutoConsoleVariable<float> CVarGunDroneBodyDownPitchScale(TEXT("outlier.Enemy.GunBodyDownPitchScale"), 0.5f,
	TEXT("Downward visual aim pitch scale for AI gun drones targeting a Shooter [0, 1]. Does not change shot direction."));
TAutoConsoleVariable<int32> CVarDroneAimClientDebug(TEXT("outlier.Enemy.AimClientDebug"), 1,
	TEXT("Client drone aim diagnostics: 0=off, 1=rendered combat gun drones, at most every 0.5s plus phase changes."));
}

UVECDroneMovementComponent::UVECDroneMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UVECDroneMovementComponent::BeginPlay()
{
	Super::BeginPlay();
	if (const AVECDrone* Drone = GetVECDroneOwner())
	{
		if (const USceneComponent* TiltRoot = Drone->GetThirdPersonTiltRoot())
			BaseNoiseFacingTiltPitch = TiltRoot->GetRelativeRotation().Pitch;
		// 같은 프레임의 조준/사격 Phase를 읽어야 이동 중 첫 사격도 이전 비행 자세를 표시하지 않는다.
		if (UStateTreeComponent* StateTree = Drone->GetStateTreeComponent())
		{
			AddTickPrerequisiteComponent(StateTree);
		}
		if (Drone->GetNetMode() != NM_DedicatedServer)
		{
			DiagnosticMesh = Drone->GetMesh();
			if (USkeletalMeshComponent* Mesh = DiagnosticMesh.Get())
				DiagnosticBoneHandle = Mesh->RegisterOnBoneTransformsFinalizedDelegate(
					FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateUObject(this, &UVECDroneMovementComponent::OnDiagnosticBonesFinalized));
		}
	}
}

void UVECDroneMovementComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (USkeletalMeshComponent* Mesh = DiagnosticMesh.Get())
		Mesh->UnregisterOnBoneTransformsFinalizedDelegate(DiagnosticBoneHandle);
	DiagnosticMesh.Reset();
	Super::EndPlay(EndPlayReason);
}

void UVECDroneMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	DiagnosticMovementFrame = GFrameCounter;
	// 이동 기울기까지 적용한 뒤 기록한다. 본 평가가 뒤에 실행되면 완료 콜백에서 같은 프레임을 기록한다.
	if (DiagnosticBoneFrame == MAX_uint64 || DiagnosticBoneFrame == GFrameCounter
		|| (DiagnosticBoneFrame != MAX_uint64 && GFrameCounter > DiagnosticBoneFrame + 1))
		LogClientAimPresentation();
}

void UVECDroneMovementComponent::OnDiagnosticBonesFinalized()
{
	DiagnosticBoneFrame = GFrameCounter;
	if (DiagnosticMovementFrame == GFrameCounter) LogClientAimPresentation();
}

void UVECDroneMovementComponent::LogClientAimPresentation()
{
	if (CVarDroneAimClientDebug.GetValueOnGameThread() == 0) return;
	const AVECDrone* Drone = GetVECDroneOwner();
	if (!Drone || Drone->GetNetMode() == NM_DedicatedServer || Drone->IsEnemyPossessed() || Drone->IsDead()
		|| Drone->GetRuntimeStat().Type != EEnemyType::Gun || !Drone->WasRecentlyRendered()
		|| Drone->GetCombatState() != EEnemyCombatState::Combat) return;
	const double Now = GetWorld()->GetTimeSeconds();
	const uint8 Phase = static_cast<uint8>(Drone->GetAttackPhase());
	if (Now < NextAimDiagnosticTime && Phase == LastDiagnosticPhase) return;
	USkeletalMeshComponent* Mesh = Drone->GetMesh();
	USceneComponent* Facing = Drone->GetAIFacingPitchRoot();
	USceneComponent* Tilt = Drone->GetThirdPersonTiltRoot();
	if (!Mesh || !Mesh->GetSkeletalMeshAsset() || !Facing || !Tilt) return;
	NextAimDiagnosticTime = Now + 0.5;
	LastDiagnosticPhase = Phase;

	// 기준 포즈와 본 번호는 메시가 바뀔 때만 조회한다. 진단을 위해 본 평가를 강제로 실행하지 않는다.
	if (DiagnosticMeshAsset.Get() != Mesh->GetSkeletalMeshAsset())
	{
		DiagnosticMeshAsset = Mesh->GetSkeletalMeshAsset();
		const FReferenceSkeleton& Skeleton = DiagnosticMeshAsset->GetRefSkeleton();
		DiagnosticBodyBone = Skeleton.FindBoneIndex(TEXT("BodyBone"));
		if (DiagnosticBodyBone != INDEX_NONE)
		{
			FTransform Reference = Skeleton.GetRefBonePose()[DiagnosticBodyBone];
			for (int32 Parent = Skeleton.GetParentIndex(DiagnosticBodyBone); Parent != INDEX_NONE; Parent = Skeleton.GetParentIndex(Parent))
				Reference = Reference * Skeleton.GetRefBonePose()[Parent];
			// 첫 로그에서 이미 숙인 메시 회전을 기준으로 삼으면 오차가 지워진다. BP 기본 회전을 기준선으로 쓴다.
			const USkeletalMeshComponent* DefaultMesh = Drone->GetClass()->GetDefaultObject<AVECDrone>()->GetMesh();
			const FRotator NeutralMeshRotation = DefaultMesh ? DefaultMesh->GetRelativeRotation() : FRotator::ZeroRotator;
			DiagnosticBodyLocalForward = Reference.GetRotation().UnrotateVector(
				NeutralMeshRotation.UnrotateVector(FVector::ForwardVector));
		}
	}
	const FVector Origin = Drone->GetCombatShotOrigin();
	const FVector LastKnown = Drone->GetLastKnownPlayerLocation();
	const AController* Controller = Drone->GetController();
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	const FTransform Body = DiagnosticBodyBone != INDEX_NONE ? Mesh->GetBoneTransform(DiagnosticBodyBone) : FTransform::Identity;
	const FRotator BodyForward = DiagnosticBodyBone != INDEX_NONE
		? Body.GetRotation().RotateVector(DiagnosticBodyLocalForward).Rotation() : FRotator::ZeroRotator;
	// 회전 성분의 합 대신 최종 방향도 기록한다. 본 샘플의 프레임을 명시해 오래된 포즈와 구분한다.
	UE_LOG(LogTemp, Display, TEXT("[EnemyAimClient] Enemy=%s NetMode=%d Frame=%llu WorldTime=%.3f ServerTime=%.3f Phase=%s AimActive=%d ActorPitch=%.3f HasController=%d ControlPitch=%.3f BaseAimPitch=%.3f BaseFacingPitch=%.3f FacingRelative=%s FacingWorld=%s TiltRelative=%s TiltWorld=%s MeshRelative=%s MeshWorld=%s BodyValid=%d BodyFrame=%llu BodyFresh=%d BodyPitch=%.3f BodyWorld=%s BodyPosition=%s Origin=%s LastKnown=%s LastKnownPitch=%.3f Speed=%.2f ControllerRotatesPitch=%d"),
		*Drone->GetName(), int32(Drone->GetNetMode()), GFrameCounter, Now,
		GameState ? GameState->GetServerWorldTimeSeconds() : Now, *UEnum::GetValueAsString(Drone->GetAttackPhase()),
		IsAIAimPresentationActive(), Drone->GetActorRotation().Pitch, Controller != nullptr,
		Controller ? FRotator::NormalizeAxis(Controller->GetControlRotation().Pitch) : 0.0f,
		FRotator::NormalizeAxis(Drone->GetBaseAimRotation().Pitch), BaseAIFacingPitchRotation.Pitch,
		*Facing->GetRelativeRotation().ToCompactString(), *Facing->GetComponentRotation().ToCompactString(),
		*Tilt->GetRelativeRotation().ToCompactString(), *Tilt->GetComponentRotation().ToCompactString(),
		*Mesh->GetRelativeRotation().ToCompactString(), *Mesh->GetComponentRotation().ToCompactString(),
		DiagnosticBodyBone != INDEX_NONE, DiagnosticBoneFrame, DiagnosticBoneFrame == GFrameCounter, BodyForward.Pitch,
		*Body.GetRotation().Rotator().ToCompactString(), *Body.GetLocation().ToCompactString(),
		*Origin.ToCompactString(), *LastKnown.ToCompactString(), (LastKnown - Origin).Rotation().Pitch,
		Drone->GetVelocity().Size(), Drone->bUseControllerRotationPitch);
}

void UVECDroneMovementComponent::ApplyDroneMoveSpeed(float NewMoveSpeed)
{
	const float ClampedMoveSpeed = FMath::Max(NewMoveSpeed, 0.0f);
	SetMoveSpeed(ClampedMoveSpeed);
	SetBoostSpeed(ClampedMoveSpeed);
	SetVerticalSpeed(ClampedMoveSpeed);
}

ACharacter* UVECDroneMovementComponent::GetFlightOwnerCharacter() const
{
	return GetVECDroneOwner();
}

USceneComponent* UVECDroneMovementComponent::GetFlightVisualTiltRoot() const
{
	const AVECDrone* Drone = GetVECDroneOwner();
	return Drone ? Drone->GetThirdPersonTiltRoot() : nullptr;
}

USceneComponent* UVECDroneMovementComponent::GetFlightViewModelRoot() const
{
	const AVECDrone* Drone = GetVECDroneOwner();
	return Drone ? Drone->GetFirstPersonViewModelRoot() : nullptr;
}

bool UVECDroneMovementComponent::CanRunInputMovement() const
{
	const AVECDrone* Drone = GetVECDroneOwner();
	if (!Drone)
	{
		return false;
	}

	return Drone->HasAuthority()
		? Drone->IsEnemyPossessed() && !Drone->IsPossessedImpactInputLocked()
		: Drone->IsLocallyControlled() && !Drone->IsPossessedImpactInputLocked();
}

void UVECDroneMovementComponent::ResetPoolMovementPresentation()
{
	ResetMovementFeel();
	NoiseFacingTiltWeight = 0.0f;
	if (AVECDrone* Drone = GetVECDroneOwner())
	{
		if (bAIFacingPitchInitialized && Drone->GetAIFacingPitchRoot())
			Drone->GetAIFacingPitchRoot()->SetRelativeRotation(BaseAIFacingPitchRotation);
	}
	// 이동 기록과 함께 화면에 남은 기울기도 즉시 기본 자세로 돌린다.
	ApplyMeshTilt();
}

bool UVECDroneMovementComponent::ShouldUpdateMovementFeel() const
{
	const AVECDrone* Drone = GetVECDroneOwner();
	if (!Drone || Drone->GetNetMode() == NM_DedicatedServer
		|| (Drone->IsPoolManaged() && Drone->GetEnemyPoolState() != EEnemyPoolState::CombatActive))
	{
		return false;
	}

	if (Drone->IsEnemyPossessed() && Drone->IsLocallyControlled())
	{
		return true;
	}

	return Drone->WasRecentlyRendered();
}

bool UVECDroneMovementComponent::IsAIAimPresentationActive() const
{
	const AVECDrone* Drone = GetVECDroneOwner();
	return Drone && !Drone->IsEnemyPossessed()
		&& Drone->GetRuntimeStat().Type == EEnemyType::Gun
		&& Drone->GetAttackPhase() == EEnemyAttackPhase::Firing;
}

bool UVECDroneMovementComponent::ShouldApplyMeshPitchTilt() const
{
	// 사격 중에는 상위 조준 축이 Pitch를 전담한다. 이동/반동 기울기를 더하면 총구 외형이 아래로 벗어난다.
	// 내부 기울기 계산은 계속 진행해 사격 종료 후 현재 비행 상태의 연출로 복귀한다.
	return !IsAIAimPresentationActive();
}

EFlightInputMode UVECDroneMovementComponent::GetFlightInputMode() const
{
	return EFlightInputMode::Free;
}

void UVECDroneMovementComponent::OnAfterInputMovement(float DeltaTime)
{
	Super::OnAfterInputMovement(DeltaTime);

	// 빙의 중에는 공용 StateTree 대신 Pawn이 짧은 물리 반동 감쇠를 직접 갱신한다.
	if (AVECDrone* Drone = GetVECDroneOwner())
	{
		Drone->UpdatePossessedImpactRecovery(DeltaTime);
	}

	if (ShouldUpdateMovementFeel())
	{
		UpdateAIFacingPitch(DeltaTime);
	}
}

void UVECDroneMovementComponent::UpdateAIFacingPitch(float DeltaTime)
{
	AVECDrone* Drone = GetVECDroneOwner();
	USceneComponent* PitchRoot = Drone ? Drone->GetAIFacingPitchRoot() : nullptr;
	if (!Drone || !PitchRoot)
	{
		return;
	}

	// 컴포넌트가 붙어 있던 최초 상대 회전값(에디터에서 설정한 오프셋)을 기준선으로 잡아두고,
	// 이후로는 그 기준선에 목표 Pitch만 더한다 — 오프셋을 매 프레임 덮어쓰지 않기 위함.
	if (!bAIFacingPitchInitialized)
	{
		BaseAIFacingPitchRotation = PitchRoot->GetRelativeRotation();
		bAIFacingPitchInitialized = true;
	}

	// GetBaseAimRotation()은 빙의 중엔 플레이어 카메라 Pitch, AI 주행 중엔 AIController가
	// RotateTowardLocation 등으로 설정한 ControlRotation.Pitch를 반환한다 — 즉 "누가 조종하든"
	// 같은 소스에서 Pitch를 읽어 이 회전축에 반영할 수 있다.
	// 슈터를 겨냥할 때만 하향 몸통 각도를 줄인다. 탄도와 다른 대상·상향 조준 자세는 유지한다.
	// 대상 구분은 서버에서 복제하므로 AIController가 없는 클라이언트도 같은 외형을 표시한다.
	const bool bScaleGunBodyPitch = !Drone->IsEnemyPossessed() && Drone->GetRuntimeStat().Type == EEnemyType::Gun
		&& Drone->IsAimingAtShooter() && !Drone->IsNoiseDirectionFacing();
	float DesiredPitch = FMath::ClampAngle(
		Drone->GetBaseAimRotation().Pitch,
		-89.0f,
		89.0f
	);
	if (bScaleGunBodyPitch && DesiredPitch < 0.0f)
	{
		DesiredPitch *= FMath::Clamp(CVarGunDroneBodyDownPitchScale.GetValueOnGameThread(), 0.0f, 1.0f);
	}
	// 이동 기울기를 즉시 없애면 확인 진입/종료 때 자세가 튄다. 기존 축은 유지하고 점진적으로 상쇄한다.
	NoiseFacingTiltWeight = FMath::FInterpTo(NoiseFacingTiltWeight, Drone->IsNoiseDirectionFacing() ? 1.0f : 0.0f,
		DeltaTime, AIFacingPitchInterpSpeed);
	if (const USceneComponent* TiltRoot = Drone->GetThirdPersonTiltRoot())
		DesiredPitch -= NoiseFacingTiltWeight * FRotator::NormalizeAxis(TiltRoot->GetRelativeRotation().Pitch - BaseNoiseFacingTiltPitch);
	FRotator TargetRotation = BaseAIFacingPitchRotation;
	TargetRotation.Pitch = FRotator::NormalizeAxis(
		BaseAIFacingPitchRotation.Pitch + DesiredPitch
	);

	// 사격 중에는 보정된 외형 목표각을 즉시 적용한다. 클라이언트도 복제된 Phase와 조준각을 사용한다.
	if (IsAIAimPresentationActive())
	{
		PitchRoot->SetRelativeRotation(TargetRotation);
		return;
	}

	PitchRoot->SetRelativeRotation(FMath::RInterpTo(
		PitchRoot->GetRelativeRotation(),
		TargetRotation,
		DeltaTime,
		AIFacingPitchInterpSpeed
	));
}

AVECDrone* UVECDroneMovementComponent::GetVECDroneOwner() const
{
	return Cast<AVECDrone>(GetOwner());
}
