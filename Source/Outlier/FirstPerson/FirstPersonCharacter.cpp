// Fill out your copyright notice in the Description page of Project Settings.


#include "FirstPersonCharacter.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "FirstPersonInputConfig.h"
#include "Interface/InteractableInterface.h"
#include "Interaction/InteractionNode.h"
#include "Interaction/InteractableComponent.h"
#include "LocalPlayerUISubSystem.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "Components/SceneCaptureComponent2D.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Weapon/WeaponBase.h"
#include "Net/UnrealNetwork.h"
#include "Engine/OverlapResult.h"
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

	//Capture Component
	CaptureComponent = CreateDefaultSubobject< USceneCaptureComponent2D>(TEXT("PartnerCameraCapture"));
	CaptureComponent->SetupAttachment(FirstPersonCamera);
}

// Called to bind functionality to input
void AFirstPersonCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInputComponent || !InputConfig) {
		UE_LOG(LogTemp, Warning, TEXT("EnhancedInputComponent or InputConfig is Null"));
		return;
	}

	// Move
	EnhancedInputComponent->BindAction(InputConfig->MoveAction, ETriggerEvent::Triggered, this, &AFirstPersonCharacter::MoveInput);
	EnhancedInputComponent->BindAction(InputConfig->MoveAction, ETriggerEvent::Completed, this, &AFirstPersonCharacter::MoveInput);

	// Look
	EnhancedInputComponent->BindAction(InputConfig->LookAction, ETriggerEvent::Triggered, this, &AFirstPersonCharacter::LookInput);

	// Attack
	EnhancedInputComponent->BindAction(InputConfig->AttackAction, ETriggerEvent::Started, this, &AFirstPersonCharacter::TryStartAttack);
	EnhancedInputComponent->BindAction(InputConfig->AttackAction, ETriggerEvent::Completed, this, &AFirstPersonCharacter::TryStopAttack);

	// Interaction
	EnhancedInputComponent->BindAction(InputConfig->InteractionAction, ETriggerEvent::Started, this, &AFirstPersonCharacter::TryInteract);
	EnhancedInputComponent->BindAction(InputConfig->InteractionAction, ETriggerEvent::Completed, this, &AFirstPersonCharacter::EndInteract);
	EnhancedInputComponent->BindAction(InputConfig->InteractionAction, ETriggerEvent::Canceled, this, &AFirstPersonCharacter::EndInteract);

	EnhancedInputComponent->BindAction(InputConfig->CamToggleAction, ETriggerEvent::Started, this, &AFirstPersonCharacter::TryCamToggle);
}

void AFirstPersonCharacter::BeginPlay()
{
	Super::BeginPlay();

	GetWorldTimerManager().SetTimer(
		InteractionTraceTimerHandle,
		this,
		&AFirstPersonCharacter::UpdateInteractableFocus,
		InteractionTraceInterval,
		true
	);

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

	APlayerController* PlayerController = Cast<APlayerController>(GetController());

	if (PlayerController)
	{	UE_LOG(LogTemp, Error, TEXT("PlayerController"));

		if (ULocalPlayer* LP = PlayerController->GetLocalPlayer())
		{
			UE_LOG(LogTemp, Error, TEXT("ULocalPlayer"));

			if (ULocalPlayerUISubSystem* PPSubsystem = LP->GetSubsystem<ULocalPlayerUISubSystem>())
			{
				PPSubsystem->PartnerCameraToggle();
				bPartnerCameraCaptureActive = !bPartnerCameraCaptureActive;
				SetPartnerCameraCaptureUpdating(bPartnerCameraCaptureActive);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("PPSubsystem"));

			}
		}
	}
}

void AFirstPersonCharacter::SetPartnerCameraCaptureUpdating(bool bEnabled)
{
	if (!CaptureComponent)
	{
		return;
	}

	CaptureComponent->bCaptureEveryFrame = bEnabled;
	CaptureComponent->bCaptureOnMovement = bEnabled;
	CaptureComponent->SetComponentTickEnabled(bEnabled);

	if (bEnabled)
	{
		CaptureComponent->CaptureScene();
	}
}

bool AFirstPersonCharacter::CanInteract() const
{
	return true;
}

