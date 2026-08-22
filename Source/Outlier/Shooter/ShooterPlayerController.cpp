// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterPlayerController.h"

#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "LocalPlayerUISubSystem.h"
#include "LocalPlayerPostProcessSubsystem.h"
#include "UI/ShooterAbilityUI.h"
#include "PostProcess/MaterialPostProcessSubsystem.h"
#include "ShooterCharacter.h"
#include "ShooterInventoryComponent.h"
#include "ShooterMainWidget.h"
#include "OutlierGameMode.h"
#include "UI/LocalPlayerUILayerSubsystem.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GAS/Attributes/OutlierShieldAttributeSet.h"
#include "GAS/Attributes/OutlierVitalAttributeSet.h"

AShooterPlayerController::AShooterPlayerController()
{
	DefaultPlayerRole = EOutlierPlayerRole::Shooter;
}

void AShooterPlayerController::SocketDistanceUpdate(float Distance)
{
	if (ULocalPlayer* LP = this->GetLocalPlayer())
	{
		if (ULocalPlayerPostProcessSubsystem* PPSubsystem = LP->GetSubsystem<ULocalPlayerPostProcessSubsystem>())
		{
			PPSubsystem->SetADSSocketDistance(Distance);
		}
	}
}

void AShooterPlayerController::BeginPlay()
{
	Super::BeginPlay();
	BindMainUI();
}

void AShooterPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindShooterCharacterDelegates();
	CleanupPossessedShooterWeapons();

	Super::EndPlay(EndPlayReason);
}

void AShooterPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
}

void AShooterPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (!InPawn)
	{
		return;
	}

	if (AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(InPawn))
	{
		ShooterCharacter->Tags.Add(PlayerPawnTag);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ShooterPC] OnPossess"));	
	}

	if (IsLocalController())
	{
		if (UMaterialPostProcessSubsystem* PPS = GetWorld()->GetSubsystem<UMaterialPostProcessSubsystem>())
		{
			PPS->Refresh();
		}
	}


	/*if (AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(InPawn))
	{

		if (IsLocalController())
		{
			UE_LOG(LogTemp, Error, TEXT("TryRefresh"));
			ShooterCharacter->RefreshPostProcessState();
		}
	}*/
	
}
void AShooterPlayerController::AcknowledgePossession(APawn* P) 
{
	Super::AcknowledgePossession(P);
	BindShooterCharacterDelegates(Cast<AShooterCharacter>(P));

	if (IsLocalController())
	{
		if (UMaterialPostProcessSubsystem* PPS = GetWorld()->GetSubsystem<UMaterialPostProcessSubsystem>())
		{
			PPS->Refresh();
		}
	}
}

void AShooterPlayerController::BindShooterCharacterDelegates(AShooterCharacter* ShooterCharacter)
{
	UnbindShooterCharacterDelegates();

	if (!ShooterCharacter)
	{
		return;
	}

	BoundShooterCharacter = ShooterCharacter;
	BoundShooterAbilitySystem = ShooterCharacter->GetOutlierAbilitySystemComponent();

	ShooterCharacter->OnMovementStateChanged.AddDynamic(
		this,
		&AShooterPlayerController::HandleMovementStateChanged
	);

	ShooterCharacter->OnWeaponChanged.AddDynamic(
		this,
		&AShooterPlayerController::OnWeaponChanged
	);

	if (BoundShooterAbilitySystem)
	{
		HealthChangedHandle = BoundShooterAbilitySystem->GetGameplayAttributeValueChangeDelegate(
			UOutlierVitalAttributeSet::GetHealthAttribute()).AddUObject(
				this, &AShooterPlayerController::HandleShooterHealthAttributeChanged);
		MaxHealthChangedHandle = BoundShooterAbilitySystem->GetGameplayAttributeValueChangeDelegate(
			UOutlierVitalAttributeSet::GetMaxHealthAttribute()).AddUObject(
				this, &AShooterPlayerController::HandleShooterHealthAttributeChanged);
		ShieldChangedHandle = BoundShooterAbilitySystem->GetGameplayAttributeValueChangeDelegate(
			UOutlierShieldAttributeSet::GetShieldAttribute()).AddUObject(
				this, &AShooterPlayerController::HandleShooterShieldAttributeChanged);
		MaxShieldChangedHandle = BoundShooterAbilitySystem->GetGameplayAttributeValueChangeDelegate(
			UOutlierShieldAttributeSet::GetMaxShieldAttribute()).AddUObject(
				this, &AShooterPlayerController::HandleShooterShieldAttributeChanged);
		PartnerShieldChangedHandle = BoundShooterAbilitySystem->GetGameplayAttributeValueChangeDelegate(
			UOutlierShieldAttributeSet::GetPartnerShieldAttribute()).AddUObject(
				this, &AShooterPlayerController::HandleShooterPartnerShieldAttributeChanged);
		MaxPartnerShieldChangedHandle = BoundShooterAbilitySystem->GetGameplayAttributeValueChangeDelegate(
			UOutlierShieldAttributeSet::GetMaxPartnerShieldAttribute()).AddUObject(
				this, &AShooterPlayerController::HandleShooterPartnerShieldAttributeChanged);
	}

	ShooterCharacter->OnShooterDynamicCrosshairChanged.AddUObject(
		this,
		&AShooterPlayerController::HandleShooterDynamicCrosshair
	);

	ShooterCharacter->OnShooterAimingBlur.AddUObject(
		this, &AShooterPlayerController::HandleShooterAimingBlur
	);

	RefreshShooterVitalityUI();
	OnWeaponChanged(ShooterCharacter->GetWeaponType());
	RefreshShooterSuitUI();
}

