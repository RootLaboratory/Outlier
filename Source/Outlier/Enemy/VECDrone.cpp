#include "Enemy/VECDrone.h"
#include "Audio/OutlierAudioFeedback.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Drone/DroneInputConfig.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputActionValue.h"
#include "Enemy/VECDroneMovementComponent.h"
#include "Outlier.h"

namespace
{
	void CollectSocketsByPrefix(
		const USkeletalMeshComponent* MeshComponent,
		FName SocketPrefix,
		TArray<FName>& OutSocketNames)
	{
		if (!MeshComponent || SocketPrefix.IsNone())
		{
			return;
		}

		const FString PrefixString = SocketPrefix.ToString();
		TArray<FName> MatchingSockets;
		for (const FName SocketName : MeshComponent->GetAllSocketNames())
		{
			const FString SocketString = SocketName.ToString();
			if (SocketName == SocketPrefix || SocketString.StartsWith(PrefixString))
			{
				MatchingSockets.Add(SocketName);
			}
		}

		MatchingSockets.Sort(
			[](const FName& Left, const FName& Right)
			{
				return Left.LexicalLess(Right);
			});

		OutSocketNames.Append(MatchingSockets);
	}
}

AVECDrone::AVECDrone()
{
	SetDefaultEnemyType(EEnemyType::Gun);
	HitAudioContext = OutlierAudioFeedback::Tag(TEXT("Audio.Context.Enemy.Drone.Gun.State.Hit"));
	DeathAudioContext = OutlierAudioFeedback::Tag(TEXT("Audio.Context.Enemy.Drone.Gun.State.Death"));
	DetectAudioContext = OutlierAudioFeedback::Tag(TEXT("Audio.Context.Enemy.Drone.Gun.State.Detect"));
	HoverAudioContext = OutlierAudioFeedback::Tag(TEXT("Audio.Context.Enemy.Drone.Gun.Hovering.Loop"));
	AppearAudioContext = OutlierAudioFeedback::Tag(TEXT("Audio.Context.Enemy.Drone.Gun.State.Appear"));

	bUseControllerRotationPitch = false;

	// Capsule -> AIFacingPitchRoot -> ThirdPersonTiltRoot -> Mesh 순으로 계층을 둔다.
	// Capsule 자체는 Yaw만 회전하므로(이동/충돌용), AI가 플레이어를 바라볼 때의 Pitch는
	// 이 컴포넌트 하나에만 적용해 3인칭 메시가 고개를 드는 것처럼 보이게 한다. 관성 틸트용
	// ThirdPersonTiltRoot와 축을 분리해둬야 두 회전이 서로 덮어쓰지 않는다.
	AIFacingPitchRoot = CreateDefaultSubobject<USceneComponent>(TEXT("AI Facing Pitch Root"));
	AIFacingPitchRoot->SetupAttachment(GetCapsuleComponent());

	ThirdPersonTiltRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Third Person Tilt Root"));
	ThirdPersonTiltRoot->SetupAttachment(AIFacingPitchRoot);
	GetMesh()->SetupAttachment(ThirdPersonTiltRoot);

	FirstPersonCameraRoot = CreateDefaultSubobject<USceneComponent>(TEXT("First Person Camera Root"));
	FirstPersonCameraRoot->SetupAttachment(GetCapsuleComponent());
	FirstPersonCameraRoot->SetRelativeLocation(FVector(0.0f, 0.0f, 64.0f));

	EnemyCameraComponent->SetupAttachment(FirstPersonCameraRoot);
	EnemyCameraComponent->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
	EnemyCameraComponent->bUsePawnControlRotation = true;
	EnemyCameraComponent->bEnableFirstPersonFieldOfView = true;
	EnemyCameraComponent->bEnableFirstPersonScale = false;
	EnemyCameraComponent->FirstPersonFieldOfView = 70.0f;
	EnemyCameraComponent->FirstPersonScale = 1.0f;

	FirstPersonViewModelRoot = CreateDefaultSubobject<USceneComponent>(TEXT("First Person ViewModel Root"));
	FirstPersonViewModelRoot->SetupAttachment(EnemyCameraComponent);
	FirstPersonViewModelRoot->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);

	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Mesh"));
	FirstPersonMesh->SetupAttachment(FirstPersonViewModelRoot);
	FirstPersonMesh->SetOnlyOwnerSee(true);
	FirstPersonMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	FirstPersonMesh->SetRelativeRotation(FRotator::ZeroRotator);
	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));

	GetMesh()->SetOwnerNoSee(true);
	GetMesh()->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::WorldSpaceRepresentation;

	VECMovementComponent = CreateDefaultSubobject<UVECDroneMovementComponent>(TEXT("VECMovementComponent"));
}

