// Fill out your copyright notice in the Description page of Project Settings.


#include "FirstPersonPlayerController.h"
#include "EnhancedInputDeveloperSettings.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "FirstPersonPlayerCameraManager.h"
#include "OutlierGameMode.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Outlier.h"
#include "MainUIBase.h"
#include "Shooter/ShooterCharacter.h"
#include "Network/OutlierArenaPoolSubsystem.h"
#include "UI/LocalPlayerUILayerSubsystem.h"

AFirstPersonPlayerController::AFirstPersonPlayerController()
{
	// set the player camera manager
	PlayerCameraManagerClass = AFirstPersonPlayerCameraManager::StaticClass();
}

void AFirstPersonPlayerController::ClientPushUILayer_Implementation(
	const FUILayerPushRequest& Request)
{
	if (!IsLocalController())
	{
		return;
	}

	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	if (!LayerSubsystem)
	{
		return;
	}

	LayerSubsystem->PushWidget(Request);
}

bool AFirstPersonPlayerController::SetFirstPersonInputMode(FGameplayTag NewInputMode)
{
	if (!IsLocalController() || !NewInputMode.IsValid())
	{
		return false;
	}

	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	if (!LocalPlayer)
	{
		return false;
	}

	UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
		LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!InputSubsystem)
	{
		return false;
	}

	const FGameplayTagContainer CurrentInputMode = InputSubsystem->GetInputMode();
	if (CurrentInputMode.Num() == 1 && CurrentInputMode.HasTagExact(NewInputMode))
	{
		CurrentFirstPersonInputMode = NewInputMode;
		return true;
	}

	FGameplayTagContainer InputModeContainer;
	InputModeContainer.AddTag(NewInputMode);

	FModifyContextOptions Options;
	Options.bIgnoreAllPressedKeysUntilRelease = true;
	Options.bForceImmediately = true;

	InputSubsystem->SetInputMode(InputModeContainer, Options);
	CurrentFirstPersonInputMode = NewInputMode;
	return true;
}

bool AFirstPersonPlayerController::TryRestoreFirstPersonDefaultInputMode(FGameplayTag ExpectedInputMode)
{
	if (!IsLocalController() || !ExpectedInputMode.IsValid())
	{
		return false;
	}

	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	if (!LocalPlayer)
	{
		return false;
	}

	UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
		LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!InputSubsystem)
	{
		return false;
	}

	// A delayed cleanup from one ability must not replace a newer ability's input mode.
	const FGameplayTagContainer CurrentInputMode = InputSubsystem->GetInputMode();
	if (CurrentInputMode.Num() != 1 || !CurrentInputMode.HasTagExact(ExpectedInputMode))
	{
		return false;
	}

	return RestoreFirstPersonDefaultInputMode();
}

bool AFirstPersonPlayerController::RestoreFirstPersonDefaultInputMode()
{
	if (!IsLocalController())
	{
		return false;
	}

	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	if (!LocalPlayer)
	{
		return false;
	}

	UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
		LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!InputSubsystem)
	{
		return false;
	}

	const FGameplayTagContainer& DefaultInputMode =
		GetDefault<UEnhancedInputDeveloperSettings>()->DefaultInputMode;
	if (DefaultInputMode.IsEmpty())
	{
		return false;
	}

	FModifyContextOptions Options;
	Options.bIgnoreAllPressedKeysUntilRelease = true;
	Options.bForceImmediately = true;

	InputSubsystem->SetInputMode(DefaultInputMode, Options);
	CurrentFirstPersonInputMode = DefaultInputMode.First();

	SetInputMode(FInputModeGameOnly());
	bShowMouseCursor = false;
	return true;
}

FGameplayTag AFirstPersonPlayerController::GetFirstPersonInputMode() const
{
	 return CurrentFirstPersonInputMode; 
}

bool AFirstPersonPlayerController::IsFirstPersonInputMode(FGameplayTag InputMode) const
{
	return CurrentFirstPersonInputMode.MatchesTagExact(InputMode);
}

void AFirstPersonPlayerController::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[OutlierInputDebug] PC BeginPlay: %s Local=%d Authority=%d Pawn=%s MappingContexts=%d Role=%d PairId=%d"),
		*GetNameSafe(this),
		IsLocalPlayerController(),
		HasAuthority(),
		*GetNameSafe(GetPawn()),
		DefaultMappingContexts.Num(),
		static_cast<int32>(DefaultPlayerRole),
		DefaultPairId
	);

	InitializeOutlierPlayerState();
}

void AFirstPersonPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[OutlierInputDebug] PC OnPossess: %s Pawn=%s PawnClass=%s Authority=%d"),
		*GetNameSafe(this),
		*GetNameSafe(InPawn),
		InPawn ? *GetNameSafe(InPawn->GetClass()) : TEXT("None"),
		HasAuthority()
	);

	InitializeOutlierPlayerState();
	RegisterCurrentPawnWithPlayerState();
}