void AShooterPlayerController::UnbindShooterCharacterDelegates()
{
	if (!BoundShooterCharacter && !BoundShooterAbilitySystem)
	{
		return;
	}

	if (BoundShooterCharacter)
	{
		BoundShooterCharacter->OnMovementStateChanged.RemoveAll(this);
		BoundShooterCharacter->OnWeaponChanged.RemoveAll(this);
		BoundShooterCharacter->OnShooterDynamicCrosshairChanged.RemoveAll(this);
		BoundShooterCharacter->OnShooterAimingBlur.RemoveAll(this);
	}
	if (BoundShooterAbilitySystem)
	{
		BoundShooterAbilitySystem->GetGameplayAttributeValueChangeDelegate(
			UOutlierVitalAttributeSet::GetHealthAttribute()).Remove(HealthChangedHandle);
		BoundShooterAbilitySystem->GetGameplayAttributeValueChangeDelegate(
			UOutlierVitalAttributeSet::GetMaxHealthAttribute()).Remove(MaxHealthChangedHandle);
		BoundShooterAbilitySystem->GetGameplayAttributeValueChangeDelegate(
			UOutlierShieldAttributeSet::GetShieldAttribute()).Remove(ShieldChangedHandle);
		BoundShooterAbilitySystem->GetGameplayAttributeValueChangeDelegate(
			UOutlierShieldAttributeSet::GetMaxShieldAttribute()).Remove(MaxShieldChangedHandle);
		BoundShooterAbilitySystem->GetGameplayAttributeValueChangeDelegate(
			UOutlierShieldAttributeSet::GetPartnerShieldAttribute()).Remove(PartnerShieldChangedHandle);
		BoundShooterAbilitySystem->GetGameplayAttributeValueChangeDelegate(
			UOutlierShieldAttributeSet::GetMaxPartnerShieldAttribute()).Remove(MaxPartnerShieldChangedHandle);
	}
	BoundShooterAbilitySystem = nullptr;
	HealthChangedHandle.Reset();
	MaxHealthChangedHandle.Reset();
	ShieldChangedHandle.Reset();
	MaxShieldChangedHandle.Reset();
	PartnerShieldChangedHandle.Reset();
	MaxPartnerShieldChangedHandle.Reset();
	BoundShooterCharacter = nullptr;
}

void AShooterPlayerController::RefreshShooterVitalityUI()
{
	if (!BoundShooterCharacter)
	{
		return;
	}

	HandleShooterHealthChanged(BoundShooterCharacter->GetCurHealth(), BoundShooterCharacter->GetMaxHealth());
	HandleShooterShieldChanged(BoundShooterCharacter->GetCurShield(), BoundShooterCharacter->GetMaxShield());
	HandleShooterPartnerShieldChanged(
		BoundShooterCharacter->GetCurPartnerShield(),
		BoundShooterCharacter->GetMaxPartnerShield());
	HandleShooterConditionChanged(BoundShooterCharacter->GetShooterConditionTagForUI());
}

void AShooterPlayerController::RefreshShooterSuitUI()
{
	if (!BoundShooterCharacter)
	{
		return;
	}

	// The controller keeps its widgets across respawn, while the new Pawn owns a
	// fresh ASC. Clear presentation cached from the old Pawn before projecting the
	// new Pawn's selected ability, availability, and actual cooldown effects.
	if (AbilityUIInstance)
	{
		AbilityUIInstance->ResetCooldowns();
	}
	if (ULocalPlayerUISubSystem* UISubsystem = GetLocalUISubsystem())
	{
		UISubsystem->ResetShooterAbilityState(BoundShooterCharacter->GetSelectedAbilityTag());
	}

	BoundShooterCharacter->RefreshShooterSuitUI();
}

