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
