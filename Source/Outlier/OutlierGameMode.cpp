// Copyright Epic Games, Inc. All Rights Reserved.

#include "OutlierGameMode.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "Shooter/ShooterCharacter.h"
#include "OutlierPlayerState.h"
#include "Save/OutlierCheckpoint.h"
#include "OutlierGameState.h"
#include "FrontendPlayerController.h"
#include "Network/OutlierArenaPoolSubsystem.h"
#include "Save/OutlierSaveSubSystem.h"
#include "GameFramework/GameStateBase.h"
#include "Shooter/ShooterPlayerController.h"
#include "Drone/Partner/PartnerPlayerController.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetConnection.h"
#include "Enemy/EnemyBase.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerState.h"

AOutlierGameMode::AOutlierGameMode()
{

}

void AOutlierGameMode::RegisterCheckpoint(AController* Controller, AOutlierCheckpoint* Checkpoint)
{
	if (!Controller || !Checkpoint)
	{
		return;
	}

	AOutlierPlayerState* PS = Controller->GetPlayerState<AOutlierPlayerState>();

	if (!PS)
	{
		return;
	}

	FOutlierCheckpointData Data;
	Data.LevelName = FName(*GetWorld()->GetMapName());
	Data.CheckpointId = Checkpoint->GetCheckpointId();

	ApplyCheckpointToPair(PS, Data);
}

void AOutlierGameMode::RefreshPairLinks(AOutlierPlayerState* TriggeringPlayerState)
{
	if (!TriggeringPlayerState)
	{
		return;
	}

	AOutlierPlayerState* ShooterPlayerState = TriggeringPlayerState->IsShooterPlayer()
		? TriggeringPlayerState
		: FindPairPlayerState(TriggeringPlayerState->GetPairId(), EOutlierPlayerRole::Shooter);

	AOutlierPlayerState* PartnerPlayerState = TriggeringPlayerState->IsPartnerPlayer()
		? TriggeringPlayerState
		: FindPairPlayerState(TriggeringPlayerState->GetPairId(), EOutlierPlayerRole::Partner);

	AShooterCharacter* Shooter = ShooterPlayerState
		? ShooterPlayerState->GetShooterCharacter()
		: nullptr;

	APartnerCharacter* Partner = PartnerPlayerState
		? PartnerPlayerState->GetPartnerCharacter()
		: nullptr;

	if (!Shooter && PartnerPlayerState)
	{
		Shooter = PartnerPlayerState->GetShooterCharacter();
	}

	if (!Partner && ShooterPlayerState)
	{
		Partner = ShooterPlayerState->GetPartnerCharacter();
	}

	RegisterSpawnedPair(ShooterPlayerState, PartnerPlayerState, Shooter, Partner);
}

void AOutlierGameMode::ApplyCheckpointToPair(AOutlierPlayerState* TriggeringPlayerState, const FOutlierCheckpointData& Data)
{
	if (!TriggeringPlayerState)
	{
		return;
	}

	const int32 PairId = TriggeringPlayerState->GetPairId();

	if (PairId == INDEX_NONE || !GameState)
	{
		TriggeringPlayerState->SetCheckpointData(Data);

		if (AController* Controller = GetControllerFromPlayerState(TriggeringPlayerState))
		{
			if (UOutlierSaveSubSystem* SaveSubsystem =
				GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>())
			{
				SaveSubsystem->SavePlayerCheckpoint(GetPlayerSaveId(Controller), Data);
			}
		}

		return;
	}

	for (APlayerState* RawPlayerState : GameState->PlayerArray)
	{
		AOutlierPlayerState* PairPlayerState = Cast<AOutlierPlayerState>(RawPlayerState);
		if (!PairPlayerState || PairPlayerState->GetPairId() != PairId)
		{
			continue;
		}

		PairPlayerState->SetCheckpointData(Data);

		if (AController* Controller = GetControllerFromPlayerState(PairPlayerState))
		{
			if (UOutlierSaveSubSystem* SaveSubsystem =
				GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>())
			{
				SaveSubsystem->SavePlayerCheckpoint(GetPlayerSaveId(Controller), Data);
			}
		}
	}
}

