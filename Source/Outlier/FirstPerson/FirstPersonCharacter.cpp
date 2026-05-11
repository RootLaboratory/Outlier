// Fill out your copyright notice in the Description page of Project Settings.


#include "FirstPersonCharacter.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "LocalPlayerUISubSystem.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Weapon/WeaponBase.h"
#include "Net/UnrealNetwork.h"
#include "OutlierNetUtils.h"
#include "Outlier.h"
#include "Shooter/ShooterCharacter.h"


// Sets default values
AFirstPersonCharacter::AFirstPersonCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.0f, 96.0f);

	FirstPersonCameraRoot = CreateDefaultSubobject<USceneComponent>(TEXT("First Person Camera Root"));
	FirstPersonCameraRoot->SetupAttachment(GetCapsuleComponent());
	FirstPersonCameraRoot->SetRelativeLocation(FVector(0.0f, 0.0f, 64.0f));

	// Create Camera
	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("First Person Camera"));
	FirstPersonCamera->SetupAttachment(FirstPersonCameraRoot);
	FirstPersonCamera->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
	FirstPersonCamera->bUsePawnControlRotation = true;
	FirstPersonCamera->bEnableFirstPersonFieldOfView = true;
	FirstPersonCamera->bEnableFirstPersonScale = false;
	FirstPersonCamera->FirstPersonFieldOfView = 70.0f;
	FirstPersonCamera->FirstPersonScale = 1.0f;

	FirstPersonViewModelRoot = CreateDefaultSubobject<USceneComponent>(TEXT("First Person ViewModel Root"));
	FirstPersonViewModelRoot->SetupAttachment(FirstPersonCamera);
	FirstPersonViewModelRoot->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
	FirstPersonViewModelRoot->SetRelativeRotation(FRotator::ZeroRotator);

	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Mesh"));
	FirstPersonMesh->SetupAttachment(FirstPersonViewModelRoot);
	FirstPersonMesh->SetOnlyOwnerSee(true);
	FirstPersonMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	FirstPersonMesh->SetRelativeLocation(FVector(-2.0f, 0.0f, -130.0f));
	FirstPersonMesh->SetRelativeRotation(FRotator::ZeroRotator);
	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));

	// configure the character comps
	GetMesh()->SetOwnerNoSee(true);
	GetMesh()->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::WorldSpaceRepresentation;
	GetMesh()->SetWorldLocation(FVector(0, 0, -GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));

	GetCapsuleComponent()->SetCapsuleSize(34.0f, 96.0f);

	// configure character movement
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;
	GetCharacterMovement()->AirControl = 0.5f;
}

// Called to bind functionality to input
void AFirstPersonCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Move
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AFirstPersonCharacter::MoveInput);
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Completed, this, &AFirstPersonCharacter::MoveInput);

		// Look
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AFirstPersonCharacter::LookInput);

		// Attack
		EnhancedInputComponent->BindAction(AttackAction, ETriggerEvent::Started,   this, &AFirstPersonCharacter::TryStartAttack);
		EnhancedInputComponent->BindAction(AttackAction, ETriggerEvent::Completed, this, &AFirstPersonCharacter::TryStopAttack);


		EnhancedInputComponent->BindAction(CamToggleAction, ETriggerEvent::Started, this, &AFirstPersonCharacter::TryCamToggle);
	}
}

void AFirstPersonCharacter::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(
		LogTemp,
		Log,
		TEXT("%s %s FPBeginPlay FirstPersonMesh=%s MeshAnimClass=%s MeshAnimInstance=%s"),
		OutlierNet::GetNetPrefix(this),
		*GetName(),
		*GetNameSafe(FirstPersonMesh),
		FirstPersonMesh ? *GetNameSafe(FirstPersonMesh->GetAnimClass()) : TEXT("None"),
		FirstPersonMesh ? *GetNameSafe(FirstPersonMesh->GetAnimInstance()) : TEXT("None"));
}

void AFirstPersonCharacter::MoveInput(const FInputActionValue& Value)
{
	// get the Vector2D move axis
	const FVector2D MovementVector = Value.Get<FVector2D>();

	DoMove(MovementVector.X, MovementVector.Y);

	OnMoveInputUpdated(MovementVector);
}

void AFirstPersonCharacter::LookInput(const FInputActionValue& Value)
{
	// get the Vector2D move axis
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// 카메라 기준이라 Y를 뒤집음
	DoAim(LookAxisVector.X, -LookAxisVector.Y);
}