void AFirstPersonCharacter::TryInteract()
{
	if (!CanInteract())
	{
		UE_LOG(LogTemp, Warning, TEXT("%s %s TryInteract blocked"), OutlierNet::GetNetPrefix(this), *GetName());
		return;
	}

	if (!GetController())
	{
		UE_LOG(LogTemp, Warning, TEXT("%s %s TryInteract blocked: controller is null"), OutlierNet::GetNetPrefix(this), *GetName());
		return;
	}

	AActor* TargetActor = FindInteractTargetByTrace();
	if (!TargetActor)
	{
		UE_LOG(LogTemp, Log, TEXT("%s %s TryInteract miss"), OutlierNet::GetNetPrefix(this), *GetName());

		if (IInteractableInterface* PreviousInteractable = Cast<IInteractableInterface>(HoldingInteractActor))
		{
			PreviousInteractable->EndHoldInteract(this, true);
		}

		return;
	}

	if (IInteractableInterface* Interactable = Cast<IInteractableInterface>(TargetActor))
	{
		if (Interactable->RequiresHoldInteract())
		{
			if (HoldingInteractActor == TargetActor)
			{
				return;
			}

			if (HoldingInteractActor && HoldingInteractActor != TargetActor)
			{
				if (IInteractableInterface* PreviousInteractable = Cast<IInteractableInterface>(HoldingInteractActor))
				{
					PreviousInteractable->EndHoldInteract(this, true);
				}
			}

			HoldingInteractActor = TargetActor;
			Interactable->BeginHoldInteract(this);
			return;
		}
	}

	ServerInteract(TargetActor);
}

void AFirstPersonCharacter::EndInteract()
{
	if (!HoldingInteractActor)
	{
		return;
	}

	if (IInteractableInterface* Interactable = Cast<IInteractableInterface>(HoldingInteractActor.Get()))
	{
		Interactable->EndHoldInteract(this, true);
	}

	HoldingInteractActor = nullptr;
}

void AFirstPersonCharacter::NotifyHoldInteractCompleted(AActor* CompletedActor)
{
	if (!CompletedActor || CompletedActor != HoldingInteractActor)
	{
		return;
	}

	if (IInteractableInterface* Interactable = Cast<IInteractableInterface>(CompletedActor))
	{
		Interactable->EndHoldInteract(this, false);
	}

	ServerInteract(CompletedActor);
	HoldingInteractActor = nullptr;
}

void AFirstPersonCharacter::ServerInteract_Implementation(AActor* TargetActor)
{
	if (!TargetActor || !CanInteract())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Server] %s ServerInteract blocked Target=%s"), *GetName(), *GetNameSafe(TargetActor));
		return;
	}

	if (!GetController())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Server] %s ServerInteract blocked: controller is null"), *GetName());
		return;
	}

	if (!IsInteractTargetByTrace(TargetActor))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Server] %s ServerInteract validation failed Requested=%s"), *GetName(), *GetNameSafe(TargetActor));
		return;
	}

	if (IInteractableInterface* Interactable = Cast<IInteractableInterface>(TargetActor))
	{
		UE_LOG(LogTemp, Log, TEXT("[Server] %s ServerInteract success Target=%s"), *GetName(), *GetNameSafe(TargetActor));
		Interactable->Interact(this);
		ClientOnInteractSucceeded(TargetActor); //InteractObject UI Update;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[Server] %s ServerInteract failed: target not interactable Target=%s"), *GetName(), *GetNameSafe(TargetActor));
	}
}

void AFirstPersonCharacter::ClientOnInteractSucceeded_Implementation(AActor* TargetActor)
{
	FocusedInteractable = TargetActor;

	if (IInteractableInterface* Interactable = Cast<IInteractableInterface>(TargetActor))
	{
		if (UInteractableComponent* InteractableComponent = Interactable->GetInteractableComponent())
		{
			InteractableComponent->InteractKeyWidgetDeactivate();
		}

		if (AInteractionNode* InteractionNode = Cast<AInteractionNode>(TargetActor))
		{
			if (HasAuthority())
			{
				return;
			}

			InteractionNode->Interact(this);
		}
	}
}

FGameplayTagContainer AFirstPersonCharacter::GetOwnedGameplayTagsForQuery() const
{
	return OwnedQueryTags;
}

void AFirstPersonCharacter::OnRep_CurrentWeapon()
{
	UE_LOG(LogTemp, Log, TEXT("%s %s OnRep_CurrentWeapon Previous=%s Current=%s"), OutlierNet::GetNetPrefix(this), *GetName(), *GetNameSafe(LastReplicatedWeapon), *GetNameSafe(CurrentWeapon));
	CurrentWeaponType = CurrentWeapon ? CurrentWeapon->GetWeaponType() : EWeaponType::Unarmed;
	LastReplicatedWeapon = CurrentWeapon;
	OnWeaponChanged.Broadcast(CurrentWeaponType);

	CaptureComponentWeaponNotIncluded(LastReplicatedWeapon);
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
		OnWeaponChanged.Broadcast(CurrentWeapon->GetWeaponType());
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

void AFirstPersonCharacter::CaptureComponentWeaponNotIncluded(AWeaponBase* Weapon)
{
	if (CaptureComponent && Weapon)
	{
		CaptureComponent->HideActorComponents(Weapon, true);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("CaptureComponentWEAPONnoTiNCLUDED"));
	}
}