void AOutlierGameMode::HandlePlayerDeath(AShooterCharacter* Character)
{
	if (!Character)
	{
		return;
	}

	AController* Controller = Character->GetController();
	if (AOutlierPlayerState* PS = Controller
		? Controller->GetPlayerState<AOutlierPlayerState>()
		: nullptr)
	{
		if (!PS->GetShooterCharacter())
		{
			PS->SetShooterCharacter(Character);
		}

		PS->SetSuitDisabledByPartnerBoundary(false);
	}

	Character->DetachFromControllerPendingDestroy();
	
	RespawnPairAtCheckpoint(Controller);
}

void AOutlierGameMode::StartMatchedPair(AController* FirstController, AController* SecondController, int32 PairId, int32 ArenaId, EOutlierPlayerRole FirstRole, EOutlierPlayerRole SecondRole)
{
	if (!FirstController || !SecondController)
	{
		return;
	}

	if (AFrontendPlayerController* FrontendShooterPC = Cast<AFrontendPlayerController>(FirstController))
	{
		FrontendShooterPC->ClientPrepareForMatch();
	}

	if (AFrontendPlayerController* FrontendPartnerPC = Cast<AFrontendPlayerController>(SecondController))
	{
		FrontendPartnerPC->ClientPrepareForMatch();
	}

	AOutlierPlayerState* FirstPS = FirstController->GetPlayerState<AOutlierPlayerState>();
	AOutlierPlayerState* SecondPS = SecondController->GetPlayerState<AOutlierPlayerState>();

	if (!FirstPS || !SecondPS)
	{
		return;
	}

	FirstPS->SetPairId(PairId);
	FirstPS->SetArenaId(ArenaId);
	FirstPS->SetPlayerRole(FirstRole);
	FirstPS->ClearPendingLobbyState();

	SecondPS->SetPairId(PairId);
	SecondPS->SetArenaId(ArenaId);
	SecondPS->SetPlayerRole(SecondRole);
	SecondPS->ClearPendingLobbyState();

	AController* ShooterController =
		FirstRole == EOutlierPlayerRole::Shooter
		? FirstController
		: SecondController;

	AController* PartnerController =
		SecondRole == EOutlierPlayerRole::Partner
		? SecondController
		: FirstController;

	AOutlierPlayerState* ShooterPS =
		ShooterController->GetPlayerState<AOutlierPlayerState>();

	AOutlierPlayerState* PartnerPS =
		PartnerController->GetPlayerState<AOutlierPlayerState>();

	FTransform ShooterSpawn;
	FTransform PartnerSpawn;

	if (!ResolveArenaSpawnTransforms(ArenaId, ShooterSpawn, PartnerSpawn))
	{
		AActor* FallbackStart = FindPlayerStart(ShooterController);
		ShooterSpawn = FallbackStart ? FallbackStart->GetActorTransform() : FTransform::Identity;
		PartnerSpawn = ShooterSpawn;
		PartnerSpawn.AddToTranslation(ShooterSpawn.GetRotation().GetRightVector() * 150.0f);
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[GameMode] ResolveArenaSpawnTransforms success ArenaId=%d ShooterSpawn=%s PartnerSpawn=%s"),
			ArenaId,
			*ShooterSpawn.ToHumanReadableString(),
			*PartnerSpawn.ToHumanReadableString());
	}

	AShooterCharacter* Shooter = GetWorld()->SpawnActor<AShooterCharacter>(
		ShooterClass,
		ShooterSpawn
	);

	APartnerCharacter* Partner = GetWorld()->SpawnActor<APartnerCharacter>(
		PartnerClass,
		PartnerSpawn
	);

	APlayerController* NewShooterPC = nullptr;
	APlayerController* NewPartnerPC = nullptr;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	if (ShooterControllerClass)
	{
		NewShooterPC = GetWorld()->SpawnActor<APlayerController>(ShooterControllerClass, ShooterSpawn, SpawnParams);
	}
	if (PartnerControllerClass)
	{
		NewPartnerPC = GetWorld()->SpawnActor<APlayerController>(PartnerControllerClass, PartnerSpawn, SpawnParams);
	}

	if (ShooterController && NewShooterPC)
	{
		SwapPlayerControllers(Cast<APlayerController>(ShooterController), NewShooterPC);
	}
	if (PartnerController && NewPartnerPC)
	{
		SwapPlayerControllers(Cast<APlayerController>(PartnerController), NewPartnerPC);
	}

	AOutlierPlayerState* NewShooterPS = NewShooterPC ? NewShooterPC->GetPlayerState<AOutlierPlayerState>() : nullptr;
	AOutlierPlayerState* NewPartnerPS = NewPartnerPC ? NewPartnerPC->GetPlayerState<AOutlierPlayerState>() : nullptr;

	if (NewShooterPS)
	{
		NewShooterPS->SetPairId(PairId);
		NewShooterPS->SetArenaId(ArenaId);
		NewShooterPS->SetPlayerRole(EOutlierPlayerRole::Shooter);
		NewShooterPS->ClearPendingLobbyState();
	}

	if (NewPartnerPS)
	{
		NewPartnerPS->SetPairId(PairId);
		NewPartnerPS->SetArenaId(ArenaId);
		NewPartnerPS->SetPlayerRole(EOutlierPlayerRole::Partner);
		NewPartnerPS->ClearPendingLobbyState();
	}


	RegisterSpawnedPair(NewShooterPS, NewPartnerPS, Shooter, Partner);

	if (AFirstPersonPlayerController* FPC = Cast<AFirstPersonPlayerController>(NewShooterPC))
	{
		if (!FPC->IsLocalController())
		{
			//UE_LOG(LogTemp, Warning, TEXT("[ArenaReady][0] ClientArenaLoad → ShooterPC=%s ArenaId=%d"), *GetNameSafe(FPC), ArenaId);
			FPC->ClientArenaLoad(ArenaId);
		}
	}
	if (AFirstPersonPlayerController* FPC = Cast<AFirstPersonPlayerController>(NewPartnerPC))
	{
		if (!FPC->IsLocalController())
		{
			//UE_LOG(LogTemp, Warning, TEXT("[ArenaReady][0] ClientArenaLoad → PartnerPC=%s ArenaId=%d"), *GetNameSafe(FPC), ArenaId);
			FPC->ClientArenaLoad(ArenaId);
		}
	}

	if (NewShooterPC && Shooter)
	{
		if (NewShooterPC->IsLocalController())
		{
			//UE_LOG(LogTemp, Warning, TEXT("[ArenaReady][0] ShooterPC is local → Possess immediately"));
			NewShooterPC->Possess(Shooter);
		}
		else
		{
			/*UE_LOG(LogTemp, Warning, TEXT("[ArenaReady][0] ShooterPC is remote → PendingPossessions PC=%s Pawn=%s"),
			*GetNameSafe(NewShooterPC), *GetNameSafe(Shooter));*/
			PendingPossessions.Add(NewShooterPC, Shooter);
		}
	}

	if (NewPartnerPC && Partner)
	{
		if (NewPartnerPC->IsLocalController())
		{
			//UE_LOG(LogTemp, Warning, TEXT("[ArenaReady][0] PartnerPC is local → Possess immediately"));
			NewPartnerPC->Possess(Partner);
		}
		else
		{
			/*UE_LOG(LogTemp, Warning, TEXT("[ArenaReady][0] PartnerPC is remote → PendingPossessions PC=%s Pawn=%s"),
				*GetNameSafe(NewPartnerPC), *GetNameSafe(Partner));*/
			PendingPossessions.Add(NewPartnerPC, Partner);
		}
	}

}



