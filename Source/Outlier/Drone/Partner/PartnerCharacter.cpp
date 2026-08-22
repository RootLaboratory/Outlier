// Fill out your copyright notice in the Description page of Project Settings.


#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerInputConfig.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Drone/Partner/PartnerDistanceComponent.h"
#include "Drone/Partner/PartnerMovementComponent.h"
#include "Drone/Partner/PartnerSupportComponent.h"
#include "Drone/Partner/PartnerCombatComponent.h"
#include "Drone/Partner/PartnerSpriteAnimationComponent.h"
#include "Drone/Partner/PartnerHackComponent.h"
#include "Drone/Partner/PartnerEMPComponent.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Drone/DroneMoveDataRow.h"
#include "Drone/DroneControlDataRow.h"
#include "PostProcess/MaterialPostProcessSubsystem.h"
#include "PostProcess/OutlierPostProcessVolume.h"
#include "Drone/Partner/PartnerSkillCommonRow.h"
#include "Drone/Partner/PartnerSkillDataRow.h"
#include "Drone/Partner/PartnerSurvivalDataRow.h"
#include "Drone/Partner/PartnerVitalityComponent.h"
#include "Drone/Partner/PartnerCameraAssistDataRow.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Shooter/ShooterCharacter.h"
#include "OutlierPlayerState.h"
#include "LocalPlayerUISubSystem.h"
#include "TagDrivenUIGameplayTags.h"
#include "Perception/AISense_Hearing.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Upgrade/OutlierUpgradeComponent.h"
#include "Weapon/WeaponBase.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GAS/Attributes/OutlierVitalAttributeSet.h"
#include "GameplayEffect.h"

namespace
{
	void CollectSocketNamesByPrefix(
		const USkeletalMeshComponent* MeshComponent,
		FName SocketPrefix,
		TArray<FName>& OutSocketNames)
	{
		if (!MeshComponent || SocketPrefix.IsNone())
		{
			return;
		}

		const FString PrefixString = SocketPrefix.ToString();
		for (const FName SocketName : MeshComponent->GetAllSocketNames())
		{
			const FString SocketString = SocketName.ToString();
			if (SocketName == SocketPrefix || SocketString.StartsWith(PrefixString))
			{
				OutSocketNames.Add(SocketName);
			}
		}

		OutSocketNames.Sort(
			[](const FName& Left, const FName& Right)
			{
				return Left.LexicalLess(Right);
			});
	}
}

void APartnerCharacter::BeginPlay()
{
	if (ThirdPersonTiltRoot && GetMesh()->GetAttachParent() != ThirdPersonTiltRoot)
	{
		GetMesh()->AttachToComponent(
			ThirdPersonTiltRoot,
			FAttachmentTransformRules::KeepRelativeTransform
		);
	}

	Super::BeginPlay();
	RefreshAbilitySystemActorInfo();
	BindPartnerCooldownUIObserver();

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->SetMovementMode(MOVE_Flying);
		MoveComp->GravityScale = 0.0f;
		MoveComp->MaxFlySpeed = bIsAccelerate ? BoostSpeed : MoveSpeed;
		MoveComp->BrakingDecelerationFlying = Deceleration;
	}

	EnsurePartnerDataInitialized();
	AttachBoostVFXToMeshes();
}

void APartnerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority() && CachedShooterCharacter)
	{
		CachedShooterCharacter->CancelActiveQuantumLeap(false);
		CachedShooterCharacter->EndActiveBulletReflection(false);
		CachedShooterCharacter->EndActiveWeaponOvercharge(false);
	}
	UnbindPartnerCooldownUIObserver();
	CleanupBoostVFXComponents();
	if (PartnerVitalityComponent)
	{
		PartnerVitalityComponent->BeginOwnerTeardown();
	}
	if (OutlierAbilitySystemComponent)
	{
		OutlierAbilitySystemComponent->ClearForPawn(this);
	}

	Super::EndPlay(EndPlayReason);
}

void APartnerCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	RefreshAbilitySystemActorInfo();

	if (bIsAccelerate)
	{
		StartBoostNoiseTimer();
	}
}


float APartnerCharacter::ReceiveOutlierDamage(const FOutlierDamageRequest& Request)
{
	if (!HasAuthority() || !CanBeDamaged() || Request.DamageAmount <= 0.0f || !OutlierAbilitySystemComponent)
	{
		return 0.0f;
	}
	const bool bApplied = OutlierAbilitySystemComponent->ApplyDamageToSelf(
		Request.DamageAmount,
		Request.EventInstigator,
		Request.DamageCauser,
		Request.DamageTag);
	return bApplied ? Request.DamageAmount : 0.0f;
}

void APartnerCharacter::UnPossessed()
{
	StopBoostNoiseTimer();

	if (CombatComponent)
	{
		CombatComponent->ForceStopAttack();
	}

	if (MovementComponent)
	{
		MovementComponent->ClearFlightInput();
	}

	Super::UnPossessed();
	RefreshAbilitySystemActorInfo();
}

void APartnerCharacter::OnRep_Controller()
{
	Super::OnRep_Controller();
	RefreshAbilitySystemActorInfo();
	RefreshPartnerCooldownUI();
}

UAbilitySystemComponent* APartnerCharacter::GetAbilitySystemComponent() const
{
	return OutlierAbilitySystemComponent;
}

void APartnerCharacter::RefreshAbilitySystemActorInfo()
{
	if (OutlierAbilitySystemComponent)
	{
		OutlierAbilitySystemComponent->InitializeForPawn(this);
	}
	if (PartnerVitalityComponent)
	{
		PartnerVitalityComponent->BindObservers();
	}
}

void APartnerCharacter::BindPartnerCooldownUIObserver()
{
	if (!OutlierAbilitySystemComponent || PartnerCooldownEffectAddedHandle.IsValid())
	{
		return;
	}

	PartnerCooldownEffectAddedHandle =
		OutlierAbilitySystemComponent->OnActiveGameplayEffectAddedDelegateToSelf.AddUObject(
			this,
			&APartnerCharacter::HandlePartnerCooldownEffectAdded);
}