void AFirstPersonCharacter::UpdateInteractableFocus()
{
	TArray<AActor*> CurrentInteractables;
	GetInteractablesInRange(CurrentInteractables);
	SyncInteractableKeyWidgets(CurrentInteractables);
}

void AFirstPersonCharacter::GetInteractablesInRange(TArray<AActor*>& OutInteractables) const
{
	if (!GetWorld() || !GetController())
	{
		return;
	}

	const FVector SphereCenter = FirstPersonCamera
		? FirstPersonCamera->GetComponentLocation()
		: GetActorLocation();

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(InteractionSphereOverlap), false, this);
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(CurrentWeapon);

	TArray<FOverlapResult> Overlaps;
	const bool bHasOverlap = GetWorld()->OverlapMultiByChannel(
		Overlaps,
		SphereCenter,
		FQuat::Identity,
		InteractionTraceChannel,
		FCollisionShape::MakeSphere(InteractRange),
		QueryParams
	);

	if (bDrawInteractionTrace)
	{
		const FColor TraceColor = bHasOverlap ? FColor::Green : FColor::Red;
		DrawDebugSphere(GetWorld(), SphereCenter, InteractRange, 32, TraceColor, false, InteractionTraceInterval, 0, 1.5f);
		DrawDebugPoint(GetWorld(), SphereCenter, 12.0f, FColor::Yellow, false, InteractionTraceInterval, 0);
		DrawDebugString(GetWorld(), SphereCenter + FVector(0.0f, 0.0f, 16.0f), TEXT("Interaction SphereOverlap"), nullptr, TraceColor, InteractionTraceInterval, true);
	}

	if (!bHasOverlap)
	{
		return;
	}

	TSet<AActor*> VisitedActors;

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* HitActor = Overlap.GetActor();
		if (!HitActor || VisitedActors.Contains(HitActor))
		{
			continue;
		}

		VisitedActors.Add(HitActor);

		IInteractableInterface* Interactable = Cast<IInteractableInterface>(HitActor);
		if (!Interactable)
		{
			continue;
		}

		UInteractableComponent* InteractableComponent = Interactable->GetInteractableComponent();
		if (!InteractableComponent || !InteractableComponent->CanInteract(GetOwnedGameplayTagsForQuery()))
		{
			continue;
		}

		OutInteractables.Add(HitActor);
	}
}

