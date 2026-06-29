// Fill out your copyright notice in the Description page of Project Settings.


#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerInputConfig.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Drone/Partner/PartnerDistanceComponent.h"
#include "Drone/Partner/PartnerMovementComponent.h"
#include "Drone/Partner/PartnerSupportComponent.h"
#include "Drone/Partner/PartnerCombatComponent.h"
#include "Drone/Partner/PartnerHackComponent.h"
#include "Drone/Partner/PartnerEMPComponent.h"
#include "Net/UnrealNetwork.h"
#include "Components/SceneCaptureComponent2D.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Drone/DroneMoveDataRow.h"
#include "Drone/DroneControlDataRow.h"
#include "PostProcess/MaterialPostProcessSubsystem.h"
#include "PostProcess/OutlierPostProcessVolume.h"
#include "Drone/Partner/PartnerSkillCommonRow.h"
#include "Drone/Partner/PartnerSkillDataRow.h"
#include "Drone/Partner/PartnerSurvivalDataRow.h"
#include "Drone/Partner/PartnerCameraAssistDataRow.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Shooter/ShooterCharacter.h"
#include "OutlierPlayerState.h"
#include "LocalPlayerUISubSystem.h"
#include "PartnerAbilityComponent.h"
#include "TagDrivenUIGameplayTags.h"

void APartnerCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->SetMovementMode(MOVE_Flying);
		MoveComp->GravityScale = 0.0f;
		MoveComp->MaxFlySpeed = bIsAccelerate ? BoostSpeed : MoveSpeed;
		MoveComp->BrakingDecelerationFlying = Deceleration;
	}

	EnsurePartnerDataInitialized();
}


float APartnerCharacter::TakeDamage(
	float DamageAmount,
	FDamageEvent const& DamageEvent,
	AController* EventInstigator,
	AActor* DamageCauser)
{

	const float AppliedDamage = Super::TakeDamage(
		DamageAmount,
		DamageEvent,
		EventInstigator,
		DamageCauser
	);

	if (!HasAuthority() || AppliedDamage <= 0.0f || bIsInvincible || bIsRebooting)
	{
		return AppliedDamage;
	}

	HandlePartnerHit();
	return AppliedDamage;
}

void APartnerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

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
	if (CombatComponent)
	{
		CombatComponent->TryStartAttack();
	}
}

void APartnerCharacter::TryStopAttack()
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
	DOREPLIFETIME(APartnerCharacter, LastHackServerTime);
	DOREPLIFETIME(APartnerCharacter, bIsAccelerate);
	DOREPLIFETIME(APartnerCharacter, bIsRebooting);
	DOREPLIFETIME(APartnerCharacter, bIsInvincible);
	DOREPLIFETIME(APartnerCharacter, CurrentHitCount);
}

void APartnerCharacter::OnRep_CurrentHitCount()
{

	if (!IsLocallyControlled()) return;

	if (CurrentHitCount <= 0)
	{
		NullifyDamagedEvenet();
		return;
	}

	if (MaxHitCount <= 0)
	{
		return;
	}
	
	ApplyDamagedEvent(static_cast<float>(MaxHitCount-CurrentHitCount) / static_cast<float>(MaxHitCount));
}

void APartnerCharacter::AreaOfEffect()
{
	TryEMP();

	//ServerUseSkill(EPartnerSkillType::AreaOfEffect);
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
	UPartnerHackComponent* RuntimeHackComponent = GetRuntimeHackComponent();

	/*UE_LOG(LogTemp, Warning, TEXT("[PartnerHackDebug] TryHacking input Pawn=%s Local=%d Authority=%d CanAcceptInput=%d HackComponent=%s RuntimeHackComponent=%s MemberOwner=%s RuntimeOwner=%s"),
		*GetNameSafe(this),
		IsLocallyControlled() ? 1 : 0,
		HasAuthority() ? 1 : 0,
		CanAcceptInput() ? 1 : 0,
		*GetNameSafe(HackComponent),
		*GetNameSafe(RuntimeHackComponent),
		*GetNameSafe(HackComponent ? HackComponent->GetOwner() : nullptr),
		*GetNameSafe(RuntimeHackComponent ? RuntimeHackComponent->GetOwner() : nullptr));*/

	if (!CanAcceptInput())
	{
		return;
	}

	if (RuntimeHackComponent)
	{
		RuntimeHackComponent->TryHack();
	}

	//ServerUseSkill(EPartnerSkillType::Hack);
}