void AOutlierGameMode::OnClientArenaReady(APlayerController* PC)
{
	/*UE_LOG(LogTemp, Warning, TEXT("[ArenaReady][5] OnClientArenaReady PC=%s PendingCount=%d"),
		*GetNameSafe(PC), PendingPossessions.Num());*/

	TObjectPtr<APawn>* PendingPawn = PendingPossessions.Find(PC);
	if (!PendingPawn || !(*PendingPawn))
	{
		UE_LOG(LogTemp, Warning, TEXT("[ArenaReady][5] No pending pawn for PC=%s"), *GetNameSafe(PC));
		PendingPossessions.Remove(PC);
		return;
	}

	APawn* Pawn = PendingPawn->Get();
	PendingPossessions.Remove(PC);

	//UE_LOG(LogTemp, Warning, TEXT("[ArenaReady][5] Possessing Pawn=%s"), *GetNameSafe(Pawn));
	PC->Possess(Pawn);
}

void AOutlierGameMode::Logout(AController* Exiting)
{
	if (Exiting)
	{
		if (AShooterCharacter* ShooterCharacter = Cast<AShooterCharacter>(Exiting->GetPawn()))
		{
			ShooterCharacter->CleanupOwnedWeapons();
		}

		if (APartnerPlayerController* PartnerController = Cast<APartnerPlayerController>(Exiting))
		{
			if (AEnemyBase* EnemyPawn = Cast<AEnemyBase>(Exiting->GetPawn()))
			{
				// 캐시를 먼저 비워야 함 — 아래 Destroy()가 트리거하는 PawnPendingDestroy() 안전장치가
				// 이미 지워질 캐시된 Partner를 복원하려고 시도하는 걸 막기 위함.
				// AI에게 돌려주지 않고 바로 지우는 건, 일반 Shooter/Partner 로그아웃과 동일하게
				// 빙의 중이던 Pawn과 원래 Pawn이 둘 다 사라지는 게 자연스럽다는 판단.
				APartnerCharacter* CachedPartner = PartnerController->ExtractCachedPartnerCharacterForLogout();

				EnemyPawn->ClearPossessedPlayerState();
				EnemyPawn->Destroy();

				if (CachedPartner)
				{
					CachedPartner->Destroy();
				}
			}
		}
	}

	Super::Logout(Exiting);

}