void AVECDrone::BeginPlay()
{
	// Blueprint 인스턴스가 네이티브 생성자의 부착 관계를 런타임에 다시 틀어놓는 경우가 있어
	// (기존 ThirdPersonTiltRoot/Mesh 재부착 로직과 동일한 이유), BeginPlay 시점에 의도한
	// Capsule -> AIFacingPitchRoot -> ThirdPersonTiltRoot -> Mesh 계층을 다시 한번 강제한다.
	if (AIFacingPitchRoot && AIFacingPitchRoot->GetAttachParent() != GetCapsuleComponent())
	{
		AIFacingPitchRoot->AttachToComponent(
			GetCapsuleComponent(),
			FAttachmentTransformRules::KeepRelativeTransform
		);
	}

	if (ThirdPersonTiltRoot && ThirdPersonTiltRoot->GetAttachParent() != AIFacingPitchRoot)
	{
		ThirdPersonTiltRoot->AttachToComponent(
			AIFacingPitchRoot,
			FAttachmentTransformRules::KeepRelativeTransform
		);
	}

	if (ThirdPersonTiltRoot && GetMesh()->GetAttachParent() != ThirdPersonTiltRoot)
	{
		GetMesh()->AttachToComponent(
			ThirdPersonTiltRoot,
			FAttachmentTransformRules::KeepRelativeTransform
		);
	}

	Super::BeginPlay();
}

float AVECDrone::ReceiveOutlierDamage(const FOutlierDamageRequest& Request)
{
	// 피해·스턴·사망은 기존 경로에서 한 번만 처리한다. 반환 뒤에는 이미 사망했을 수도 있다.
	const float AppliedDamage = Super::ReceiveOutlierDamage(Request);
	if (!HasAuthority() || AppliedDamage <= 0.0f || !CanPlayHitReaction())
	{
		return AppliedDamage;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	if (Now >= NextHitReactionTime)
	{
		NextHitReactionTime = Now + FMath::Max(HitReactionMinInterval, 0.05f);
		MulticastPlayHitReaction(GetPoolGameplayGeneration(), GetPoolLeaseSerial(), ++HitReactionSerial);
	}
	return AppliedDamage;
}

bool AVECDrone::CanPlayHitReaction() const
{
	return IsValid(this) && !IsActorBeingDestroyed() && HitReactionMontage
		&& !IsDead() && GetCurrentHealth() > 0.0f
		&& (!IsPoolManaged() || GetEnemyPoolState() == EEnemyPoolState::CombatActive)
		&& !HasActiveStunTag() && GetCombatState() != EEnemyCombatState::Stun
		&& !IsPossessionInProgress() && !IsImpactReactionActive();
}

bool AVECDrone::AcceptHitReactionMessage(int32 Generation, int32 Lease, int64 Serial)
{
	if (Serial <= LastReceivedHitReactionSerial)
	{
		return false;
	}
	LastReceivedHitReactionSerial = Serial;
	return Generation == GetPoolGameplayGeneration() && Lease == GetPoolLeaseSerial();
}

void AVECDrone::MulticastPlayHitReaction_Implementation(int32 Generation, int32 Lease, int64 Serial)
{
	if (!AcceptHitReactionMessage(Generation, Lease, Serial)
		|| GetNetMode() == NM_DedicatedServer || !CanPlayHitReaction())
	{
		return;
	}

	UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance || AnimInstance->GetInstanceForMontage(HitReactionMontage))
	{
		return;
	}

	// bStopAllMontages=false여도 같은 그룹은 중단된다. 다른 동작이 사용 중이면 작은 피격을 생략한다.
	for (const FAnimMontageInstance* Instance : AnimInstance->MontageInstances)
	{
		if (Instance && Instance->Montage && Instance->IsActive()
			&& Instance->Montage->GetGroupName() == HitReactionMontage->GetGroupName())
		{
			return;
		}
	}

	if (AnimInstance->Montage_Play(HitReactionMontage, 1.0f,
		EMontagePlayReturnType::MontageLength, 0.0f, false) > 0.0f)
	{
		PresentationGeneration = Generation;
		PresentationLease = Lease;
	}
}