void APartnerCharacter::UnbindPartnerCooldownUIObserver()
{
	if (OutlierAbilitySystemComponent && PartnerCooldownEffectAddedHandle.IsValid())
	{
		OutlierAbilitySystemComponent->OnActiveGameplayEffectAddedDelegateToSelf.Remove(
			PartnerCooldownEffectAddedHandle);
	}
	PartnerCooldownEffectAddedHandle.Reset();
}

void APartnerCharacter::HandlePartnerCooldownEffectAdded(
	UAbilitySystemComponent* AbilitySystem,
	const FGameplayEffectSpec& EffectSpec,
	FActiveGameplayEffectHandle EffectHandle)
{
	(void)AbilitySystem;
	(void)EffectHandle;
	if (!EffectSpec.Def)
	{
		return;
	}

	const FGameplayTagContainer& GrantedTags = EffectSpec.Def->GetGrantedTags();
	const FGameplayTag CooldownTags[] =
	{
		OutlierGameplayTags::Cooldown::Partner::EMP(),
		OutlierGameplayTags::Cooldown::Partner::Shield(),
		OutlierGameplayTags::Cooldown::Partner::Hacking(),
		OutlierGameplayTags::Cooldown::Partner::Scan()
	};
	for (const FGameplayTag& CooldownTag : CooldownTags)
	{
		if (GrantedTags.HasTagExact(CooldownTag))
		{
			NotifyPartnerCooldownUI(CooldownTag);
			return;
		}
	}
}

void APartnerCharacter::RefreshPartnerCooldownUI()
{
	const FGameplayTag CooldownTags[] =
	{
		OutlierGameplayTags::Cooldown::Partner::EMP(),
		OutlierGameplayTags::Cooldown::Partner::Shield(),
		OutlierGameplayTags::Cooldown::Partner::Hacking(),
		OutlierGameplayTags::Cooldown::Partner::Scan()
	};
	for (const FGameplayTag& CooldownTag : CooldownTags)
	{
		NotifyPartnerCooldownUI(CooldownTag);
	}
}

void APartnerCharacter::NotifyPartnerCooldownUI(const FGameplayTag& CooldownTag)
{
	if (!IsLocallyControlled() || !OutlierAbilitySystemComponent)
	{
		return;
	}

	const float Remaining = OutlierAbilitySystemComponent->GetPartnerCooldownRemaining(CooldownTag);
	if (Remaining <= 0.0f)
	{
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	ULocalPlayerUISubSystem* UISubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUISubSystem>()
		: nullptr;
	if (!UISubsystem)
	{
		return;
	}

	FGameplayTag AbilityTag;
	if (CooldownTag == OutlierGameplayTags::Cooldown::Partner::EMP())
	{
		AbilityTag = TagDrivenUITags::Ability::Partner::EMP();
	}
	else if (CooldownTag == OutlierGameplayTags::Cooldown::Partner::Shield())
	{
		AbilityTag = TagDrivenUITags::Ability::Partner::Shield();
	}
	else if (CooldownTag == OutlierGameplayTags::Cooldown::Partner::Hacking())
	{
		AbilityTag = TagDrivenUITags::Ability::Partner::Hacking();
	}
	else if (CooldownTag == OutlierGameplayTags::Cooldown::Partner::Scan())
	{
		AbilityTag = TagDrivenUITags::Ability::Partner::Scan();
	}
	if (AbilityTag.IsValid())
	{
		UISubsystem->OnAbilityUsed(AbilityTag, Remaining);
	}
}

void APartnerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (MovementComponent)
	{
		MovementComponent->ClearFlightInput();
	}

	if (ToggleTestWeaponAttachmentKey.IsValid())
	{
		PlayerInputComponent->BindKey(
			ToggleTestWeaponAttachmentKey,
			IE_Pressed,
			this,
			&APartnerCharacter::ToggleTestWeaponEquipment);
	}

	// Set up Action Bindings
	UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	UPartnerInputConfig* PartnerInputConfig = Cast<UPartnerInputConfig>(InputConfig);
	if (!EnhancedInputComponent || !PartnerInputConfig) {
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[OutlierInputDebug] Partner input bind failed: EnhancedInput=%s PartnerInputConfig=%s RawInputConfig=%s RawClass=%s"),
			*GetNameSafe(EnhancedInputComponent),
			*GetNameSafe(PartnerInputConfig),
			*GetNameSafe(InputConfig),
			InputConfig ? *GetNameSafe(InputConfig->GetClass()) : TEXT("None")
		);
		return;
	}

	// AreaOfEffect
	EnhancedInputComponent->BindAction(PartnerInputConfig->AreaOfEffectAction,	ETriggerEvent::Started,   this, &APartnerCharacter::AreaOfEffect);

	// CameraAssist
	EnhancedInputComponent->BindAction(PartnerInputConfig->CameraAssistAction,	ETriggerEvent::Triggered, this, &APartnerCharacter::CameraAssist);
	EnhancedInputComponent->BindAction(PartnerInputConfig->CameraAssistAction,  ETriggerEvent::Completed, this, &APartnerCharacter::StopCameraAssist);

	// Hacking
	EnhancedInputComponent->BindAction(PartnerInputConfig->HackingAction,		ETriggerEvent::Started,	  this, &APartnerCharacter::TryHacking);
	EnhancedInputComponent->BindAction(PartnerInputConfig->HackingAction,		ETriggerEvent::Completed, this, &APartnerCharacter::EndHacking);
	EnhancedInputComponent->BindAction(PartnerInputConfig->HackingAction,		ETriggerEvent::Canceled,  this, &APartnerCharacter::EndHacking);

	// Scan
	EnhancedInputComponent->BindAction(PartnerInputConfig->ScanAction,			ETriggerEvent::Started,   this, &APartnerCharacter::Scan);	
	//EnhancedInputComponent->BindAction(PartnerInputConfig->ScanAction, ETriggerEvent::Started, this, &APartnerCharacter::TestAbilityScan);

	// Shield
	EnhancedInputComponent->BindAction(PartnerInputConfig->ShieldAction,		ETriggerEvent::Started,	  this, &APartnerCharacter::Shield);

	// SyncMove
	EnhancedInputComponent->BindAction(PartnerInputConfig->SyncMoveAction,		ETriggerEvent::Triggered, this, &APartnerCharacter::SyncMove);
	EnhancedInputComponent->BindAction(PartnerInputConfig->SyncMoveAction,		ETriggerEvent::Completed, this, &APartnerCharacter::StopSyncMove);

	// Accelerate
	EnhancedInputComponent->BindAction(PartnerInputConfig->AccelerateAction,	ETriggerEvent::Started,	  this, &APartnerCharacter::ToggleAccelerate);

	// FreeMove
	EnhancedInputComponent->BindAction(PartnerInputConfig->FreeMoveAction,		ETriggerEvent::Triggered, this, &APartnerCharacter::FreeMove);
	EnhancedInputComponent->BindAction(PartnerInputConfig->FreeMoveAction,		ETriggerEvent::Completed, this, &APartnerCharacter::StopFreeMove);

	// VerticalMove
	EnhancedInputComponent->BindAction(PartnerInputConfig->VerticalMoveAction,	ETriggerEvent::Triggered, this, &APartnerCharacter::VerticalMove);
	EnhancedInputComponent->BindAction(PartnerInputConfig->VerticalMoveAction, ETriggerEvent::Completed, this, &APartnerCharacter::StopVerticalMove);
}