void AOutlierGameMode::RespawnPairAtCheckpoint(AController* Controller)
{
	if (!Controller)
	{
		return;
	}

	AOutlierPlayerState* TriggeringPlayerState = Controller->GetPlayerState<AOutlierPlayerState>();
	if (!TriggeringPlayerState)
	{
		return;
	}

	AOutlierPlayerState* ShooterPlayerState = TriggeringPlayerState->IsShooterPlayer()
		? TriggeringPlayerState
		: FindPairPlayerState(TriggeringPlayerState->GetPairId(), EOutlierPlayerRole::Shooter);

	AOutlierPlayerState* PartnerPlayerState = TriggeringPlayerState->IsPartnerPlayer()
		? TriggeringPlayerState
		: FindPairPlayerState(TriggeringPlayerState->GetPairId(), EOutlierPlayerRole::Partner);

	if (!ShooterPlayerState)
	{
		ShooterPlayerState = TriggeringPlayerState;
	}

	const int32 ArenaId = ShooterPlayerState->GetArenaId();

	FTransform SpawnTransform;
	FTransform PartnerSpawnTransform;

	if (ResolveCheckpointTransform(GetControllerFromPlayerState(ShooterPlayerState), ArenaId, SpawnTransform))
	{
		PartnerSpawnTransform = SpawnTransform;
		PartnerSpawnTransform.AddToTranslation(
			SpawnTransform.GetRotation().GetRightVector() * 150.0f
		);
	}
	else if (ResolveArenaSpawnTransforms(ArenaId, SpawnTransform, PartnerSpawnTransform))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Respawn] Checkpoint missing. Fallback to arena start. ArenaId=%d Spawn=%s PartnerSpawn=%s"),
			ArenaId,
			*SpawnTransform.ToHumanReadableString(),
			*PartnerSpawnTransform.ToHumanReadableString());
	}
	else
	{
		AActor* PlayerStart = FindPlayerStart(Controller);
		SpawnTransform = PlayerStart
			? PlayerStart->GetActorTransform()
			: FTransform::Identity;

		PartnerSpawnTransform.AddToTranslation(
			SpawnTransform.GetRotation().GetRightVector() * 150.0f
		);

		UE_LOG(LogTemp, Warning,
			TEXT("[Respawn] Arena fallback failed. Fallback to FindPlayerStart. ArenaId=%d PlayerStart=%s Spawn=%s"),
			ArenaId,
			*GetNameSafe(PlayerStart),
			*SpawnTransform.ToHumanReadableString());
	}


	AShooterCharacter* OldShooter = ShooterPlayerState->GetShooterCharacter();
	APartnerCharacter* OldPartner = ShooterPlayerState->GetPartnerCharacter();

	if (!OldPartner && PartnerPlayerState)
	{
		OldPartner = PartnerPlayerState->GetPartnerCharacter();
	}

	if (APartnerPlayerController* PartnerController = Cast<APartnerPlayerController>(GetControllerFromPlayerState(PartnerPlayerState)))
	{
		if (Cast<AEnemyBase>(PartnerController->GetPawn()))
		{
			PartnerController->ReleaseEnemyPossession();
		}
	}

	ShooterPlayerState->SetShooterCharacter(nullptr);
	ShooterPlayerState->SetPartnerCharacter(nullptr);
	ShooterPlayerState->SetSuitDisabledByPartnerBoundary(false);

	if (PartnerPlayerState)
	{
		PartnerPlayerState->SetShooterCharacter(nullptr);
		PartnerPlayerState->SetPartnerCharacter(nullptr);
		PartnerPlayerState->SetSuitDisabledByPartnerBoundary(false);
	}

	if (UEnemyRoomSubsystem* EnemyRoomSubsystem = GetWorld()->GetSubsystem<UEnemyRoomSubsystem>())
	{
		EnemyRoomSubsystem->NotifyTargetActorRemoved(OldShooter);
		EnemyRoomSubsystem->NotifyTargetActorRemoved(OldPartner);
	}

	if (OldShooter)
	{
		OldShooter->CleanupOwnedWeapons();
		OldShooter->Destroy();
	}

	if (OldPartner)
	{
		OldPartner->Destroy();
	}

	AShooterCharacter* NewShooter = nullptr;
	if (ShooterClass)
	{
		NewShooter = GetWorld()->SpawnActor<AShooterCharacter>(ShooterClass, SpawnTransform);
	}
	else if (DefaultPawnClass)
	{
		NewShooter = Cast<AShooterCharacter>(
			GetWorld()->SpawnActor<APawn>(DefaultPawnClass, SpawnTransform)
		);
	}

	APartnerCharacter* NewPartner = PartnerClass
		? GetWorld()->SpawnActor<APartnerCharacter>(PartnerClass, PartnerSpawnTransform)
		: nullptr;

	if (AController* ShooterController = GetControllerFromPlayerState(ShooterPlayerState))
	{
		if (NewShooter)
		{
			ShooterController->Possess(NewShooter);
		}
	}

	if (AController* PartnerController = GetControllerFromPlayerState(PartnerPlayerState))
	{
		if (NewPartner)
		{
			PartnerController->Possess(NewPartner);
		}
	}

	RegisterSpawnedPair(ShooterPlayerState, PartnerPlayerState, NewShooter, NewPartner);
}