void AVECDrone::StopHitReaction(float BlendOutTime)
{
	if (!HitReactionMontage || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	FAnimMontageInstance* Instance = AnimInstance ? AnimInstance->GetInstanceForMontage(HitReactionMontage) : nullptr;
	if (!Instance)
	{
		return;
	}

	// Montage_Stop은 이미 Blend Out 중인 인스턴스를 찾지 못한다.
	// 사망·풀 반환의 즉시 중단은 이전 짧은 Blend Out도 0초로 줄여야 한다.
	FMontageBlendSettings Settings;
	Settings.Blend = HitReactionMontage->BlendOut;
	Settings.Blend.BlendTime = BlendOutTime;
	Settings.BlendMode = HitReactionMontage->BlendModeOut;
	Settings.BlendProfile = HitReactionMontage->BlendProfileOut;
	Instance->Stop(Settings);
}

void AVECDrone::InterruptDamagePresentation(float BlendOutTime)
{
	if (HasAuthority() && HitReactionMontage)
	{
		// 재생은 누락되어도 되지만 상태 전환의 중단은 전달한다. 순번으로 역순 도착도 방어한다.
		MulticastStopHitReaction(GetPoolGameplayGeneration(), GetPoolLeaseSerial(), ++HitReactionSerial, BlendOutTime);
	}
	else
	{
		StopHitReaction(BlendOutTime);
	}
}

void AVECDrone::MulticastStopHitReaction_Implementation(
	int32 Generation, int32 Lease, int64 Serial, float BlendOutTime)
{
	if (AcceptHitReactionMessage(Generation, Lease, Serial))
	{
		StopHitReaction(BlendOutTime);
	}
}

bool AVECDrone::ShouldResetPoolMontages() const
{
	// RPC가 CombatActive의 OnRep보다 먼저 도착해도 같은 수명의 새 피격을 복원 과정에서 끊지 않는다.
	const UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	return !HitReactionMontage || !AnimInstance || !AnimInstance->GetInstanceForMontage(HitReactionMontage)
		|| GetEnemyPoolState() != EEnemyPoolState::CombatActive
		|| PresentationGeneration != GetPoolGameplayGeneration()
		|| PresentationLease != GetPoolLeaseSerial();
}

void AVECDrone::ResetPoolPresentationState()
{
	Super::ResetPoolPresentationState();
	if (PresentationGeneration != GetPoolGameplayGeneration() || PresentationLease != GetPoolLeaseSerial())
	{
		StopHitReaction(0.0f);
		NextHitReactionTime = 0.0;
		PresentationGeneration = GetPoolGameplayGeneration();
		PresentationLease = GetPoolLeaseSerial();
	}
}

void AVECDrone::OnPoolStateApplied(EEnemyPoolState PreviousState)
{
	Super::OnPoolStateApplied(PreviousState);
	if (GetEnemyPoolState() != EEnemyPoolState::CombatActive)
	{
		InterruptDamagePresentation(0.0f);
		NextHitReactionTime = 0.0;
	}
}

void AVECDrone::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopHitReaction(0.0f);
	NextHitReactionTime = 0.0;
	Super::EndPlay(EndPlayReason);
}

void AVECDrone::PostNetReceive()
{
	Super::PostNetReceive();
	// 빠른 반환·재대여에서는 중간 PoolState가 생략될 수 있어 Lease 변경도 직접 확인한다.
	if (IsPoolManaged() && (PresentationGeneration != GetPoolGameplayGeneration()
		|| PresentationLease != GetPoolLeaseSerial()))
	{
		ResetPoolPresentationState();
	}
}