void AFirstPersonPlayerController::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	InitializeOutlierPlayerState();
}

void AFirstPersonPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[OutlierInputDebug] PC SetupInputComponent: %s Local=%d MappingContexts=%d"),
		*GetNameSafe(this),
		IsLocalPlayerController(),
		DefaultMappingContexts.Num()
	);

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		// Add Input Mapping Contexts
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("[OutlierInputDebug] AddMappingContext: PC=%s Context=%s"),
					*GetNameSafe(this),
					*GetNameSafe(CurrentContext)
				);

				if (CurrentContext)
				{
					const TArray<FEnhancedActionKeyMapping>& Mappings = CurrentContext->GetMappings();
					UE_LOG(
						LogTemp,
						Warning,
						TEXT("[OutlierInputDebug] MappingContextDump: Context=%s MappingCount=%d"),
						*GetNameSafe(CurrentContext),
						Mappings.Num()
					);

					for (const FEnhancedActionKeyMapping& Mapping : Mappings)
					{
						UE_LOG(
							LogTemp,
							Warning,
							TEXT("[OutlierInputDebug] Mapping: Context=%s Action=%s Key=%s Triggers=%d Modifiers=%d"),
							*GetNameSafe(CurrentContext),
							*GetNameSafe(Mapping.Action),
							*Mapping.Key.ToString(),
							Mapping.Triggers.Num(),
							Mapping.Modifiers.Num()
						);
					}
				}

				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			CurrentFirstPersonInputMode = Subsystem->GetInputMode().First();
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[OutlierInputDebug] EnhancedInputLocalPlayerSubsystem is null: %s"), *GetNameSafe(this));
		}
	}

}




TSubclassOf<UMainUIBase> AFirstPersonPlayerController::GetMainUIClass_Implementation() const
{
	return MainUIClass;
}

void AFirstPersonPlayerController::BindMainUI()
{
	
}

void AFirstPersonPlayerController::BindPostProcessSubSystem()
{
}

void AFirstPersonPlayerController::InitializeOutlierPlayerState()
{
	if (!HasAuthority())
	{
		return;
	}

	AOutlierPlayerState* OutlierPlayerState = GetPlayerState<AOutlierPlayerState>();
	if (!OutlierPlayerState)
	{
		UE_LOG(LogTemp, Warning, TEXT("[OutlierInputDebug] InitializeOutlierPlayerState failed: PlayerState null PC=%s"), *GetNameSafe(this));
		return;
	}

	//OutlierPlayerState->SetPlayerRole(DefaultPlayerRole);
	//OutlierPlayerState->SetPairId(DefaultPairId);

	//UE_LOG(
	//	LogTemp,
	//	Warning,
	//	TEXT("[OutlierInputDebug] InitializeOutlierPlayerState: PC=%s PS=%s Role=%d PairId=%d"),
	//	*GetNameSafe(this),
	//	*GetNameSafe(OutlierPlayerState),
	//	static_cast<int32>(DefaultPlayerRole),
	//	DefaultPairId
	//);
}

void AFirstPersonPlayerController::ClientArenaLoad_Implementation(int32 ArenaId)
{
	/*UE_LOG(LogTemp, Warning, TEXT("[ArenaReady][1] ClientArenaLoad PC=%s ArenaId=%d NetMode=%d"),
		*GetNameSafe(this), ArenaId, GetWorld() ? (int32)GetWorld()->GetNetMode() : -1);*/

	UOutlierArenaPoolSubsystem* ArenaPool = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaPoolSubsystem>()
		: nullptr;

	if (!ArenaPool)
	{
		//UE_LOG(LogTemp, Error, TEXT("[ArenaReady][1] ArenaPool is null"));
		return;
	}

	ArenaPool->EnsureArenaLoaded(ArenaId);

	if (ArenaPool->IsArenaReady(ArenaId))
	{
		//UE_LOG(LogTemp, Warning, TEXT("[ArenaReady][1] Arena already ready ArenaId=%d"), ArenaId);
		ServerNotifyArenaReady();
		return;
	}

	PendingArenaId = ArenaId;
	ArenaPool->OnArenaShown.AddUObject(this, &AFirstPersonPlayerController::HandleArenaShown);
	//UE_LOG(LogTemp, Warning, TEXT("[ArenaReady][1] Waiting for OnArenaShown ArenaId=%d"), ArenaId);
}