void APartnerCharacter::DoMove(float Right, float Forward)
{
	const FVector2D MoveValue(Right, Forward);

	if (!CanAcceptInput())
	{
		if (MovementComponent)
		{
			MovementComponent->SetMoveInput(FVector2D::ZeroVector);
		}
		return;
	}

	if (MovementComponent)
	{
		MovementComponent->SetMoveInput(MoveValue);
	}
}

void APartnerCharacter::OnMoveInputUpdated(const FVector2D& MoveValue)
{
	Super::OnMoveInputUpdated(MoveValue);
}

void APartnerCharacter::TryStartAttack()
{
	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[PartnerWeaponVFX][AttackInput] Character=%s Authority=%d CurrentWeapon=%s"),
		*GetNameSafe(this),
		HasAuthority() ? 1 : 0,
		*GetNameSafe(GetCurrentWeapon()));
	StartWeaponAttack();
}

void APartnerCharacter::TryStopAttack()
{
	StopWeaponAttack();
}

void APartnerCharacter::HandleAutoReloadRequested()
{
	if (CombatComponent)
	{
		CombatComponent->StartAutoReload();
	}
}

void APartnerCharacter::StartWeaponAttack()
{
	if (CombatComponent)
	{
		CombatComponent->TryStartAttack();
	}
}

void APartnerCharacter::StopWeaponAttack()
{
	if (CombatComponent)
	{
		CombatComponent->TryStopAttack();
	}
}


void APartnerCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(APartnerCharacter, MovementState);
	DOREPLIFETIME(APartnerCharacter, MoveMode);
	DOREPLIFETIME(APartnerCharacter, BoundaryState);
	DOREPLIFETIME(APartnerCharacter, SyncLocalOffset);
	DOREPLIFETIME(APartnerCharacter, bShieldActive);
	DOREPLIFETIME(APartnerCharacter, bScanning);
	DOREPLIFETIME(APartnerCharacter, bIsAccelerate);
	DOREPLIFETIME(APartnerCharacter, bHiddenForEnemyPossession);
}

void APartnerCharacter::ToggleTestWeaponEquipment()
{
	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[PartnerWeaponToggle][Input] Character=%s Authority=%d LocallyControlled=%d CurrentWeapon=%s CombatComponent=%s"),
		*GetNameSafe(this),
		HasAuthority() ? 1 : 0,
		IsLocallyControlled() ? 1 : 0,
		*GetNameSafe(GetCurrentWeapon()),
		*GetNameSafe(CombatComponent));

	if (CombatComponent)
	{
		CombatComponent->ToggleTestWeaponEquipped();
	}
}

FGameplayTagContainer APartnerCharacter::GetOwnedGameplayTagsForQuery() const
{
	FGameplayTagContainer GameplayTags = Super::GetOwnedGameplayTagsForQuery();
	if (OutlierAbilitySystemComponent)
	{
		FGameplayTagContainer AbilitySystemTags;
		OutlierAbilitySystemComponent->GetOwnedGameplayTags(AbilitySystemTags);
		GameplayTags.AppendTags(AbilitySystemTags);
	}
	if (bHiddenForEnemyPossession)
	{
		GameplayTags.AddTag(OutlierGameplayTags::State::Stealthed());
	}
	return GameplayTags;
}

void APartnerCharacter::AreaOfEffect()
{
	TryEMP();
}

void APartnerCharacter::CameraAssist()
{
	if (!CanAcceptInput())
	{
		return;
	}

	if (MovementComponent)
	{
		MovementComponent->ApplyCameraAssist();
	}
}

void APartnerCharacter::StopCameraAssist()
{
	if (MovementComponent)
	{
		MovementComponent->StopCameraAssist();
	}
}

void APartnerCharacter::TryHacking()
{
	if (!CanAcceptInput())
	{
		return;
	}

	if (UPartnerHackComponent* RuntimeHackComponent = GetRuntimeHackComponent())
	{
		if (RuntimeHackComponent->TryBeginHackHold())
		{
			return;
		}
		if (RuntimeHackComponent->IsHackInteractionActive())
		{
			RuntimeHackComponent->TryHack();
			return;
		}
	}

	if (!OutlierAbilitySystemComponent)
	{
		return;
	}

	OutlierAbilitySystemComponent->TryActivatePartnerAbility(
		OutlierGameplayTags::Ability::Partner::Hacking());
	FaceSpriteAnimationComponent->SetEmotion(EPartnerEmotion::Happy);
}

void APartnerCharacter::EndHacking()
{
	if (UPartnerHackComponent* RuntimeHackComponent = GetRuntimeHackComponent())
	{
		RuntimeHackComponent->EndHackHold();
	}
}