void AOutlierGameMode::DebugReloadArena(AController* Requester)
{
	if (!Requester)
	{
		return;
	}

	AOutlierPlayerState* TriggeringPS = Requester->GetPlayerState<AOutlierPlayerState>();
	if (!TriggeringPS)
	{
		return;
	}

	const int32 PairId = TriggeringPS->GetPairId();

	AOutlierPlayerState* ShooterPS = TriggeringPS->IsShooterPlayer()
		? TriggeringPS
		: FindPairPlayerState(PairId, EOutlierPlayerRole::Shooter);
	AOutlierPlayerState* PartnerPS = TriggeringPS->IsPartnerPlayer()
		? TriggeringPS
		: FindPairPlayerState(PairId, EOutlierPlayerRole::Partner);
	if (!ShooterPS)
	{
		ShooterPS = TriggeringPS;
	}

	const int32 ArenaId = ShooterPS->GetArenaId();
	UE_LOG(LogTemp, Warning, TEXT("[DebugReload] DebugReloadArena PairId=%d ArenaId=%d ShooterPS=%s PartnerPS=%s"),
		PairId, ArenaId, *GetNameSafe(ShooterPS), *GetNameSafe(PartnerPS));
	if (ArenaId == INDEX_NONE)
	{
		UE_LOG(LogTemp, Error, TEXT("[DebugReload] Bail: ArenaId==INDEX_NONE"));
		return;
	}

	//시작은 save 상관없이 start
	FTransform ShooterSpawn;
	FTransform PartnerSpawn;
	if (!ResolveArenaSpawnTransforms(ArenaId, ShooterSpawn, PartnerSpawn))
	{
		AActor* FallbackStart = FindPlayerStart(Requester);
		ShooterSpawn = FallbackStart ? FallbackStart->GetActorTransform() : FTransform::Identity;
		PartnerSpawn = ShooterSpawn;
		PartnerSpawn.AddToTranslation(ShooterSpawn.GetRotation().GetRightVector() * 150.0f);
	}

	// 2) 기존 폰 정리 (RespawnPairAtCheckpoint와 동일). 파트너가 적 빙의 중이면 먼저 해제.
	AShooterCharacter* OldShooter = ShooterPS->GetShooterCharacter();
	APartnerCharacter* OldPartner = ShooterPS->GetPartnerCharacter();
	if (!OldPartner && PartnerPS)
	{
		OldPartner = PartnerPS->GetPartnerCharacter();
	}

	if (APartnerPlayerController* PartnerPC = Cast<APartnerPlayerController>(GetControllerFromPlayerState(PartnerPS)))
	{
		if (Cast<AEnemyBase>(PartnerPC->GetPawn()))
		{
			PartnerPC->ReleaseEnemyPossession();
		}
	}

	ShooterPS->SetShooterCharacter(nullptr);
	ShooterPS->SetPartnerCharacter(nullptr);
	ShooterPS->SetSuitDisabledByPartnerBoundary(false);
	if (PartnerPS)
	{
		PartnerPS->SetShooterCharacter(nullptr);
		PartnerPS->SetPartnerCharacter(nullptr);
		PartnerPS->SetSuitDisabledByPartnerBoundary(false);
	}

	if (OldShooter)
	{
		OldShooter->CleanupOwnedWeapons();
		OldShooter->Destroy();
	}
	if (OldPartner)
	{
		OldPartner->Destroy();
	}

	AShooterCharacter* NewShooter = ShooterClass
		? GetWorld()->SpawnActor<AShooterCharacter>(ShooterClass, ShooterSpawn)
		: nullptr;
	APartnerCharacter* NewPartner = PartnerClass
		? GetWorld()->SpawnActor<APartnerCharacter>(PartnerClass, PartnerSpawn)
		: nullptr;

	RegisterSpawnedPair(ShooterPS, PartnerPS, NewShooter, NewPartner);

	// 4) possess 배선 — 지오메트리 준비 후로 지연.
	//    remote → 클라 재스트리밍 후 OnClientArenaReady에서 possess
	//    local  → 서버 자기 재스트리밍(OnArenaShown) 후 possess (아래 5)
	AController* ShooterController = GetControllerFromPlayerState(ShooterPS);
	AController* PartnerController = GetControllerFromPlayerState(PartnerPS);

	if (APlayerController* PC = Cast<APlayerController>(ShooterController))
	{
		if (PC->IsLocalController())
		{
			PendingLocalPossessions.Add(PC, NewShooter);
		}
		else
		{
			PendingPossessions.Add(PC, NewShooter);
			if (AFirstPersonPlayerController* FPC = Cast<AFirstPersonPlayerController>(PC))
			{
				FPC->ClientArenaReload(ArenaId);
			}
		}
	}

	if (APlayerController* PC = Cast<APlayerController>(PartnerController))
	{
		if (PC->IsLocalController())
		{
			PendingLocalPossessions.Add(PC, NewPartner);
		}
		else
		{
			PendingPossessions.Add(PC, NewPartner);
			if (AFirstPersonPlayerController* FPC = Cast<AFirstPersonPlayerController>(PC))
			{
				FPC->ClientArenaReload(ArenaId);
			}
		}
	}

	// 5) 로컬 possess 대기 바인딩 + 서버측 리로드 시작
	UOutlierArenaPoolSubsystem* ArenaPool = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaPoolSubsystem>()
		: nullptr;
	if (!ArenaPool)
	{
		return;
	}

	if (PendingLocalPossessions.Num() > 0)
	{
		ReloadingArenaId = ArenaId;
		if (!ArenaShownHandle.IsValid())
		{
			ArenaShownHandle = ArenaPool->OnArenaShown.AddUObject(this, &AOutlierGameMode::HandleServerArenaReloaded);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[DebugReload] Calling ReloadArena ArenaId=%d LocalPending=%d RemotePending=%d"),
		ArenaId, PendingLocalPossessions.Num(), PendingPossessions.Num());
	ArenaPool->ReloadArena(ArenaId);
}

void AOutlierGameMode::HandleServerArenaReloaded(int32 ReloadedArenaId)
{
	if (ReloadedArenaId != ReloadingArenaId)
	{
		return;
	}

	for (auto It = PendingLocalPossessions.CreateIterator(); It; ++It)
	{
		APlayerController* PC = It->Key.Get();
		APawn* Pawn = It->Value.Get();
		if (PC && Pawn)
		{
			PC->Possess(Pawn);
		}
	}
	PendingLocalPossessions.Empty();

	if (UOutlierArenaPoolSubsystem* ArenaPool = GetWorld()
		? GetWorld()->GetSubsystem<UOutlierArenaPoolSubsystem>()
		: nullptr)
	{
		ArenaPool->OnArenaShown.Remove(ArenaShownHandle);
	}
	ArenaShownHandle.Reset();
	ReloadingArenaId = INDEX_NONE;
}

bool AOutlierGameMode::ResolveCheckpointTransform(AController* Controller, int32 ArenaId, FTransform& OutTransform) const
{
	const AOutlierPlayerState* PS = Controller
		? Controller->GetPlayerState<AOutlierPlayerState>()
		: nullptr;

	if (!PS || !PS->GetCheckpointData().IsValid())
	{
		return false;
	}

	const FOutlierCheckpointData& Data = PS->GetCheckpointData();

	ULevel* ArenaLevel = nullptr;
	if (const UWorld* World = GetWorld())
	{
		if (const UOutlierArenaPoolSubsystem* ArenaPool = World->GetSubsystem<UOutlierArenaPoolSubsystem>())
		{
			ArenaLevel = ArenaPool->GetArenaLoadedLevel(ArenaId);
		}
	}

	if (!ArenaLevel)
	{
		return false;
	}

	TArray<AActor*> Checkpoints;
	UGameplayStatics::GetAllActorsOfClass(
		GetWorld(),
		AOutlierCheckpoint::StaticClass(),
		Checkpoints
	);

	for (AActor* Actor : Checkpoints)
	{
		const AOutlierCheckpoint* Checkpoint = Cast<AOutlierCheckpoint>(Actor);
		if (!Checkpoint)
		{
			continue;
		}

		if (Checkpoint->GetLevel() != ArenaLevel)
		{
			continue;
		}

		if (Checkpoint->GetCheckpointId() == Data.CheckpointId)
		{
			OutTransform = Checkpoint->GetSpawnTransform();
			return true;
		}
	}

	return false;
}

FString AOutlierGameMode::GetPlayerSaveId(AController* Controller) const
{
	if (!Controller)
	{
		return TEXT("InvalidPlayer");
	}

	if (APlayerState* PS = Controller->PlayerState)
	{
		const FString PlayerName = PS->GetPlayerName();
		if (!PlayerName.IsEmpty())
		{
			return PlayerName;
		}

		return FString::Printf(TEXT("Player_%d"), PS->GetPlayerId());
	}

	return Controller->GetName();
}

AOutlierPlayerState* AOutlierGameMode::FindPairPlayerState(int32 PairId, EOutlierPlayerRole PlayerRole) const
{
	if (PairId == INDEX_NONE || !GameState)
	{
		return nullptr;
	}

	for (APlayerState* RawPlayerState : GameState->PlayerArray)
	{
		AOutlierPlayerState* PS = Cast<AOutlierPlayerState>(RawPlayerState);
		if (PS && PS->GetPairId() == PairId && PS->GetPlayerRole() == PlayerRole)
		{
			return PS;
		}
	}

	return nullptr;
}

AController* AOutlierGameMode::GetControllerFromPlayerState(AOutlierPlayerState* PlayerState) const
{
	return PlayerState ? Cast<AController>(PlayerState->GetOwner()) : nullptr;
}

void AOutlierGameMode::RegisterSpawnedPair(
	AOutlierPlayerState* ShooterPlayerState,
	AOutlierPlayerState* PartnerPlayerState,
	AShooterCharacter* Shooter,
	APartnerCharacter* Partner)
{
	if (ShooterPlayerState)
	{
		ShooterPlayerState->SetShooterCharacter(Shooter);
		ShooterPlayerState->SetPartnerCharacter(Partner);
		ShooterPlayerState->SetSuitDisabledByPartnerBoundary(false);
	}

	if (PartnerPlayerState)
	{
		PartnerPlayerState->SetShooterCharacter(Shooter);
		PartnerPlayerState->SetPartnerCharacter(Partner);
		PartnerPlayerState->SetSuitDisabledByPartnerBoundary(false);
	}
}

bool AOutlierGameMode::ResolveArenaSpawnTransforms(int32 ArenaId, FTransform& OutShooterSpawn, FTransform& OutPartnerSpawn) const
{
	const UWorld* World = GetWorld();
	ULevel* ArenaLevel = nullptr;
	if (World)
	{
		if (const UOutlierArenaPoolSubsystem* ArenaPool = World->GetSubsystem<UOutlierArenaPoolSubsystem>())
		{
			ArenaLevel = ArenaPool->GetArenaLoadedLevel(ArenaId);
		}
	}

	if (!ArenaLevel)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[GameMode] ResolveArenaSpawnTransforms FAIL no ArenaLevel ArenaId=%d"),
			ArenaId);
		return false;
	}

	TArray<AActor*> PlayerStartActors;
	UGameplayStatics::GetAllActorsOfClass(
		World,
		APlayerStart::StaticClass(),
		PlayerStartActors
	);

	APlayerStart* ShooterStart = nullptr;
	APlayerStart* PartnerStart = nullptr;
	TArray<APlayerStart*> ArenaStarts;

	for (AActor* Actor : PlayerStartActors)
	{
		APlayerStart* PlayerStart = Cast<APlayerStart>(Actor);
		if (!PlayerStart)
		{
			continue;
		}

		const bool bSameLevel = PlayerStart->GetLevel() == ArenaLevel;
	
		if (!bSameLevel)
		{
			continue;
		}

		ArenaStarts.Add(PlayerStart);

		const FName StartTag = PlayerStart->PlayerStartTag;
		if (!ShooterStart && (StartTag == TEXT("Shooter") || StartTag == TEXT("Player1") || StartTag == TEXT("1P")))
		{
			ShooterStart = PlayerStart;
		}
		else if (!PartnerStart && (StartTag == TEXT("Partner") || StartTag == TEXT("Player2") || StartTag == TEXT("2P")))
		{
			PartnerStart = PlayerStart;
		}
	}

	if (!ShooterStart && ArenaStarts.Num() > 0)
	{
		ShooterStart = ArenaStarts[0];
	}

	if (!PartnerStart && ArenaStarts.Num() > 1)
	{
		PartnerStart = ArenaStarts[1];
	}

	if (!ShooterStart)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[GameMode] ResolveArenaSpawnTransforms FAIL no ShooterStart ArenaId=%d ArenaStartCount=%d"),
			ArenaId,
			ArenaStarts.Num());
		return false;
	}

	OutShooterSpawn = ShooterStart->GetActorTransform();
	if (PartnerStart)
	{
		OutPartnerSpawn = PartnerStart->GetActorTransform();
	}
	else
	{
		OutPartnerSpawn = OutShooterSpawn;
		OutPartnerSpawn.AddToTranslation(OutShooterSpawn.GetRotation().GetRightVector() * 150.0f);
	}

	return true;

}