void APartnerCharacter::TryEMP()
{
	UPartnerEMPComponent* RuntimeEMPComponent = GetRuntimeEMPComponent();

	if (!CanAcceptInput() || !RuntimeEMPComponent)
	{
		return;
	}

	RuntimeEMPComponent->TryEMP();
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

void APartnerCharacter::TestAbilityScan()
{
	if (!CanAcceptInput())
	{
		return;
	}
	UE_LOG(LogTemp, Error, TEXT("Scan Valid"));


	TestAbilityComponent->TryActivateAbilityByTag(OutlierGameplayTags::Ability::Partner::Scan());
}

void APartnerCharacter::Scan()
{
	if (!CanAcceptInput())
	{
		return;
	}
	UE_LOG(LogTemp, Error, TEXT("Scan Valid"));

	ServerUseSkill(EPartnerSkillType::Scan);
}

void APartnerCharacter::Shield()
{
	if (!CanAcceptInput())
	{
		return;
	}

	UE_LOG(LogTemp, Error, TEXT("Shield Valid"));

	ServerUseSkill(EPartnerSkillType::Shield);
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

void APartnerCharacter::HandlePartnerHit()
{
	if (!HasAuthority() || MaxHitCount <= 0)
	{
		return;
	}

	++CurrentHitCount;
	bIsInvincible = true;

	GetWorldTimerManager().SetTimer(
		HitInvincibleTimerHandle,
		this,
		&APartnerCharacter::ClearHitInvincible,
		HitInvincibleTime,
		false
	);

	if (IsLocallyControlled())
	{
		OnRep_CurrentHitCount();
	}

	if (CurrentHitCount >= MaxHitCount)
	{
		StartReboot();
	}
}

void APartnerCharacter::StartReboot()
{
	if (!HasAuthority() || bIsRebooting)
	{
		return;
	}

	bIsRebooting = true;
	bIsInvincible = true;
	CurrentHitCount = 0;

	if (IsLocallyControlled())
	{
		OnRep_CurrentHitCount();
	}

	ApplyAccelerateState(false);

	if (MovementComponent)
	{
		MovementComponent->SetMoveInput(FVector2D::ZeroVector);
		MovementComponent->SetVerticalInput(0.0f);
		SetMoveMode(EPartnerMoveMode::Normal);
	}

	GetWorldTimerManager().ClearTimer(HitInvincibleTimerHandle);
	GetWorldTimerManager().SetTimer(
		RebootTimerHandle,
		this,
		&APartnerCharacter::FinishReboot,
		RebootTime,
		false
	);
}

void APartnerCharacter::FinishReboot()
{
	if (!HasAuthority())
	{
		return;
	}

	bIsRebooting = false;
	bIsInvincible = true;

	GetWorldTimerManager().SetTimer(
		RebootInvincibleTimerHandle,
		this,
		&APartnerCharacter::ClearRebootInvincible,
		InvincibleAfterRebootTime,
		false
	);
}

void APartnerCharacter::ClearHitInvincible()
{
	if (!bIsRebooting)
	{
		bIsInvincible = false;
	}
}

void APartnerCharacter::ClearRebootInvincible()
{
	bIsInvincible = false;
}

bool APartnerCharacter::CanAcceptInput() const
{
	return !bIsRebooting;
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
		MoveComp->MaxWalkSpeed = CurrentSpeed;
		MoveComp->MaxFlySpeed = CurrentSpeed;
	}

	if (MovementComponent)
	{
		MovementComponent->ResetMovementFeel();
	}
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

void APartnerCharacter::ServerUseSkill_Implementation(EPartnerSkillType SkillType)
{
	if (!CanAcceptInput() && !SupportComponent)
	{
		return;
	}

	switch (SkillType)
	{
	case EPartnerSkillType::AreaOfEffect:

			SupportComponent->TryAreaOfEffect_Server();

		break;
	case EPartnerSkillType::Shield:

			SupportComponent->TryShield_Server();
		break;

	case EPartnerSkillType::Scan:

			SupportComponent->TryScan_Server();

		break;
	case EPartnerSkillType::Hack:
		break;
	default:
		break;
	}
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
			CharacterMovementComp->MaxWalkSpeed = MoveSpeed;
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

	if (const FPartnerSkillDataRow* SkillDataRow = PartnerSkillDataRow.GetRow<FPartnerSkillDataRow>(TEXT("InitializeSkillData")))
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

		if (EMPComponent)
		{
			EMPComponent->EMPRange       = AreaOfEffectRange;
			EMPComponent->EMPMarkingTime = EMPMarkingTime;
		}

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

	if (const FPartnerSurvivalDataRow* SurvivalDataRow = PartnerSurvivalDataRow.GetRow<FPartnerSurvivalDataRow>(TEXT("InitializeSurvivalData")))
	{
		MaxHitCount				  = SurvivalDataRow->MaxHitCount;
		RebootTime				  = SurvivalDataRow->RebootTime;
		InvincibleAfterRebootTime = SurvivalDataRow->InvincibleAfterRebootTime;
		HitInvincibleTime		  = SurvivalDataRow->HitInvincibleTime;
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

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->DefaultLandMovementMode = MOVE_Flying;
		MoveComp->SetMovementMode(MOVE_Flying);
		MoveComp->GravityScale = 0.0f;
		MoveComp->MaxWalkSpeed = MoveSpeed;
		MoveComp->MaxFlySpeed = MoveSpeed;
		MoveComp->BrakingDecelerationFlying = Deceleration;
	}
	CaptureComponent = CreateDefaultSubobject< USceneCaptureComponent2D>(TEXT("PartnerCameraCapture"));
	CaptureComponent->SetupAttachment(RootComponent);
	DistanceComponent = CreateDefaultSubobject<UPartnerDistanceComponent>(TEXT("DistanceComponent"));
	MovementComponent = CreateDefaultSubobject<UPartnerMovementComponent>(TEXT("MovementComponent"));
	SupportComponent  = CreateDefaultSubobject<UPartnerSupportComponent> (TEXT("SupportComponent"));
	TestAbilityComponent = CreateDefaultSubobject<UPartnerAbilityComponent>(TEXT("AbilityComponent"));
	CombatComponent   = CreateDefaultSubobject<UPartnerCombatComponent>  (TEXT("CombatComponent"));
	HackComponent     = CreateDefaultSubobject<UPartnerHackComponent>    (TEXT("HackComponent"));
	EMPComponent      = CreateDefaultSubobject<UPartnerEMPComponent>     (TEXT("EMPComponent"));
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

void APartnerCharacter::ClientNotifySkillUseResult_Implementation(EPartnerSkillType SkillType, EPartnerSkillUseResult Result)
{
	OnPartnerSkillUseResult.Broadcast(SkillType, Result);

	if (Result != EPartnerSkillUseResult::Success)
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

	ULocalPlayerUISubSystem* SubSystem = LP->GetSubsystem<ULocalPlayerUISubSystem>();
	if (!SubSystem)
	{
		return;
	}

	FGameplayTag AbilityTag;
	float CoolTime = 0.f;

	switch (SkillType)
	{
	case EPartnerSkillType::Shield:
		AbilityTag = TagDrivenUITags::Ability::Partner::Shield();
		CoolTime   = ShieldCooldown;
		break;
	case EPartnerSkillType::Scan:
		AbilityTag = TagDrivenUITags::Ability::Partner::Scan();
		CoolTime   = ScanCooldown;
		break;
	case EPartnerSkillType::Hack:
		AbilityTag = TagDrivenUITags::Ability::Partner::Hacking();
		CoolTime   = HackCooldown;
		break;
	case EPartnerSkillType::AreaOfEffect:
		AbilityTag = TagDrivenUITags::Ability::Partner::EMP();
		CoolTime   = AreaOfEffectCooldown;
		break;
	default:
		return;
	}

	SubSystem->OnAbilityUsed(AbilityTag, CoolTime);
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