void APartnerCharacter::TryEMP()
{
	if (!CanAcceptInput() || !OutlierAbilitySystemComponent)
	{
		return;
	}

	if (UPartnerEMPComponent* RuntimeEMPComponent = GetRuntimeEMPComponent();
		RuntimeEMPComponent && RuntimeEMPComponent->IsEMPInteractionActive())
	{
		RuntimeEMPComponent->TryEMP();
		return;
	}

	OutlierAbilitySystemComponent->TryActivatePartnerAbility(
		OutlierGameplayTags::Ability::Partner::EMP());
	FaceSpriteAnimationComponent->SetEmotion(EPartnerEmotion::Surprised);

}

void APartnerCharacter::Hacking(AActor* TargetActor)
{
	if (!TargetActor)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("Hack Target : %s"), *GetNameSafe(TargetActor));

	// 해킹 로직
}

void APartnerCharacter::Scan()
{
	if (!CanAcceptInput())
	{
		return;
	}
	UE_LOG(LogTemp, Error, TEXT("Scan Valid"));
	FaceSpriteAnimationComponent->SetEmotion(EPartnerEmotion::Sad);

	if (OutlierAbilitySystemComponent)
	{
		OutlierAbilitySystemComponent->TryActivatePartnerAbility(
			OutlierGameplayTags::Ability::Partner::Scan());
	}
}

void APartnerCharacter::Shield()
{
	if (!CanAcceptInput())
	{
		return;
	}

	UE_LOG(LogTemp, Error, TEXT("Shield Valid"));

	if (OutlierAbilitySystemComponent)
	{
		OutlierAbilitySystemComponent->TryActivatePartnerAbility(
			OutlierGameplayTags::Ability::Partner::Shield());
	}
	FaceSpriteAnimationComponent->SetEmotion(EPartnerEmotion::Angry);

}

void APartnerCharacter::NotifyBoundaryUI(bool bDisabled)
{
	if (!IsLocallyControlled())
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		return;
	}

	ULocalPlayer* LP = PC->GetLocalPlayer();
	if (!LP)
	{
		return;
	}

	if (ULocalPlayerUISubSystem* SubSystem = LP->GetSubsystem<ULocalPlayerUISubSystem>())
	{
		if (bDisabled)
		{
			SubSystem->OnAbilityDisabledByDistance(); //스킬 사거리 존재한다면 따로 분리.
		}
		else
		{
			SubSystem->OnAbilityEnabledByDistance();
		}
	}
}

void APartnerCharacter::ApplyDamagedEvent(float InRatio) const
{
	if (IsLocallyControlled())
	{
		UMaterialPostProcessSubsystem* PPS = GetWorld()->GetSubsystem<UMaterialPostProcessSubsystem>();
		if (PPS)
		{
			PPS->SetPostProcessEnabled(EOutlierPostProcessMaterialType::Damaged, true);
			PPS->UpdateDamagedPostProcess(InRatio, FVector4(0.0f,0.0f,1.0f,0.0f));
		}
	}
}

void APartnerCharacter::NullifyDamagedEvenet() const
{
	if (!IsLocallyControlled())
	{
		return;
	}

	UMaterialPostProcessSubsystem* MaterialSub = GetWorld()->GetSubsystem<UMaterialPostProcessSubsystem>();
	if (MaterialSub)
	{
		UE_LOG(LogTemp, Error, TEXT("MaterialSub"));

		MaterialSub->SetPostProcessEnabled(EOutlierPostProcessMaterialType::Damaged, false);
		MaterialSub->UpdateDamagedPostProcess(1);
	}
}

void APartnerCharacter::SyncMove()
{
	if (!CanAcceptInput())
	{
		return;
	}

	if (MovementComponent)
	{
		MovementComponent->SetSyncMove(true);
	}
}

void APartnerCharacter::StopSyncMove()
{
	if (MovementComponent)
	{
		MovementComponent->SetSyncMove(false);
	}
}

void APartnerCharacter::ToggleAccelerate()
{
	if (!CanAcceptInput())
	{
		return;
	}

	const bool bNewAccelerate = !bIsAccelerate;
	ApplyAccelerateState(bNewAccelerate);

	if (!HasAuthority())
	{
		ServerSetAccelerate(bNewAccelerate);
	}
}

void APartnerCharacter::FreeMove()
{
	if (!CanAcceptInput())
	{
		return;
	}

	if (MovementComponent)
	{
		MovementComponent->SetFreeMove(true);
	}
}

void APartnerCharacter::StopFreeMove()
{
	UE_LOG(LogTemp, Warning, TEXT("[OutlierInputDebug] Partner FreeMove Completed: %s"), *GetNameSafe(this));

	if (MovementComponent)
	{
		MovementComponent->SetFreeMove(false);
	}
}

void APartnerCharacter::VerticalMove(const FInputActionValue& Value)
{
	if (!CanAcceptInput())
	{
		if (MovementComponent)
		{
			MovementComponent->SetVerticalInput(0.0f);
		}
		return;
	}

	const float Axis = Value.Get<float>();

	if (MovementComponent)
	{
		MovementComponent->SetVerticalInput(Axis);
	}
}

void APartnerCharacter::StopVerticalMove()
{
	if (MovementComponent)
	{
		MovementComponent->SetVerticalInput(0.0f);
	}
}

void APartnerCharacter::SetBoundaryOutside(bool bOutside)
{
	const EPartnerBoundaryState NewState = bOutside
		? EPartnerBoundaryState::Outside
		: EPartnerBoundaryState::Inside;

	if (BoundaryState == NewState)
	{
		return;
	}

	BoundaryState = NewState;

	if (AOutlierPlayerState* PS = GetPlayerState<AOutlierPlayerState>())
	{
		PS->SetSuitDisabledByPartnerBoundary(bOutside);
	}
}

EPartnerBoundaryState APartnerCharacter::GetBoundaryOutside()
{
	return BoundaryState;
}

void APartnerCharacter::SetEnemyPossessionProtection(bool bEnabled)
{
	if (!HasAuthority())
	{
		return;
	}

	if (PartnerVitalityComponent)
	{
		PartnerVitalityComponent->SetEnemyPossessionProtection(bEnabled);
	}

	if (bHiddenForEnemyPossession == bEnabled)
	{
		return;
	}

	bHiddenForEnemyPossession = bEnabled;
	ForceNetUpdate();

	if (UEnemyRoomSubsystem* EnemyRoomSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UEnemyRoomSubsystem>()
		: nullptr)
	{
		EnemyRoomSubsystem->RefreshDetectionTarget(this);
	}
}

