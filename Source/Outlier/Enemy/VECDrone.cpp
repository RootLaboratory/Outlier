#include "Enemy/VECDrone.h"

#include "Drone/DroneInputConfig.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputActionValue.h"
#include "Enemy/VECDroneMovementComponent.h"

AVECDrone::AVECDrone()
{
	SetDefaultEnemyType(EEnemyType::Gun);
	VECMovementComponent = CreateDefaultSubobject<UVECDroneMovementComponent>(TEXT("VECMovementComponent"));
}

void AVECDrone::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

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