void AFirstPersonCharacter::DoMove(float Right, float Forward)
{
	if (const AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(this))
	{
		if (ShooterCharacter->GetMovementState() == EMovementState::Slide)
		{
			return;
		}
	}

	if (GetController())
	{
		// move inputs
		AddMovementInput(GetActorRightVector(), Right);
		AddMovementInput(GetActorForwardVector(), Forward);
	}
}

void AFirstPersonCharacter::DoAim(float Yaw, float Pitch)
{
	if (GetController())
	{
		// rotation inputs
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void AFirstPersonCharacter::TryCamToggle()
{
	if (!IsLocallyControlled())
	{
		return;
	}

	//UE_LOG(LogTemp, Error, TEXT("Toggle"));
	APlayerController* PlayerController = Cast<APlayerController>(GetController());

	if (PlayerController)
	{
		if (ULocalPlayer* LP = PlayerController->GetLocalPlayer())
		{
			if (ULocalPlayerUISubSystem* PPSubsystem = LP->GetSubsystem<ULocalPlayerUISubSystem>())
			{

				PPSubsystem->PartnerCameraToggle();
			}
		}
	}
}

void AFirstPersonCharacter::OnRep_CurrentWeapon()
{
	UE_LOG(LogTemp, Log, TEXT("%s %s OnRep_CurrentWeapon Previous=%s Current=%s"), OutlierNet::GetNetPrefix(this), *GetName(), *GetNameSafe(LastReplicatedWeapon), *GetNameSafe(CurrentWeapon));
	CurrentWeaponType = CurrentWeapon ? CurrentWeapon->GetWeaponType() : EWeaponType::Unarmed;
	LastReplicatedWeapon = CurrentWeapon;
}

void AFirstPersonCharacter::TryStartAttack()
{
	UE_LOG(LogTemp, Log, TEXT("FirstPerson"));
	if (!CurrentWeapon)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s %s TryStartAttack blocked: no weapon equipped"), OutlierNet::GetNetPrefix(this), *GetName());
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("%s %s TryStartAttack Weapon=%s"), OutlierNet::GetNetPrefix(this), *GetName(), *GetNameSafe(CurrentWeapon));
	CurrentWeapon->StartAttack();
}

void AFirstPersonCharacter::TryStopAttack()
{
	if (!CurrentWeapon)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s %s TryStopAttack blocked: no weapon equipped"), OutlierNet::GetNetPrefix(this), *GetName());
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("%s %s TryStopAttack Weapon=%s"), OutlierNet::GetNetPrefix(this), *GetName(), *GetNameSafe(CurrentWeapon));
	CurrentWeapon->StopAttack();
}

void AFirstPersonCharacter::EquipWeapon(AWeaponBase* Weapon)
{
	if (CurrentWeapon == Weapon)
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("%s %s EquipWeapon skipped: already equipped %s"),
			OutlierNet::GetNetPrefix(this),
			*GetName(),
			*GetNameSafe(Weapon)
		);
		return;
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("%s %s EquipWeapon Previous=%s New=%s"),
		OutlierNet::GetNetPrefix(this),
		*GetName(),
		*GetNameSafe(CurrentWeapon),
		*GetNameSafe(Weapon)
	);

	if (CurrentWeapon && CurrentWeapon->GetOwner() == this)
	{
		CurrentWeapon->OnUnequipped();
	}

	CurrentWeapon = Weapon;
	CurrentWeaponType = CurrentWeapon ? CurrentWeapon->GetWeaponType() : EWeaponType::Unarmed;

	if (CurrentWeapon)
	{
		CurrentWeapon->OnEquipped(this);
	}

	LastReplicatedWeapon = CurrentWeapon;
	ForceNetUpdate();

	UE_LOG(LogTemp, Log, TEXT("%s %s EquipWeapon complete Current=%s"), OutlierNet::GetNetPrefix(this), *GetName(), *GetNameSafe(CurrentWeapon));
}

void AFirstPersonCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFirstPersonCharacter, CurrentWeapon);
}

EWeaponType AFirstPersonCharacter::GetWeaponType() const
{
	return CurrentWeaponType;
}

void AFirstPersonCharacter::OnMoveInputUpdated(const FVector2D& MoveValue)
{
}