void APartnerCharacter::StopActionsForReboot()
{
	if (!HasAuthority())
	{
		return;
	}
	if (CachedShooterCharacter)
	{
		CachedShooterCharacter->CancelActiveQuantumLeap(true);
		CachedShooterCharacter->EndActiveBulletReflection(true);
		CachedShooterCharacter->EndActiveWeaponOvercharge(true);
	}
	if (CombatComponent)
	{
		CombatComponent->CancelForReboot();
	}
	if (UPartnerHackComponent* RuntimeHackComponent = GetRuntimeHackComponent())
	{
		RuntimeHackComponent->CancelForReboot();
	}
	if (UPartnerEMPComponent* RuntimeEMPComponent = GetRuntimeEMPComponent())
	{
		RuntimeEMPComponent->CancelForReboot();
	}
	if (SupportComponent)
	{
		SupportComponent->CancelForReboot();
	}

	ApplyAccelerateState(false);
	if (MovementComponent)
	{
		MovementComponent->ClearFlightInput();
		MovementComponent->StopCameraAssist();
		ApplyMoveMode(EPartnerMoveMode::Normal);
	}
}

void APartnerCharacter::RefreshEnemyDetectionForVitality()
{
	if (!HasAuthority())
	{
		return;
	}

	if (UEnemyRoomSubsystem* EnemyRoomSubsystem = GetWorld()
		? GetWorld()->GetSubsystem<UEnemyRoomSubsystem>()
		: nullptr)
	{
		EnemyRoomSubsystem->RefreshDetectionTarget(this);
	}
}

bool APartnerCharacter::CanAcceptInput() const
{
	return !OutlierAbilitySystemComponent
		|| !OutlierAbilitySystemComponent->HasMatchingGameplayTag(OutlierGameplayTags::State::Rebooting());
}

UPartnerEMPComponent* APartnerCharacter::GetRuntimeEMPComponent() const
{
	UPartnerEMPComponent* RuntimeEMPComponent = FindComponentByClass<UPartnerEMPComponent>();
	if (RuntimeEMPComponent && RuntimeEMPComponent->GetOwner() == this)
	{
		return RuntimeEMPComponent;
	}

	if (EMPComponent && EMPComponent->GetOwner() == this)
	{
		return EMPComponent;
	}

	return nullptr;
}

UPartnerHackComponent* APartnerCharacter::GetRuntimeHackComponent() const
{
	UPartnerHackComponent* RuntimeHackComponent = FindComponentByClass<UPartnerHackComponent>();
	if (RuntimeHackComponent && RuntimeHackComponent->GetOwner() == this)
	{
		return RuntimeHackComponent;
	}

	if (HackComponent && HackComponent->GetOwner() == this)
	{
		return HackComponent;
	}

	return nullptr;
}

void APartnerCharacter::SetMoveMode(EPartnerMoveMode NewMode)
{
	if (!CanApplyMoveMode(NewMode))
	{
		return;
	}

	ApplyMoveMode(NewMode);

	if (!HasAuthority())
	{
		ServerSetMoveMode(NewMode);
	}
}

void APartnerCharacter::ApplyMoveMode(EPartnerMoveMode NewMode)
{
	if (MoveMode == NewMode)
	{
		return;
	}

	MoveMode = NewMode;

	if (MovementComponent)
	{
		MovementComponent->OnMoveModeChanged(NewMode);
	}
}

bool APartnerCharacter::CanApplyMoveMode(EPartnerMoveMode NewMode) const
{
	if (!CanAcceptInput() && NewMode != EPartnerMoveMode::Normal)
	{
		return false;
	}

	return NewMode == EPartnerMoveMode::Normal ||
		NewMode == EPartnerMoveMode::FreeMove ||
		NewMode == EPartnerMoveMode::SyncMove ||
		NewMode == EPartnerMoveMode::CameraAssist;
}

void APartnerCharacter::ApplyAccelerateState(bool bNewAccelerate)
{
	bIsAccelerate = bNewAccelerate;

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		const float CurrentSpeed = bIsAccelerate ? BoostSpeed : MoveSpeed;
		MoveComp->MaxFlySpeed = CurrentSpeed;
	}

	if (MovementComponent)
	{
		MovementComponent->ApplyPartnerFlightSettings();
		MovementComponent->ResetMovementFeel();
	}

	if (bIsAccelerate)
	{
		StartBoostNoiseTimer();
	}
	else
	{
		StopBoostNoiseTimer();
	}
}

void APartnerCharacter::StartBoostNoiseTimer()
{
	if (!HasAuthority() || !GetWorld() || GetWorldTimerManager().IsTimerActive(BoostNoiseTimerHandle))
	{
		return;
	}

	GetWorldTimerManager().SetTimer(
		BoostNoiseTimerHandle,
		this,
		&APartnerCharacter::ReportBoostNoise,
		FMath::Max(BoostNoiseInterval, 0.05f),
		true,
		0.0f);
}

void APartnerCharacter::StopBoostNoiseTimer()
{
	if (!HasAuthority() || !GetWorld())
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(BoostNoiseTimerHandle);
}

