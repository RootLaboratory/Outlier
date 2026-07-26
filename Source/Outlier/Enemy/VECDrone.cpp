#include "Enemy/VECDrone.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Drone/DroneInputConfig.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputActionValue.h"
#include "Enemy/VECDroneMovementComponent.h"

AVECDrone::AVECDrone()
{
	SetDefaultEnemyType(EEnemyType::Gun);
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
	FirstPersonMesh->SetRelativeLocation(FVector(-2.0f, 0.0f, -130.0f));
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
	if (!VECMovementComponent)
	{
		return;
	}

	VECMovementComponent->SetMoveInput(Value.Get<FVector2D>());
}

void AVECDrone::LookInput(const FInputActionValue& Value)
{
	if (!Controller)
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
	if (VECMovementComponent)
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
	// 부모의 보호된 입력 경로를 거쳐 클라이언트 RPC와 서버 권한 검증을 동일하게 사용한다.
	HandleStartAttackInput();
}

void AVECDrone::StopAttackInput()
{
	// 입력 해제도 부모 경로로 전달해 서버의 CurrentWeapon만 공격을 종료하게 한다.
	HandleStopAttackInput();
}
