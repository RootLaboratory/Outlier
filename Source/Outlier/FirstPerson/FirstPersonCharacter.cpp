// Fill out your copyright notice in the Description page of Project Settings.


#include "FirstPersonCharacter.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Weapon/WeaponBase.h"
#include "Net/UnrealNetwork.h"
#include "Outlier.h"

namespace
{
	const TCHAR* NetPrefix(const AActor* Actor)
	{
		return (Actor && Actor->HasAuthority()) ? TEXT("[Server]") : TEXT("[Client]");
	}
}


// Sets default values
AFirstPersonCharacter::AFirstPersonCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.0f, 96.0f);

	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Mesh"));

	FirstPersonMesh->SetupAttachment(GetMesh());
	FirstPersonMesh->SetOnlyOwnerSee(true);
	FirstPersonMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));

	// Create Camera
	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("First Person Camera"));
	FirstPersonCamera->SetupAttachment(FirstPersonMesh, FName("head"));
	FirstPersonCamera->SetRelativeLocationAndRotation(FVector(-2.8f, 5.89f, 0.0f), FRotator(0.0f, 90.0f, -90.0f));
	FirstPersonCamera->bUsePawnControlRotation = true;
	FirstPersonCamera->bEnableFirstPersonFieldOfView = true;
	FirstPersonCamera->bEnableFirstPersonScale = true;
	FirstPersonCamera->FirstPersonFieldOfView = 70.0f;
	FirstPersonCamera->FirstPersonScale = 0.6f;

	// configure the character comps
	GetMesh()->SetOwnerNoSee(true);
	GetMesh()->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::WorldSpaceRepresentation;

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

		// Look
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AFirstPersonCharacter::LookInput);

		// Attack
		EnhancedInputComponent->BindAction(AttackAction, ETriggerEvent::Started,   this, &AFirstPersonCharacter::TryStartAttack);
		EnhancedInputComponent->BindAction(AttackAction, ETriggerEvent::Completed, this, &AFirstPersonCharacter::TryStopAttack);
	}
}

void AFirstPersonCharacter::MoveInput(const FInputActionValue& Value)
{
	// get the Vector2D move axis
	FVector2D MovementVector = Value.Get<FVector2D>();

	DoMove(MovementVector.X, MovementVector.Y);
}

void AFirstPersonCharacter::LookInput(const FInputActionValue& Value)
{
	// get the Vector2D move axis
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	DoAim(LookAxisVector.X, LookAxisVector.Y);
}

void AFirstPersonCharacter::DoMove(float Right, float Forward)
{
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

void AFirstPersonCharacter::OnRep_CurrentWeapon()
{
	UE_LOG(LogTemp, Log, TEXT("%s %s OnRep_CurrentWeapon Previous=%s Current=%s"), NetPrefix(this), *GetName(), *GetNameSafe(LastReplicatedWeapon), *GetNameSafe(CurrentWeapon));
	if (LastReplicatedWeapon && LastReplicatedWeapon != CurrentWeapon)
	{
		LastReplicatedWeapon->OnUnequipped();
	}

	if (CurrentWeapon)
	{
		CurrentWeapon->OnEquipped(this);
	}

	LastReplicatedWeapon = CurrentWeapon;
}

void AFirstPersonCharacter::TryStartAttack()
{
	if (!CurrentWeapon)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s %s TryStartAttack blocked: no weapon equipped"), NetPrefix(this), *GetName());
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("%s %s TryStartAttack Weapon=%s"), NetPrefix(this), *GetName(), *GetNameSafe(CurrentWeapon));
	CurrentWeapon->StartAttack();
}

void AFirstPersonCharacter::TryStopAttack()
{
	if (!CurrentWeapon)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s %s TryStopAttack blocked: no weapon equipped"), NetPrefix(this), *GetName());
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("%s %s TryStopAttack Weapon=%s"), NetPrefix(this), *GetName(), *GetNameSafe(CurrentWeapon));
	CurrentWeapon->StopAttack();
}

void AFirstPersonCharacter::EquipWeapon(AWeaponBase* Weapon)
{
	if (!Weapon)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s %s EquipWeapon blocked: weapon is null"), NetPrefix(this), *GetName());
		return;
	}

	if (CurrentWeapon == Weapon)
	{
		UE_LOG(LogTemp, Log, TEXT("%s %s EquipWeapon skipped: already equipped %s"), NetPrefix(this), *GetName(), *GetNameSafe(Weapon));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("%s %s EquipWeapon Previous=%s New=%s"), NetPrefix(this), *GetName(), *GetNameSafe(CurrentWeapon), *GetNameSafe(Weapon));

	if (CurrentWeapon)
	{
		CurrentWeapon->OnUnequipped();
	}

	CurrentWeapon = Weapon;
	CurrentWeaponType = Weapon->GetWeaponType();

	if (CurrentWeapon)
	{
		CurrentWeapon->OnEquipped(this);
	}

	LastReplicatedWeapon = CurrentWeapon;
	UE_LOG(LogTemp, Log, TEXT("%s %s EquipWeapon complete Current=%s"), NetPrefix(this), *GetName(), *GetNameSafe(CurrentWeapon));
}

void AFirstPersonCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFirstPersonCharacter, CurrentWeapon);
}