void AVECDrone::ApplyExplosionReactionPresentation(const FVector& Direction, float ReactionScale)
{
	if (VECMovementComponent)
	{
		VECMovementComponent->ApplyExternalImpactTilt(
			Direction,
			ReactionScale,
			RuntimeImpactReactionProfile.MaxMeshImpactTiltDegrees,
			RuntimeImpactReactionProfile.MeshImpactTiltInterpSpeed);
	}

	Super::ApplyExplosionReactionPresentation(Direction, ReactionScale);
}

void AVECDrone::HideSourceMeshes()
{
	Super::HideSourceMeshes();

	if (FirstPersonMesh)
	{
		SaveSourceMeshVisibility(FirstPersonMesh);
		FirstPersonMesh->SetVisibility(false, true);
		FirstPersonMesh->SetHiddenInGame(true, true);
	}
}

float AVECDrone::GetCurrentCameraPitchDegrees() const
{
	return VECMovementComponent
		? VECMovementComponent->GetCurrentCameraPitchDegrees()
		: 0.0f;
}

float AVECDrone::GetCurrentCameraRollDegrees() const
{
	return VECMovementComponent
		? VECMovementComponent->GetCurrentCameraRollDegrees()
		: 0.0f;
}

float AVECDrone::GetMaxCameraPitchDegrees() const
{
	return VECMovementComponent
		? FMath::Max(VECMovementComponent->GetCameraPitchOnMove(), 0.0f)
		: 0.0f;
}

float AVECDrone::GetMaxCameraRollDegrees() const
{
	return VECMovementComponent
		? FMath::Max(VECMovementComponent->GetCameraRollOnTurn(), 0.0f)
		: 0.0f;
}

void AVECDrone::GetWeaponMuzzleSocketNames(bool bFirstPerson, TArray<FName>& OutSocketNames) const
{
	const FName SocketPrefix = GetWeaponMuzzleSocketName(bFirstPerson);
	CollectSocketsByPrefix(GetWeaponMuzzleComponent(bFirstPerson), SocketPrefix, OutSocketNames);

	if (OutSocketNames.IsEmpty() && !SocketPrefix.IsNone())
	{
		OutSocketNames.Add(SocketPrefix);
	}
}

void AVECDrone::UnPossessed()
{
	if (VECMovementComponent)
	{
		VECMovementComponent->ClearFlightInput();
	}

	Super::UnPossessed();
}

void AVECDrone::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (VECMovementComponent)
	{
		VECMovementComponent->ClearFlightInput();
	}

	UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInputComponent || !InputConfig)
	{
		if (IsPossessedAttackDiagnosticsEnabled())
		{
			UE_LOG(
				LogOutlier,
				Error,
				TEXT("[EnemyPossessedAttackDiag] InputBindingFailed Enemy=%s EnhancedInput=%s InputConfig=%s"),
				*GetNameSafe(this),
				EnhancedInputComponent ? TEXT("true") : TEXT("false"),
				*GetNameSafe(InputConfig));
		}
		return;
	}

	EnhancedInputComponent->BindAction(InputConfig->MoveAction, ETriggerEvent::Triggered, this, &AVECDrone::MoveInput);
	EnhancedInputComponent->BindAction(InputConfig->MoveAction, ETriggerEvent::Completed, this, &AVECDrone::MoveInput);
	EnhancedInputComponent->BindAction(InputConfig->LookAction, ETriggerEvent::Triggered, this, &AVECDrone::LookInput);
	EnhancedInputComponent->BindAction(InputConfig->VerticalMoveAction, ETriggerEvent::Triggered, this, &AVECDrone::VerticalMove);
	EnhancedInputComponent->BindAction(InputConfig->VerticalMoveAction, ETriggerEvent::Completed, this, &AVECDrone::StopVerticalMove);

	// VEC은 AFirstPersonCharacter 계층이 아니므로 공격 입력을 별도로 연결한다.
	// 시작/종료만 AEnemyBase RPC 경로로 보내며 Canceled도 종료로 처리해 입력 전환 중 연사가 남지 않게 한다.
	EnhancedInputComponent->BindAction(InputConfig->AttackAction, ETriggerEvent::Started, this, &AVECDrone::StartAttackInput);
	EnhancedInputComponent->BindAction(InputConfig->AttackAction, ETriggerEvent::Completed, this, &AVECDrone::StopAttackInput);
	EnhancedInputComponent->BindAction(InputConfig->AttackAction, ETriggerEvent::Canceled, this, &AVECDrone::StopAttackInput);
	if (IsPossessedAttackDiagnosticsEnabled())
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[EnemyPossessedAttackDiag] InputBound Enemy=%s InputConfig=%s AttackAction=%s"),
			*GetNameSafe(this),
			*GetNameSafe(InputConfig),
			*GetNameSafe(InputConfig->AttackAction));
	}
}

