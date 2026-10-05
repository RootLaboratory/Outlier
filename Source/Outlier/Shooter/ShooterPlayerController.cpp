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
#include "Weapon/RangedWeaponBase.h"
#include "Weapon/FirstPickupDiagnostics.h"
#include "OutlierGameMode.h"
#include "OutlierPlayerState.h"
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
	UnbindSuitPlayerStateDelegates();
	UnbindShooterCharacterDelegates();
	CleanupPossessedShooterWeapons();

	// MainUI는 Base가 정리한다. AbilityUI는 이 Controller 소유이므로 여기서 정리.
	if (AbilityUIInstance)
	{
		AbilityUIInstance->OnAbilitySelected.RemoveDynamic(
			this,
			&AShooterPlayerController::HandleAbilitySelected);
		AbilityUIInstance->RemoveFromParent();
		AbilityUIInstance = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

void AShooterPlayerController::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	BindSuitPlayerStateDelegates();
	RefreshShooterSuitHUDFromPlayerState();
}

void AShooterPlayerController::BindSuitPlayerStateDelegates()
{
	if (!IsLocalController())
	{
		return;
	}

	AOutlierPlayerState* OutlierPlayerState = GetPlayerState<AOutlierPlayerState>();
	if (BoundSuitPlayerState == OutlierPlayerState)
	{
		return;
	}

	UnbindSuitPlayerStateDelegates();
	BoundSuitPlayerState = OutlierPlayerState;
	if (BoundSuitPlayerState)
	{
		BoundSuitPlayerState->OnAcquiredSuitChanged.AddUObject(
			this, &AShooterPlayerController::HandleAcquiredSuitChanged);
	}
}

void AShooterPlayerController::UnbindSuitPlayerStateDelegates()
{
	if (BoundSuitPlayerState)
	{
		BoundSuitPlayerState->OnAcquiredSuitChanged.RemoveAll(this);
		BoundSuitPlayerState = nullptr;
	}
}

void AShooterPlayerController::HandleAcquiredSuitChanged(AOutlierPlayerState* ChangedPlayerState)
{
	if (ChangedPlayerState == GetPlayerState<AOutlierPlayerState>())
	{
		RefreshShooterSuitHUDFromPlayerState();
	}
}

void AShooterPlayerController::RefreshShooterSuitHUDFromPlayerState()
{
	if (!IsLocalController())
	{
		return;
	}

	const AOutlierPlayerState* OutlierPlayerState = GetPlayerState<AOutlierPlayerState>();
	const bool bSuitAcquired = OutlierPlayerState && OutlierPlayerState->GetAcquiredSuit();

	ControlMainWidget(bSuitAcquired);
	if (ULocalPlayerUISubSystem* UISubsystem = GetLocalUISubsystem())
	{
		UISubsystem->OnShooterSuitAcquiredChanged(bSuitAcquired);
	}
	if (!bSuitAcquired && AbilityUIInstance)
	{
		AbilityUIInstance->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void AShooterPlayerController::ClientNotifyMeleeTargeted_Implementation(
	AActor* Target, AShooterCharacter* SourceShooter, bool bTargeted)
{
	// 이전 Pawn의 알림이 리스폰 후 새 Pawn의 표시를 바꾸지 않도록 한다.
	if (!IsLocalController() || !IsValid(SourceShooter) || GetPawn() != SourceShooter || !GetWorld())
	{
		return;
	}

	if (UMaterialPostProcessSubsystem* PPS = GetWorld()->GetSubsystem<UMaterialPostProcessSubsystem>())
	{
		if (bTargeted)
		{
			PPS->SetMeleeOutlineTarget(Target, SourceShooter);
		}
		else if (Target)
		{
			PPS->ClearMeleeOutlineTarget(Target);
		}
	}
}

void AShooterPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
}

void AShooterPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	BindSuitPlayerStateDelegates();
	RefreshShooterSuitHUDFromPlayerState();

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
	BindSuitPlayerStateDelegates();
	RefreshShooterSuitHUDFromPlayerState();
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
	// HUD 가시성은 Pawn이 없거나 아직 로컬 연결 전이어도 먼저 자기 PS로 적용한다.
	RefreshShooterSuitHUDFromPlayerState();

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

	BindSuitPlayerStateDelegates();
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
	// 새 HUD 등록 시 이전 캐시 대신 현재 PS의 슈트 상태를 적용한다.
	RefreshShooterSuitHUDFromPlayerState();
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

	RefreshShooterSuitHUDFromPlayerState();
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
	OutlierFirstPickup::FScope Scope(TEXT("WeaponHUD"), this);
	if (ULocalPlayerUISubSystem* UISubsystem = GetLocalUISubsystem())
	{
		UISubsystem->OnCurrentWeaponChanged(static_cast<EWidgetWeaponType>(NewType));

		// 권총 쿨타임은 무기를 내려도 GAS에서 계속 흐른다. 다시 권총을 들었을 때
		// 사격 당시 UI 알림에만 의존하지 않고 실제 남은 시간을 크로스헤어에 재투영한다.
		if (NewType == EWeaponType::Pistol && BoundShooterCharacter)
		{
			if (const ARangedWeaponBase* Pistol = Cast<ARangedWeaponBase>(BoundShooterCharacter->GetCurrentWeapon()))
			{
				const float TotalCooldown = Pistol->GetReuseCooldown();
				const float RemainingCooldown = Pistol->GetReuseCooldownRemaining();
				if (TotalCooldown > UE_KINDA_SMALL_NUMBER && RemainingCooldown > UE_KINDA_SMALL_NUMBER)
				{
					const float ElapsedCooldown = FMath::Max(0.0f, TotalCooldown - RemainingCooldown);
					UISubsystem->OnRep_ShootCrosshairChanged(TotalCooldown, ElapsedCooldown);
				}
			}
		}
	}

	// 무기가 바뀌면 탄약도 같이 바뀐다. 슈트 플래그(PlayerState)와 CurrentWeapon(Pawn)은
	// 서로 다른 액터에서 따로 복제돼 도착 순서가 보장되지 않으므로, 양쪽 신호에서 모두
	// 다시 투영해야 "권총 들고 슈트 입기 -> 라이플인데 권총 탄약" 같은 어긋남이 안 생긴다.
	if (BoundShooterCharacter)
	{
		BoundShooterCharacter->RefreshShooterAmmoUI();
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