AActor* AFirstPersonCharacter::FindInteractTargetByTrace() const
{
	if (!GetWorld() || !GetController())
	{
		return nullptr;
	}

	FVector CameraLocation;
	FRotator CameraRotation;
	GetController()->GetPlayerViewPoint(CameraLocation, CameraRotation);

	 FVector Start = CameraLocation;
	 FVector End = Start + CameraRotation.Vector() * InteractRange;

	if (InteractionTraceMode == EInteractionTraceMode::SphereTrace)
	{
		End = Start + CameraRotation.Vector() * InteractRange - InteractionSphereTraceRadius;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(InteractionTrace), false, this);
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(CurrentWeapon);

	FHitResult Hit;
	bool bHit = false;

	if (InteractionTraceMode == EInteractionTraceMode::SphereTrace)
	{
		bHit = GetWorld()->SweepSingleByChannel(
			Hit,
			Start,
			End - InteractionSphereTraceRadius,
			FQuat::Identity,
			InteractionTraceChannel,
			FCollisionShape::MakeSphere(InteractionSphereTraceRadius),
			QueryParams
		);
	}
	else
	{
		bHit = GetWorld()->LineTraceSingleByChannel(
			Hit,
			Start,
			End,
			InteractionTraceChannel,
			QueryParams
		);
	}

	if (bDrawInteractionTrace)
	{
		const FColor TraceColor = bHit ? FColor::Cyan : FColor::Orange;

		if (InteractionTraceMode == EInteractionTraceMode::SphereTrace)
		{
			const FVector TraceVector = End - Start;
			const float TraceLength = TraceVector.Size();
			const FVector TraceDirection = TraceLength > UE_KINDA_SMALL_NUMBER
				? TraceVector / TraceLength
				: FVector::ForwardVector;
			const FVector CapsuleCenter = (Start + End) * 0.5f;
			const float CapsuleHalfHeight = TraceLength * 0.5f + InteractionSphereTraceRadius;
			const FQuat CapsuleRotation = FQuat::FindBetweenNormals(FVector::UpVector, TraceDirection);

			DrawDebugCapsule(
				GetWorld(),
				CapsuleCenter,
				CapsuleHalfHeight,
				InteractionSphereTraceRadius,
				CapsuleRotation,
				TraceColor,
				false,
				InteractionTraceInterval,
				0,
				1.5f
			);

			DrawDebugSphere(GetWorld(), Start, InteractionSphereTraceRadius, 16, FColor::Yellow, false, InteractionTraceInterval, 0, 1.0f);
			DrawDebugSphere(GetWorld(), End, InteractionSphereTraceRadius, 16, TraceColor, false, InteractionTraceInterval, 0, 1.0f);
			DrawDebugString(GetWorld(), CapsuleCenter + FVector(0.0f, 0.0f, 16.0f), TEXT("Interact SphereTrace"), nullptr, TraceColor, InteractionTraceInterval, true);
		}
		else
		{
			DrawDebugLine(
				GetWorld(),
				Start,
				End,
				TraceColor,
				false,
				InteractionTraceInterval,
				0,
				2.0f
			);

			DrawDebugPoint(GetWorld(), Start, 8.0f, FColor::Yellow, false, InteractionTraceInterval, 0);
			DrawDebugPoint(GetWorld(), End, 8.0f, TraceColor, false, InteractionTraceInterval, 0);
			DrawDebugString(GetWorld(), (Start + End) * 0.5f + FVector(0.0f, 0.0f, 16.0f), TEXT("Interact LineTrace"), nullptr, TraceColor, InteractionTraceInterval, true);
		}


		if (bHit)
		{
			DrawDebugPoint(GetWorld(), Hit.ImpactPoint, 12.0f, FColor::Cyan, false, InteractionTraceInterval, 0);
			DrawDebugString(GetWorld(), Hit.ImpactPoint + FVector(0.0f, 0.0f, 16.0f), TEXT("Interact Trace Hit"), nullptr, FColor::Cyan, InteractionTraceInterval, true);
		}
	}

	if (!bHit)
	{
		return nullptr;
	}

	AActor* HitActor = Hit.GetActor();
	IInteractableInterface* Interactable = Cast<IInteractableInterface>(HitActor);
	if (!Interactable)
	{
		return nullptr;
	}

	UInteractableComponent* InteractableComponent = Interactable->GetInteractableComponent();
	if (!InteractableComponent || !InteractableComponent->CanInteract(GetOwnedGameplayTagsForQuery()))
	{
		return nullptr;
	}

	return HitActor;
}

bool AFirstPersonCharacter::IsInteractTargetByTrace(AActor* TargetActor) const
{
	if (!TargetActor)
	{
		return false;
	}

	return FindInteractTargetByTrace() == TargetActor;
}

void AFirstPersonCharacter::SyncInteractableKeyWidgets(const TArray<AActor*>& CurrentInteractables)
{
	TSet<AActor*> CurrentSet;
	for (AActor* CurrentInteractable : CurrentInteractables)
	{
		if (CurrentInteractable)
		{
			CurrentSet.Add(CurrentInteractable);
		}
	}

	for (TObjectPtr<AActor> PreviousInteractable : NearbyInteractables)
	{
		if (!PreviousInteractable || CurrentSet.Contains(PreviousInteractable.Get()))
		{
			continue;
		}

		if (IInteractableInterface* PreviousInterface = Cast<IInteractableInterface>(PreviousInteractable.Get()))
		{
			if (UInteractableComponent* PreviousComponent = PreviousInterface->GetInteractableComponent())
			{
				PreviousComponent->InteractKeyWidgetDeactivate();
			}

			if (AInteractionNode* PreviousInteractionNode = Cast<AInteractionNode>(PreviousInteractable.Get()))
			{
				PreviousInteractionNode->InteractInfoWidgetDeactivate();
			}
		}

		if (FocusedInteractable == PreviousInteractable.Get())
		{
			FocusedInteractable = nullptr;
		}
	}

	NearbyInteractables.Reset();

	for (AActor* CurrentInteractable : CurrentInteractables)
	{
		if (!CurrentInteractable)
		{
			continue;
		}

		NearbyInteractables.Add(CurrentInteractable);

		if (IInteractableInterface* CurrentInterface = Cast<IInteractableInterface>(CurrentInteractable))
		{
			if (UInteractableComponent* CurrentComponent = CurrentInterface->GetInteractableComponent())
			{
				CurrentComponent->InteractKeyWidgetActivate(this);
			}
		}
	}

}
