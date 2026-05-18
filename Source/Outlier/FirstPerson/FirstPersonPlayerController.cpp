// Fill out your copyright notice in the Description page of Project Settings.


#include "FirstPersonPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "FirstPersonPlayerCameraManager.h"
#include "OutlierGameMode.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Outlier.h"
#include "Shooter/ShooterCharacter.h"
#include "Network/OutlierArenaPoolSubsystem.h"

AFirstPersonPlayerController::AFirstPersonPlayerController()
{
	// set the player camera manager
	PlayerCameraManagerClass = AFirstPersonPlayerCameraManager::StaticClass();
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

	OutlierPlayerState->SetPlayerRole(DefaultPlayerRole);
	OutlierPlayerState->SetPairId(DefaultPairId);

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[OutlierInputDebug] InitializeOutlierPlayerState: PC=%s PS=%s Role=%d PairId=%d"),
		*GetNameSafe(this),
		*GetNameSafe(OutlierPlayerState),
		static_cast<int32>(DefaultPlayerRole),
		DefaultPairId
	);
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