void AFirstPersonPlayerController::Server_RequestArenaReload_Implementation()
{
	AOutlierPlayerState* PS = GetPlayerState<AOutlierPlayerState>();
	UE_LOG(LogTemp, Warning, TEXT("[DebugReload] Server_RequestArenaReload PC=%s Auth=%d PS=%s ArenaId=%d"),
		*GetNameSafe(this), HasAuthority(), *GetNameSafe(PS), PS ? PS->GetArenaId() : -2);

	if (!PS || PS->GetArenaId() == INDEX_NONE)
	{
		UE_LOG(LogTemp, Error, TEXT("[DebugReload] Bail: PS null or ArenaId==INDEX_NONE"));
		return;
	}

	AOutlierGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AOutlierGameMode>() : nullptr;
	if (!GM)
	{
		UE_LOG(LogTemp, Error, TEXT("[DebugReload] Bail: AuthGameMode null (not on server?)"));
		return;
	}

	GM->DebugReloadArena(this);
}

void AFirstPersonPlayerController::ClientArenaReload_Implementation(int32 ArenaId)
{
	UOutlierArenaPoolSubsystem* ArenaPool = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaPoolSubsystem>()
		: nullptr;
	if (!ArenaPool)
	{
		return;
	}

	// 강제 리로드라 항상 새로 스트리밍 → 바인딩 먼저 걸고(레이스 방지) 재로드
	PendingArenaId = ArenaId;
	ArenaPool->OnArenaShown.AddUObject(this, &AFirstPersonPlayerController::HandleArenaShown);
	ArenaPool->EnsureArenaLoaded(ArenaId, /*bForceReload=*/true);
}

void AFirstPersonPlayerController::HandleArenaShown(int32 ShownArenaId)
{
	/*UE_LOG(LogTemp, Warning, TEXT("[ArenaReady][2] HandleArenaShown ShownArenaId=%d PendingArenaId=%d PC=%s"),
		ShownArenaId, PendingArenaId, *GetNameSafe(this));*/

	if (ShownArenaId != PendingArenaId)
	{
		return;
	}

	if (UOutlierArenaPoolSubsystem* ArenaPool = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaPoolSubsystem>()
		: nullptr)
	{
		ArenaPool->OnArenaShown.RemoveAll(this);
	}

	PendingArenaId = INDEX_NONE;
	ServerNotifyArenaReady();
}

void AFirstPersonPlayerController::ServerNotifyArenaReady_Implementation()
{
	/*UE_LOG(LogTemp, Warning, TEXT("[ArenaReady][4] ServerNotifyArenaReady PC=%s Auth=%d"),
		*GetNameSafe(this), HasAuthority());*/

	AOutlierGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AOutlierGameMode>() : nullptr;
	if (GameMode)
	{
		GameMode->OnClientArenaReady(this);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ArenaReady][4] GameMode is null"));
	}
}

void AFirstPersonPlayerController::RegisterCurrentPawnWithPlayerState()
{
	if (!HasAuthority())
	{
		return;
	}

	AOutlierPlayerState* OutlierPlayerState = GetPlayerState<AOutlierPlayerState>();
	if (!OutlierPlayerState)
	{
		UE_LOG(LogTemp, Warning, TEXT("[OutlierInputDebug] RegisterCurrentPawnWithPlayerState failed: PlayerState null PC=%s"), *GetNameSafe(this));
		return;
	}

	if (AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(GetPawn()))
	{
		UE_LOG(LogTemp, Warning, TEXT("[OutlierInputDebug] Register Shooter Pawn: %s"), *GetNameSafe(ShooterCharacter));
		OutlierPlayerState->SetPlayerRole(EOutlierPlayerRole::Shooter);
		OutlierPlayerState->SetShooterCharacter(ShooterCharacter);
	}
	else if (APartnerCharacter* PartnerCharacter = Cast<APartnerCharacter>(GetPawn()))
	{
		UE_LOG(LogTemp, Warning, TEXT("[OutlierInputDebug] Register Partner Pawn: %s"), *GetNameSafe(PartnerCharacter));
		OutlierPlayerState->SetPlayerRole(EOutlierPlayerRole::Partner);
		OutlierPlayerState->SetPartnerCharacter(PartnerCharacter);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[OutlierInputDebug] Register pawn skipped: Pawn=%s"), *GetNameSafe(GetPawn()));
	}

	if (AOutlierGameMode* GameMode = GetWorld()
		? GetWorld()->GetAuthGameMode<AOutlierGameMode>()
		: nullptr)
	{
		GameMode->RefreshPairLinks(OutlierPlayerState);
	}
}

void AFirstPersonPlayerController::ControlMainWidget(bool InFlag) const
{
	if (!IsLocalController())
	{
		return;
	}

	if (ShooterUIInstance)
	{
		ShooterUIInstance->ModulesControl(InFlag);
		ShooterUIInstance->AbilitySectionControl(InFlag);
	}
}

void AFirstPersonPlayerController::RefreshPostProcessState()
{
	
}