void APartnerCharacter::ReportBoostNoise()
{
	if (!HasAuthority() || !bIsAccelerate)
	{
		StopBoostNoiseTimer();
		return;
	}

	if (!IsPlayerControlled() || GetVelocity().SizeSquared() < FMath::Square(BoostNoiseMinimumSpeed))
	{
		return;
	}

	const AOutlierPlayerState* OutlierPS = GetPlayerState<AOutlierPlayerState>();
	const FGameplayTag CurrentRoomTag = GetCurrentRoomTag();
	if (OutlierPS && CurrentRoomTag.IsValid())
	{
		if (const UEnemyRoomSubsystem* RoomSubsystem = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
		{
			if (RoomSubsystem->IsRoomInCombat(OutlierPS->GetArenaId(), CurrentRoomTag))
			{
				return;
			}
		}
	}

	UAISense_Hearing::ReportNoiseEvent(
		GetWorld(),
		GetActorLocation(),
		BoostNoiseLoudness,
		this,
		BoostNoiseMaxRange,
		BoostNoiseTag);
}

void APartnerCharacter::AttachBoostVFXToMeshes()
{
	if (!BoostVFX || GetNetMode() == NM_DedicatedServer || !BoostVFXComponents.IsEmpty())
	{
		return;
	}

	AttachBoostVFXToMesh(GetMesh());
}

void APartnerCharacter::AttachBoostVFXToMesh(USkeletalMeshComponent* MeshComponent)
{
	if (!MeshComponent || BoostVFXSocketPrefix.IsNone())
	{
		return;
	}

	TArray<FName> BoostSocketNames;
	CollectSocketNamesByPrefix(MeshComponent, BoostVFXSocketPrefix, BoostSocketNames);

	for (const FName SocketName : BoostSocketNames)
	{
		UNiagaraComponent* NiagaraComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
			BoostVFX,
			MeshComponent,
			SocketName,
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			EAttachLocation::SnapToTarget,
			false,
			true);

		if (NiagaraComponent)
		{
			BoostVFXComponents.Add(NiagaraComponent);
		}
	}
}

void APartnerCharacter::CleanupBoostVFXComponents()
{
	for (TObjectPtr<UNiagaraComponent>& NiagaraComponent : BoostVFXComponents)
	{
		if (NiagaraComponent)
		{
			NiagaraComponent->DestroyComponent();
		}
	}

	BoostVFXComponents.Reset();
}

void APartnerCharacter::ServerSetAccelerate_Implementation(bool bNewAccelerate)
{
	if (!CanAcceptInput())
	{
		return;
	}

	ApplyAccelerateState(bNewAccelerate);
	ForceNetUpdate();
}

void APartnerCharacter::ServerSetMoveMode_Implementation(EPartnerMoveMode NewMode)
{
	if (!CanApplyMoveMode(NewMode))
	{
		return;
	}

	ApplyMoveMode(NewMode);
}

void APartnerCharacter::EnsurePartnerDataInitialized()
{
	if (bPartnerDataInitialized)
	{
		return;
	}

	InitializeFromDataTables();
	if (HasAuthority()
		&& (!OutlierAbilitySystemComponent
			|| OutlierAbilitySystemComponent->GetGrantedPartnerAbilityCount() != 4))
	{
		return;
	}
	if (HasAuthority()
		&& (!PartnerVitalityComponent
			|| !PartnerVitalityComponent->InitializeFromDataTables(VitalityDataRow, PartnerSurvivalDataRow)))
	{
		return;
	}
	bPartnerDataInitialized = true;
}