void AVECDrone::ApplyMovementFromRuntimeStat()
{
	Super::ApplyMovementFromRuntimeStat();

	if (UCharacterMovementComponent* MovementComponent = GetCharacterMovement())
	{
		MovementComponent->SetMovementMode(MOVE_Flying);
		// ACharacter::Restart()가 SetDefaultMovementMode()를 호출하면서 빙의 전환 시
		// MovementMode를 DefaultLandMovementMode로 되돌리는데, 이걸 안 해두면 빙의 해제 후 드론이 추락함
		MovementComponent->DefaultLandMovementMode = MOVE_Flying;
		MovementComponent->MaxFlySpeed = FMath::Max(RuntimeStat.MoveSpeed, 0.0f);
		MovementComponent->BrakingDecelerationFlying = MovementComponent->BrakingDecelerationWalking;
		MovementComponent->GravityScale = 0.0f;
	}

	if (VECMovementComponent)
	{
		VECMovementComponent->ApplyDroneMoveSpeed(RuntimeStat.MoveSpeed);
	}
}

void AVECDrone::MoveInput(const FInputActionValue& Value)
{
	if (!VECMovementComponent || IsPossessedActionCommitted())
	{
		return;
	}

	VECMovementComponent->SetMoveInput(Value.Get<FVector2D>());
}

void AVECDrone::LookInput(const FInputActionValue& Value)
{
	if (!Controller || IsPossessedActionCommitted())
	{
		return;
	}

	const FVector2D LookAxis = Value.Get<FVector2D>();
	if (LookAxis.SizeSquared() < FMath::Square(LookInputDeadZone))
	{
		return;
	}

	const float YawInput = LookAxis.X * LookSensitivity;
	const float PitchInput = -LookAxis.Y * LookSensitivity;

	AddControllerYawInput(YawInput);

	FRotator ControlRot = Controller->GetControlRotation();
	ControlRot.Pitch = FMath::ClampAngle(
		ControlRot.Pitch - PitchInput,
		PitchMin,
		PitchMax
	);
	Controller->SetControlRotation(ControlRot);
}

void AVECDrone::VerticalMove(const FInputActionValue& Value)
{
	if (VECMovementComponent && !IsPossessedActionCommitted())
	{
		VECMovementComponent->SetVerticalInput(Value.Get<float>());
	}
}

void AVECDrone::StopVerticalMove()
{
	if (VECMovementComponent)
	{
		VECMovementComponent->SetVerticalInput(0.0f);
	}
}

void AVECDrone::StartAttackInput()
{
	if (IsPossessedActionCommitted())
	{
		if (IsPossessedAttackDiagnosticsEnabled())
		{
			UE_LOG(LogOutlier, Warning, TEXT("[EnemyPossessedAttackDiag] VECStartInputIgnored Enemy=%s Reason=Committed"), *GetNameSafe(this));
		}
		return;
	}

	if (IsPossessedAttackDiagnosticsEnabled())
	{
		UE_LOG(LogOutlier, Warning, TEXT("[EnemyPossessedAttackDiag] VECStartInputForwarded Enemy=%s"), *GetNameSafe(this));
	}

	// 부모의 보호된 입력 경로를 거쳐 클라이언트 RPC와 서버 권한 검증을 동일하게 사용한다.
	HandleStartAttackInput();
}

void AVECDrone::StopAttackInput()
{
	// 입력 해제도 부모 경로로 전달해 서버의 CurrentWeapon만 공격을 종료하게 한다.
	HandleStopAttackInput();
}