void AShooterPlayerController::HandleShooterHealthAttributeChanged(const FOnAttributeChangeData& ChangeData)
{
	(void)ChangeData;
	if (BoundShooterCharacter)
	{
		HandleShooterHealthChanged(BoundShooterCharacter->GetCurHealth(), BoundShooterCharacter->GetMaxHealth());
		HandleShooterConditionChanged(BoundShooterCharacter->GetShooterConditionTagForUI());
	}
}

void AShooterPlayerController::HandleShooterShieldAttributeChanged(const FOnAttributeChangeData& ChangeData)
{
	(void)ChangeData;
	if (BoundShooterCharacter)
	{
		HandleShooterShieldChanged(BoundShooterCharacter->GetCurShield(), BoundShooterCharacter->GetMaxShield());
		HandleShooterConditionChanged(BoundShooterCharacter->GetShooterConditionTagForUI());
	}
}

void AShooterPlayerController::HandleShooterPartnerShieldAttributeChanged(const FOnAttributeChangeData& ChangeData)
{
	(void)ChangeData;
	if (BoundShooterCharacter)
	{
		HandleShooterPartnerShieldChanged(
			BoundShooterCharacter->GetCurPartnerShield(),
			BoundShooterCharacter->GetMaxPartnerShield());
		HandleShooterConditionChanged(BoundShooterCharacter->GetShooterConditionTagForUI());
	}
}

ULocalPlayerUISubSystem* AShooterPlayerController::GetLocalUISubsystem() const
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	return LocalPlayer ? LocalPlayer->GetSubsystem<ULocalPlayerUISubSystem>() : nullptr;
}

void AShooterPlayerController::HandleShooterHealthChanged(float CurrentHealth, float MaxHealth)
{
	if (ULocalPlayerUISubSystem* UISubsystem = GetLocalUISubsystem())
	{
		UISubsystem->OnRep_HealthChanged(CurrentHealth, MaxHealth);
	}
}

void AShooterPlayerController::HandleShooterShieldChanged(float CurrentShield, float MaxShield)
{
	if (ULocalPlayerUISubSystem* UISubsystem = GetLocalUISubsystem())
	{
		UISubsystem->OnRep_ShieldChanged(CurrentShield, MaxShield);
	}
}

void AShooterPlayerController::HandleShooterPartnerShieldChanged(float CurrentPartnerShield, float MaxPartnerShield)
{
	if (ULocalPlayerUISubSystem* UISubsystem = GetLocalUISubsystem())
	{
		UISubsystem->OnRep_PartnerShieldChanged(CurrentPartnerShield, MaxPartnerShield);
	}
}

void AShooterPlayerController::HandleShooterConditionChanged(const FGameplayTag& ConditionTag)
{
	if (ULocalPlayerUISubSystem* UISubsystem = GetLocalUISubsystem())
	{
		UISubsystem->OnRep_ShooterHPStateChanged(ConditionTag);
	}
}

void AShooterPlayerController::HandleShooterDynamicCrosshair(bool InFlag)
{
	if (ULocalPlayerUISubSystem* UISubsystem = GetLocalUISubsystem())
	{
		UISubsystem->OnRep_ShooterDynamicCrosshairChanged(InFlag);
	}
}

void AShooterPlayerController::ReceivedPlayer()
{
	Super::ReceivedPlayer();

	if (!IsLocalController())
	{
		return;
	}

	BindMainUI();
	BindPostProcessSubSystem();

}

void AShooterPlayerController::CleanupPossessedShooterWeapons()
{
	if (!HasAuthority())
	{
		return;
	}

	if (AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(GetPawn()))
	{
		ShooterCharacter->CleanupOwnedWeapons();
	}
}