void  APartnerCharacter::InitializeFromDataTables()
{
	if (const FDroneMoveDataRow* MoveDataRow = DroneMoveDataRow.GetRow<FDroneMoveDataRow>(TEXT("InitializeMoveData")))
	{
		MoveSpeed				= MoveDataRow->MoveSpeed;
		BoostSpeed				= MoveDataRow->BoostSpeed;
		VerticalSpeed		    = MoveDataRow->VerticalSpeed;
		Acceleration			= MoveDataRow->Acceleration;
		Deceleration			= MoveDataRow->Deceleration;
		SyncMoveDistance		= MoveDataRow->SyncMoveDistance;
		SyncMoveInterpSpeed		= MoveDataRow->SyncMoveInterpSpeed;
		CameraAssistStrength    = MoveDataRow->CameraAssistStrength;
		CameraAssistInterpSpeed = MoveDataRow->CameraAssistInterpSpeed;

		if (UCharacterMovementComponent* CharacterMovementComp = GetCharacterMovement())
		{
			CharacterMovementComp->MaxFlySpeed = MoveSpeed;
			CharacterMovementComp->MaxAcceleration = Acceleration;
			CharacterMovementComp->BrakingDecelerationWalking = Deceleration;
			CharacterMovementComp->BrakingDecelerationFlying = Deceleration;
			CharacterMovementComp->GravityScale = 0.0f;
			CharacterMovementComp->SetMovementMode(MOVE_Flying);
		}
	}

	if (const FDroneControlDataRow* ControlDataRow = DroneControlDataRow.GetRow<FDroneControlDataRow>(TEXT("InitializeControlData")))
	{
		LookSensitivity			= ControlDataRow->LookSensitivity;
		PitchMin				= ControlDataRow->PitchMin;
		PitchMax				= ControlDataRow->PitchMax;
		LookInputDeadZone		= ControlDataRow->LookInputDeadZone;
		TurnInterpSpeed			= ControlDataRow->TurnInterpSpeed;
		RotationLagAmount		= ControlDataRow->RotationLagAmount;
		RotationLagRecoverSpeed = ControlDataRow->RotationLagRecoverSpeed;
		CameraRollOnTurn		= ControlDataRow->CameraRollOnTurn;
		CameraRollInterpSpeed	= ControlDataRow->CameraRollInterpSpeed;
	}

	const bool bValidSkillHandle = PartnerSkillDataRow.DataTable
		&& !PartnerSkillDataRow.RowName.IsNone()
		&& PartnerSkillDataRow.DataTable->GetRowStruct() == FPartnerSkillDataRow::StaticStruct();
#if UE_BUILD_SHIPPING
	if (!bValidSkillHandle)
	{
		UE_LOG(LogTemp, Error, TEXT("[PartnerAbility] Invalid Partner skill DataTable handle"));
		return;
	}
#else
	checkf(bValidSkillHandle, TEXT("[PartnerAbility] Invalid Partner skill DataTable handle"));
#endif
	const FPartnerSkillDataRow* SkillDataRow = PartnerSkillDataRow.DataTable->FindRow<FPartnerSkillDataRow>(
		PartnerSkillDataRow.RowName,
		TEXT("InitializeSkillData"),
		false);
#if UE_BUILD_SHIPPING
	if (!SkillDataRow)
	{
		UE_LOG(LogTemp, Error, TEXT("[PartnerAbility] Missing Partner skill row %s"), *PartnerSkillDataRow.RowName.ToString());
		return;
	}
#else
	checkf(SkillDataRow, TEXT("[PartnerAbility] Missing Partner skill row %s"), *PartnerSkillDataRow.RowName.ToString());
#endif
	if (SkillDataRow)
	{
		ScanRange			= SkillDataRow->ScanRange;
		ScanDuration		= SkillDataRow->ScanDuration;
		ScanCooldown		= SkillDataRow->ScanCooldown;
		ScanExpandSpeed		= SkillDataRow->ScanExpandSpeed;

		HackRange			= SkillDataRow->HackRange;
		HackEffectiveRange	= SkillDataRow->HackEffectiveRange;
		HackMiniGameTime	= SkillDataRow->HackMiniGameTime;
		HackCooldown		= SkillDataRow->HackCooldown;
		HackFailPenaltyTime	= SkillDataRow->HackFailPenaltyTime;

		AreaOfEffectRange	= SkillDataRow->AreaOfEffectRange;
		EMPMarkingTime		= SkillDataRow->EMPMarkingTime;
		EMPStunDuration		= SkillDataRow->EMPStunDuration;
		AreaOfEffectCooldown= SkillDataRow->AreaOfEffectCooldown;
		EMPMaxTargets		= SkillDataRow->EMPMaxTargets;

		ShieldRange			= SkillDataRow->ShieldRange;
		ShieldDuration		= SkillDataRow->ShieldDuration;
		ShieldCooldown		= SkillDataRow->ShieldCooldown;
		ShieldAmount		= SkillDataRow->ShieldAmount;
		ShieldDecayRate		= SkillDataRow->ShieldDecayRate;
		ShieldDecayDelay	= SkillDataRow->ShieldDecayDelay;

		InteractionRange	= SkillDataRow->InteractionRange;
	}
	
	if (const FPartnerSkillCommonRow* SkillCommonRow = PartnerSkillCommonDataRow.GetRow<FPartnerSkillCommonRow>(TEXT("InitializeSkillCommon")))
	{
		CoolDown			= SkillCommonRow->CoolDown;
		Duration			= SkillCommonRow->Duration;
		CastTime			= SkillCommonRow->CastTime;
		bRequireLineOfSight = SkillCommonRow->bRequireLineOfSight;
	}

	if (const FPartnerCameraAssistDataRow* CameraAssistDataRow = PartnerCameraAssistDataRow.GetRow<FPartnerCameraAssistDataRow>(TEXT("InitializeCameraAssist")))
	{
		AssistTargetOffset		= CameraAssistDataRow->AssistTargetOffset;
		AssistTargetLocalOffset = CameraAssistDataRow->AssistTargetLocalOffset;
		AssistMinDistance		= CameraAssistDataRow->AssistMinDistance;
		AssistMaxDistance		= CameraAssistDataRow->AssistMaxDistance;
		AssistMaxAngle			= CameraAssistDataRow->AssistMaxAngle;
		AssistDeadZoneAngle		= CameraAssistDataRow->AssistDeadZoneAngle;
		AssistInterpSpeed		= CameraAssistDataRow->AssistInterpSpeed;
		AssistStrength			= CameraAssistDataRow->AssistStrength;
	}

	FPartnerHackAbilityData HackAbilityData;
	HackAbilityData.CandidateRange = HackRange;
	HackAbilityData.EffectiveRange = HackEffectiveRange;
	HackAbilityData.MiniGameTime = HackMiniGameTime;
	HackAbilityData.FailPenaltyTime = HackFailPenaltyTime;
	HackAbilityData.bRequireLineOfSight = bRequireLineOfSight;
	if (UPartnerHackComponent* RuntimeHackComponent = GetRuntimeHackComponent())
	{
		RuntimeHackComponent->CacheAbilityData(HackAbilityData);
	}

	FPartnerEMPAbilityData EMPAbilityData;
	EMPAbilityData.EMPRange = AreaOfEffectRange;
	EMPAbilityData.MarkingTime = EMPMarkingTime;
	EMPAbilityData.StunDuration = EMPStunDuration;
	EMPAbilityData.MaxTargets = EMPMaxTargets;
	if (UPartnerEMPComponent* RuntimeEMPComponent = GetRuntimeEMPComponent())
	{
		RuntimeEMPComponent->CacheAbilityData(EMPAbilityData);
	}

	if (HasAuthority() && OutlierAbilitySystemComponent)
	{
		FOutlierPartnerAbilityConfig AbilityConfig;
		AbilityConfig.EMPCooldown = AreaOfEffectCooldown;
		AbilityConfig.ShieldCooldown = ShieldCooldown;
		AbilityConfig.HackCooldown = HackCooldown;
		AbilityConfig.ScanCooldown = ScanCooldown;
		AbilityConfig.ScanDuration = ScanDuration;
		const bool bConfigured = OutlierAbilitySystemComponent->ConfigurePartnerAbilities(AbilityConfig);
#if UE_BUILD_SHIPPING
		if (!bConfigured)
		{
			UE_LOG(LogTemp, Error, TEXT("[PartnerAbility] Failed to configure Partner GameplayAbilities"));
			return;
		}
#else
		checkf(bConfigured, TEXT("[PartnerAbility] Failed to configure Partner GameplayAbilities"));
#endif
	}

	if (MovementComponent)
	{
		MovementComponent->ApplyPartnerFlightSettings();
	}
}

void APartnerCharacter::LookInput(const FInputActionValue& Value)
{
	if (!CanAcceptInput())
	{
		return;
	}

	FVector2D LookAxis = Value.Get<FVector2D>();

	if (LookAxis.SizeSquared() < FMath::Square(LookInputDeadZone))
	{
		return;
	}

	float YawInput   =  LookAxis.X * LookSensitivity;
	float PitchInput = -LookAxis.Y * LookSensitivity;

	if (TurnFeelCurve.IsValid())
	{
		const float CurveAlpha = FMath::Clamp(LookAxis.Size(), 0.0f, 1.0f);
		const float CurveValue = TurnFeelCurve.Get()->GetFloatValue(CurveAlpha);

		YawInput   *= CurveValue;
		PitchInput *= CurveValue;
	}

	AddControllerYawInput(YawInput);

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (PC)
	{
		FRotator ControlRot = PC->GetControlRotation();
		ControlRot.Pitch = FMath::ClampAngle(
			ControlRot.Pitch - PitchInput,
			PitchMin,
			PitchMax
		);

		PC->SetControlRotation(ControlRot);
	}
}

