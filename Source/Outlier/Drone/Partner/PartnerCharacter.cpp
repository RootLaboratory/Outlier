// Fill out your copyright notice in the Description page of Project Settings.


#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerInputConfig.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Drone/Partner/PartnerMovementComponent.h"
#include "Drone/Partner/PartnerSupportComponent.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Drone/DroneMoveDataRow.h"
#include "Drone/DroneControlDataRow.h"
#include "Drone/Partner/PartnerSkillCommonRow.h"
#include "Drone/Partner/PartnerSkillDataRow.h"
#include "Drone/Partner/PartnerSurvivalDataRow.h"
#include "Drone/Partner/PartnerCameraAssistDataRow.h"
#include "Shooter/ShooterCharacter.h"
#include "OutlierPlayerState.h"

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

	if (HasAuthority())
	{
		GetWorldTimerManager().SetTimer(
			BoundaryCheckTimerHandle,
			this,
			&APartnerCharacter::UpdateBoundaryByTimer,
			0.1f,
			true
		);
	}
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
		MovementComponent->RefreshMovementState();
	}
}

void APartnerCharacter::OnMoveInputUpdated(const FVector2D& MoveValue)
{
	Super::OnMoveInputUpdated(MoveValue);
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
	DOREPLIFETIME(APartnerCharacter, bIsRebooting);
	DOREPLIFETIME(APartnerCharacter, bIsInvincible);
	DOREPLIFETIME(APartnerCharacter, CurrentHitCount);
}

void APartnerCharacter::AreaOfEffect()
{
	if (!CanAcceptInput())
	{
		return;
	}

	ServerUseSkill(EPartnerSkillType::AreaOfEffect);
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

	ServerUseSkill(EPartnerSkillType::Hack);
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

	ServerUseSkill(EPartnerSkillType::Scan);
}

void APartnerCharacter::Shield()
{
	if (!CanAcceptInput())
	{
		return;
	}

	ServerUseSkill(EPartnerSkillType::Shield);
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

	bIsAccelerate = !bIsAccelerate;

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->MaxWalkSpeed = bIsAccelerate ? BoostSpeed : MoveSpeed;
		MoveComp->MaxFlySpeed = bIsAccelerate ? BoostSpeed : MoveSpeed;
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

void APartnerCharacter::UpdateBoundaryByTimer()
{
	if (MovementComponent)
	{
		MovementComponent->UpdateBoundaryState();
	}
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
	bIsAccelerate = false;

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

void APartnerCharacter::SetMoveMode(EPartnerMoveMode NewMode)
{
	if (!CanAcceptInput() && NewMode != EPartnerMoveMode::Normal)
	{
		return;
	}

	if (MoveMode == NewMode)
	{
		return;
	}

	MoveMode = NewMode;

	if (!HasAuthority())
	{
		ServerSetMoveMode(NewMode);
	}

	if (MovementComponent)
	{
		MovementComponent->RefreshTickEnabled();
	}
}

void APartnerCharacter::ServerUseSkill_Implementation(EPartnerSkillType SkillType)
{
	if (!SupportComponent || !CanAcceptInput())
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
	default:
		break;
	}
}

void APartnerCharacter::ServerHackTarget_Implementation(AActor* TargetActor)
{
	if (!SupportComponent || !TargetActor || !CanAcceptInput())
	{
		return;
	}

	SupportComponent->TryHack_Server(TargetActor);
}

void APartnerCharacter::ServerSetMoveMode_Implementation(EPartnerMoveMode NewMode)
{
	MoveMode = NewMode;

	if (MovementComponent)
	{
		MovementComponent->OnMoveModeChanged(NewMode);
	}
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
		ScanRange				 = SkillDataRow->ScanRange;
		ScanDuration			 = SkillDataRow->ScanDuration;
		ScanCooldown			 = SkillDataRow->ScanCooldown;
		HackRange				 = SkillDataRow->HackRange;
		HackDuration			 = SkillDataRow->HackDuration;
		HackCooldown			 = SkillDataRow->HackCooldown;
		AreaOfEffectRange		 = SkillDataRow->AreaOfEffectRange;
		AreaOfEffectDuration	 = SkillDataRow->AreaOfEffectDuration;
		AreaOfEffectCooldown	 = SkillDataRow->AreaOfEffectCooldown;
		ShieldRange				 = SkillDataRow->ShieldRange;
		ShieldDuration			 = SkillDataRow->ShieldDuration;
		ShieldCooldown			 = SkillDataRow->ShieldCooldown;
		ShieldAmount			 = SkillDataRow->ShieldAmount;
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
			ControlRot.Pitch + PitchInput,
			PitchMin,
			PitchMax
		);

		PC->SetControlRotation(ControlRot);
	}

	const float TargetRoll = -LookAxis.X * CameraRollOnTurn;
	LookRollInput = FMath::Clamp(-TargetRoll / FMath::Max(CameraRollOnTurn, KINDA_SMALL_NUMBER), -1.0f, 1.0f);

	if (MovementComponent)
	{
		MovementComponent->RefreshTickEnabled();
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

	MovementComponent = CreateDefaultSubobject<UPartnerMovementComponent>(TEXT("MovementComponent"));
	SupportComponent  = CreateDefaultSubobject<UPartnerSupportComponent> (TEXT("SupportComponent"));
}

void APartnerCharacter::OnRep_DroneMovementState()
{
	OnDroneMovementStateChanged.Broadcast(MovementState);
}

void APartnerCharacter::OnRep_MoveMode()
{
}

void APartnerCharacter::SetShooterCharacter(AShooterCharacter* NewShooter)
{
	CachedShooterCharacter = NewShooter;

	if (MovementComponent)
	{
		MovementComponent->RefreshCharacterRefsFromPlayerState();
	}

	if (SupportComponent)
	{
		SupportComponent->RefreshCharacterRefsFromPlayerState();
	}
}