void AShooterPlayerController::BindMainUI()
{
	if (!IsLocalController())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ShooterPC] BindMainUI skipped: not local PC=%s Auth=%d"),
			*GetNameSafe(this),
			HasAuthority() ? 1 : 0);
		return;
	}

	if (ShooterUIInstance)
	{
		RefreshShooterSuitUI();
		UE_LOG(LogTemp, Warning,
			TEXT("[ShooterPC] BindMainUI skipped: already exists PC=%s UI=%s"),
			*GetNameSafe(this),
			*GetNameSafe(ShooterUIInstance));
		return;
	}

	if (!MainUIClass)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ShooterPC] BindMainUI failed: MainUIClass is null PC=%s Class=%s"),
			*GetNameSafe(this),
			*GetNameSafe(GetClass()));
		return;
	}

	ShooterUIInstance = CreateWidget<UMainUIBase>(this, MainUIClass);

	if (!ShooterUIInstance)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ShooterPC] BindMainUI failed: CreateWidget returned null PC=%s MainUIClass=%s"),
			*GetNameSafe(this),
			*GetNameSafe(MainUIClass));
		return;
	}

	ShooterUIInstance->AddToViewport();
	UE_LOG(LogTemp, Warning,
		TEXT("[ShooterPC] MainUI added to viewport PC=%s UI=%s MainUIClass=%s"),
		*GetNameSafe(this),
		*GetNameSafe(ShooterUIInstance),
		*GetNameSafe(MainUIClass));

	if (ULocalPlayer* LP = this->GetLocalPlayer())
	{
		if (ULocalPlayerUISubSystem* UISubsystem = LP->GetSubsystem<ULocalPlayerUISubSystem>())
		{
			UISubsystem->RegisterMainUI(ShooterUIInstance);
			UE_LOG(LogTemp, Warning,
				TEXT("[ShooterPC] MainUI registered to UISubsystem PC=%s UI=%s"),
				*GetNameSafe(this),
				*GetNameSafe(ShooterUIInstance));
		}

		//Layer
		if (ULocalPlayerUILayerSubsystem* LayerSubsystem =
			LP->GetSubsystem<ULocalPlayerUILayerSubsystem>())
		{
			LayerSubsystem->RegisterMainUI(ShooterUIInstance);
		}
	}

	if (AbilityUIInstance)
	{
		return;
	}

	if (!AbilityUIClass)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ShooterPC] BindMainUI skipped: AbilityUIClass is null PC=%s"),
			*GetNameSafe(this));
		return;
	}

	AbilityUIInstance = CreateWidget<UShooterAbilityUI>(this, AbilityUIClass);
	if (!AbilityUIInstance)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ShooterPC] BindMainUI failed: Create AbilityUI returned null PC=%s AbilityUIClass=%s"),
			*GetNameSafe(this),
			*GetNameSafe(AbilityUIClass));
		return;
	}

	AbilityUIInstance->AddToViewport();
	AbilityUIInstance->OnAbilitySelected.AddDynamic(
		this,
		&AShooterPlayerController::HandleAbilitySelected
	);
	AbilityUIInstance->SetVisibility(ESlateVisibility::Collapsed);
	UE_LOG(LogTemp, Warning,
		TEXT("[ShooterPC] AbilityUI added PC=%s UI=%s AbilityUIClass=%s"),
		*GetNameSafe(this),
		*GetNameSafe(AbilityUIInstance),
		*GetNameSafe(AbilityUIClass));
	RefreshShooterSuitUI();
}

void AShooterPlayerController::BindPostProcessSubSystem()
{
	if (ULocalPlayer* LP = this->GetLocalPlayer())
	{
		if (ULocalPlayerPostProcessSubsystem* PPSubsystem = LP->GetSubsystem<ULocalPlayerPostProcessSubsystem>())
		{
			//PPSubsystem->ActivateSlideState();
		}
	}
}

void AShooterPlayerController::HandleMovementStateChanged(EMovementState NewState)
{
	if (ULocalPlayer* LP = GetLocalPlayer())
	{
		if (ULocalPlayerUISubSystem* UISubsystem = LP->GetSubsystem<ULocalPlayerUISubSystem>())
		{
			//UE_LOG(LogTemp, Error, TEXT("HandleMovementStateChanged %d"), NewState));
			switch (NewState)
			{
			case EMovementState::Jump:
				UISubsystem->OnRep_PlayerStateChanged(EUIPlayerState::Jump);
				break;
			case EMovementState::Slide:
				UISubsystem->OnRep_PlayerStateChanged(EUIPlayerState::Slide);
				break;
			case EMovementState::Walk:
			case EMovementState::Run:
			case EMovementState::Crouch:
				UISubsystem->OnRep_PlayerStateChanged(EUIPlayerState::Move);
				break;
			default:
				UISubsystem->OnRep_PlayerStateChanged(EUIPlayerState::Idle);
				break;
			}
		}
	}
}

void AShooterPlayerController::OnWeaponChanged(EWeaponType NewType)
{
	if (ULocalPlayerUISubSystem* UISubsystem = GetLocalUISubsystem())
	{
		UISubsystem->OnCurrentWeaponChanged(static_cast<EWidgetWeaponType>(NewType));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Shooter UI subsystem is not ready"));
	}
}

void AShooterPlayerController::HandleAbilitySelected(FGameplayTag AbilityTag)
{
	if (!AbilityTag.IsValid())
	{
		return;
	}

	if (ULocalPlayerUISubSystem* UISubsystem = GetLocalUISubsystem())
	{
		UISubsystem->OnCurrentAbilityChanged(AbilityTag);
	}
}

void AShooterPlayerController::HandleShooterAimingBlur(bool InFlag, int32 WeaponStencilValue)
{
	if (ULocalPlayer* LP = this->GetLocalPlayer())
	{
		if (ULocalPlayerPostProcessSubsystem* PPSubsystem = LP->GetSubsystem<ULocalPlayerPostProcessSubsystem>())
		{
			PPSubsystem->SetADSBlurAiming(InFlag, WeaponStencilValue);
		}
	}
}