APartnerCharacter::APartnerCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	OutlierAbilitySystemComponent = CreateDefaultSubobject<UOutlierAbilitySystemComponent>(
		TEXT("AbilitySystemComponent"));
	OutlierAbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
	VitalAttributeSet = CreateDefaultSubobject<UOutlierVitalAttributeSet>(TEXT("VitalAttributeSet"));
	PartnerVitalityComponent = CreateDefaultSubobject<UPartnerVitalityComponent>(TEXT("PartnerVitalityComponent"));

	ThirdPersonTiltRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Third Person Tilt Root"));
	ThirdPersonTiltRoot->SetupAttachment(GetCapsuleComponent());
	GetMesh()->SetupAttachment(ThirdPersonTiltRoot);

	FirstPersonWeaponRoot = CreateDefaultSubobject<USceneComponent>(TEXT("First Person Weapon Root"));
	FirstPersonWeaponRoot->SetupAttachment(GetFirstPersonCameraComponent());

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->DefaultLandMovementMode = MOVE_Flying;
		MoveComp->SetMovementMode(MOVE_Flying);
		MoveComp->GravityScale = 0.0f;
		MoveComp->MaxFlySpeed = MoveSpeed;
		MoveComp->BrakingDecelerationFlying = Deceleration;
	}

	DistanceComponent = CreateDefaultSubobject<UPartnerDistanceComponent>(TEXT("DistanceComponent"));
	MovementComponent = CreateDefaultSubobject<UPartnerMovementComponent>(TEXT("MovementComponent"));
	SupportComponent  = CreateDefaultSubobject<UPartnerSupportComponent> (TEXT("SupportComponent"));
	CombatComponent   = CreateDefaultSubobject<UPartnerCombatComponent>  (TEXT("CombatComponent"));
	HackComponent     = CreateDefaultSubobject<UPartnerHackComponent>    (TEXT("HackComponent"));
	EMPComponent      = CreateDefaultSubobject<UPartnerEMPComponent>     (TEXT("EMPComponent"));
	UpgradeComponent  = CreateDefaultSubobject<UOutlierUpgradeComponent> (TEXT("UpgradeComponent"));
	if (UpgradeComponent)
	{
		UpgradeComponent->SetUpgradeRole(EOutlierUpgradeRole::Partner);
	}

	FaceSpriteAnimationComponent = CreateDefaultSubobject<UPartnerSpriteAnimationComponent>(TEXT("SpriteAnimationComponent"));
}

USkeletalMeshComponent* APartnerCharacter::GetWeaponMuzzleComponent(bool bFirstPerson) const
{
	const FName MuzzleSocketName = GetWeaponMuzzleSocketName(bFirstPerson);
	if (const AWeaponBase* Weapon = GetCurrentWeapon())
	{
		USkeletalMeshComponent* WeaponMesh = bFirstPerson
			? Weapon->GetFirstPersonWeaponMesh()
			: Weapon->GetThirdPersonWeaponMesh();
		if (WeaponMesh && WeaponMesh->DoesSocketExist(MuzzleSocketName))
		{
			return WeaponMesh;
		}
	}

	return bFirstPerson ? GetFirstPersonMesh() : GetMesh();
}

void APartnerCharacter::OnRep_DroneMovementState()
{
	OnDroneMovementStateChanged.Broadcast(MovementState);
}

void APartnerCharacter::OnRep_MoveMode()
{
	if (MovementComponent)
	{
		MovementComponent->OnMoveModeChanged(MoveMode);
	}
}

void APartnerCharacter::OnRep_IsAccelerate()
{
	ApplyAccelerateState(bIsAccelerate);
}

void APartnerCharacter::SetShooterCharacter(AShooterCharacter* NewShooter)
{
	CachedShooterCharacter = NewShooter;

	if (MovementComponent)
	{
		MovementComponent->RefreshCharacterRefsFromPlayerState();
	}

	if (DistanceComponent)
	{
		DistanceComponent->RefreshCharacterRefsFromPlayerState();
	}

	if (SupportComponent)
	{
		SupportComponent->RefreshCharacterRefsFromPlayerState();
	}

	HackComponent = GetRuntimeHackComponent();
	if (HackComponent)
	{
		HackComponent->RefreshCharacterRefsFromPlayerState();
	}

	EMPComponent = GetRuntimeEMPComponent();
	if (EMPComponent)
	{
		EMPComponent->RefreshCharacterRefsFromPlayerState();
	}
}

void APartnerCharacter::ConfigureSuitDisableBoundaryRadius(float Radius)
{
	checkf(Radius > 0.0f, TEXT("Partner suit disable boundary radius must be positive."));
	SuitDisableBoundaryRadius = Radius;
}

void APartnerCharacter::NotifyOffensiveActionExecuted()
{
	if (HasAuthority() && CachedShooterCharacter)
	{
		CachedShooterCharacter->EndActiveStealth(true);
	}
}

void APartnerCharacter::ClientNotifySkillUseResult_Implementation(EPartnerSkillType SkillType, EPartnerSkillUseResult Result)
{
	OnPartnerSkillUseResult.Broadcast(SkillType, Result);
}

float APartnerCharacter::GetCurrentInertialCameraRollDegrees() const
{
	return MovementComponent
		? MovementComponent->GetCurrentCameraRollDegrees()
		: 0.0f;
}

void APartnerCharacter::SetMovementState(EDroneMovementState State)
{
	if (MovementState == State)
	{
		return;
	}

	MovementState = State;
	OnRep_DroneMovementState();
}

float APartnerCharacter::GetCurrentInertialCameraPitchDegrees() const
{
	return MovementComponent
		? MovementComponent->GetCurrentCameraPitchDegrees()
		: 0.0f;
}
