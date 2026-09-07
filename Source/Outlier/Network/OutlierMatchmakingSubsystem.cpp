// Fill out your copyright notice in the Description page of Project Settings.


#include "Network/OutlierMatchmakingSubsystem.h"
#include "Network/OutlierArenaPoolSubsystem.h"
#include "Network/OutlierArenaProcessSubsystem.h"
#include "OutlierGameMode.h"
#include "OutlierPlayerState.h"
#include "OutlierArenaSettings.h"
#include "FrontendPlayerController.h"
#include "GameFramework/Controller.h"
#include "OutlierLobbyIdentitySubsystem.h"

namespace
{
constexpr double PartyJoinRateLimitSeconds = 0.5;
constexpr int32 MaxPartyCodeGenerationAttempts = 32;
}

void UOutlierMatchmakingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UOutlierArenaProcessSubsystem>();
	if (UOutlierArenaProcessSubsystem* ProcessSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierArenaProcessSubsystem>()
		: nullptr)
	{
		ProcessSubsystem->OnSlotReady.AddUObject(
			this,
			&UOutlierMatchmakingSubsystem::HandleArenaSlotReady);
	}
}

void UOutlierMatchmakingSubsystem::Deinitialize()
{
	if (UOutlierArenaProcessSubsystem* ProcessSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierArenaProcessSubsystem>()
		: nullptr)
	{
		ProcessSubsystem->OnSlotReady.RemoveAll(this);
	}
	Super::Deinitialize();
}

void UOutlierMatchmakingSubsystem::SetMatchmakingMode(EOutlierMatchmakingMode NewMode)
{
	MatchmakingMode = NewMode;
}

void UOutlierMatchmakingSubsystem::EnqueueForPairThenRolePick(AController* Controller)
{
	if (!Controller)
	{
		UE_LOG(LogTemp, Error, TEXT("[Matchmaking] EnqueueForPairThenRolePick: Controller is NULL"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[Matchmaking] EnqueueForPairThenRolePick: %s added. WaitingPlayers before: %d"),
		*Controller->GetName(), WaitingPlayers.Num());

	Cancel(Controller);

	if (AOutlierPlayerState* PS = Controller->GetPlayerState<AOutlierPlayerState>())
	{
		PS->ClearPendingLobbyState();
	}

	FOutlierMatchRequest Request;
	Request.Controller = Controller;
	Request.RequestTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	WaitingPlayers.Add(Request);

	UE_LOG(LogTemp, Warning, TEXT("[Matchmaking] WaitingPlayers after enqueue: %d"), WaitingPlayers.Num());

	TryCreateMatch();
}

void UOutlierMatchmakingSubsystem::EnqueueByRole(AController* Controller, EOutlierPlayerRole DesiredRole)
{
	if (!Controller || DesiredRole == EOutlierPlayerRole::None)
	{
		return;
	}

	Cancel(Controller);

	FOutlierMatchRequest Request;
	Request.Controller = Controller;
	Request.DesiredRole = DesiredRole;
	Request.RequestTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	if (DesiredRole == EOutlierPlayerRole::Shooter)
	{
		WaitingShooters.Add(Request);
	}
	else if (DesiredRole == EOutlierPlayerRole::Partner)
	{
		WaitingPartners.Add(Request);
	}

	TryCreateMatch();
}

void UOutlierMatchmakingSubsystem::CreateParty(AController* Controller)
{
	if (!Controller)
	{
		return;
	}

	Cancel(Controller);

	UOutlierLobbyIdentitySubsystem* Identity = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierLobbyIdentitySubsystem>()
		: nullptr;
	FGuid LeaderPlayerId;
	if (!Identity || !Identity->TryGetPlayerId(Controller, LeaderPlayerId))
	{
		NotifyPartyResult(Controller, EOutlierPartyRequestResult::Failed);
		return;
	}

	const FString PartyCode = GenerateUniquePartyCode();
	if (PartyCode.IsEmpty())
	{
		NotifyPartyResult(Controller, EOutlierPartyRequestResult::Failed);
		return;
	}

	FOutlierPendingParty Party;
	Party.PartyCode = PartyCode;
	Party.LeaderPlayerId = LeaderPlayerId;
	Party.LeaderController = Controller;
	PendingPartiesByCode.Add(PartyCode, Party);

	NotifyPartyResult(Controller, EOutlierPartyRequestResult::Created, PartyCode);
}

void UOutlierMatchmakingSubsystem::JoinParty(AController* Controller, const FString& PartyCode)
{
	if (!Controller)
	{
		return;
	}

	const double CurrentTime = GetWorld()
		? GetWorld()->GetTimeSeconds()
		: FPlatformTime::Seconds();
	const TWeakObjectPtr<AController> ControllerKey(Controller);
	if (const double* LastAttemptTime = LastPartyJoinAttemptTimes.Find(ControllerKey))
	{
		if (CurrentTime - *LastAttemptTime < PartyJoinRateLimitSeconds)
		{
			NotifyPartyResult(Controller, EOutlierPartyRequestResult::RateLimited);
			return;
		}
	}
	LastPartyJoinAttemptTimes.Add(ControllerKey, CurrentTime);

	if (PartyCode.Len() > 32)
	{
		NotifyPartyResult(Controller, EOutlierPartyRequestResult::InvalidCode);
		return;
	}

	const FString NormalizedCode = OutlierPartyCode::Normalize(PartyCode);
	if (!OutlierPartyCode::IsValid(NormalizedCode))
	{
		NotifyPartyResult(Controller, EOutlierPartyRequestResult::InvalidCode);
		return;
	}

	const FOutlierPendingParty* FoundParty = PendingPartiesByCode.Find(NormalizedCode);
	if (!FoundParty || !FoundParty->IsValid())
	{
		PendingPartiesByCode.Remove(NormalizedCode);
		NotifyPartyResult(Controller, EOutlierPartyRequestResult::PartyNotFound);
		return;
	}

	AController* LeaderController = FoundParty->LeaderController.Get();
	if (!LeaderController || LeaderController == Controller)
	{
		NotifyPartyResult(Controller, EOutlierPartyRequestResult::Failed);
		return;
	}

	UOutlierLobbyIdentitySubsystem* Identity = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierLobbyIdentitySubsystem>()
		: nullptr;
	FGuid JoiningPlayerId;
	if (!Identity
		|| !Identity->TryGetPlayerId(Controller, JoiningPlayerId)
		|| JoiningPlayerId == FoundParty->LeaderPlayerId)
	{
		NotifyPartyResult(Controller, EOutlierPartyRequestResult::Failed);
		return;
	}

	// 참가자가 다른 큐나 파티에 속해 있다면 먼저 그 상태를 정리한다.
	Cancel(Controller);

	FoundParty = PendingPartiesByCode.Find(NormalizedCode);
	if (!FoundParty || FoundParty->LeaderController != LeaderController)
	{
		NotifyPartyResult(Controller, EOutlierPartyRequestResult::PartyNotFound);
		return;
	}

	PendingPartiesByCode.Remove(NormalizedCode);

	FOutlierMatchRequest LeaderRequest;
	LeaderRequest.Controller = LeaderController;
	LeaderRequest.PendingLobbySlotIndex = 0;
	LeaderRequest.RequestTime = CurrentTime;

	FOutlierMatchRequest MemberRequest;
	MemberRequest.Controller = Controller;
	MemberRequest.PendingLobbySlotIndex = 1;
	MemberRequest.RequestTime = CurrentTime;

	if (!CreatePendingRolePickMatch(
		LeaderRequest,
		MemberRequest,
		EOutlierPendingMatchSource::PremadeParty))
	{
		NotifyPartyResult(LeaderController, EOutlierPartyRequestResult::Failed);
		NotifyPartyResult(Controller, EOutlierPartyRequestResult::Failed);
		return;
	}

	NotifyPartyResult(LeaderController, EOutlierPartyRequestResult::MemberJoined, NormalizedCode);
	NotifyPartyResult(Controller, EOutlierPartyRequestResult::Joined, NormalizedCode);
}

void UOutlierMatchmakingSubsystem::LeaveParty(AController* Controller)
{
	Cancel(Controller);
}

bool UOutlierMatchmakingSubsystem::SelectRoleInPendingMatch(AController* Controller, EOutlierPlayerRole DesiredRole)
{
	if (!Controller ||
		(DesiredRole != EOutlierPlayerRole::None
			&& DesiredRole != EOutlierPlayerRole::Shooter
			&& DesiredRole != EOutlierPlayerRole::Partner))
	{
		return false;
	}

	for (int32 MatchIndex = 0; MatchIndex < PendingRolePickMatches.Num(); ++MatchIndex)
	{
		FOutlierPendingRolePickMatch& Match = PendingRolePickMatches[MatchIndex];
		if (!Match.Contains(Controller))
		{
			continue;
		}

		const bool bRoleTakenByOther =
			(DesiredRole == EOutlierPlayerRole::Shooter &&
				Match.ShooterController &&
				Match.ShooterController != Controller) ||
			(DesiredRole == EOutlierPlayerRole::Partner &&
				Match.PartnerController &&
				Match.PartnerController != Controller);

		if (bRoleTakenByOther)
		{
			return false;
		}

		if (Match.ShooterController == Controller)
		{
			Match.ShooterController = nullptr;
		}

		if (Match.PartnerController == Controller)
		{
			Match.PartnerController = nullptr;
		}

		if (DesiredRole == EOutlierPlayerRole::Shooter)
		{
			Match.ShooterController = Controller;
		}
		else if (DesiredRole == EOutlierPlayerRole::Partner)
		{
			Match.PartnerController = Controller;
		}

		if (AOutlierPlayerState* PS = Controller->GetPlayerState<AOutlierPlayerState>())
		{
			PS->SetPendingLobbyMatchId(Match.PendingMatchId);
			PS->SetPendingLobbyRole(DesiredRole);
		}

		if (Match.IsReady())
		{
			TryCreatePendingRolePickMatch(MatchIndex);
		}

		return true;
	}

	return false;
}

bool UOutlierMatchmakingSubsystem::TryStartPendingMatch(AController* Controller)
{
	if (!Controller)
	{
		return false;
	}

	for (int32 MatchIndex = 0; MatchIndex < PendingRolePickMatches.Num(); ++MatchIndex)
	{
		FOutlierPendingRolePickMatch& Match = PendingRolePickMatches[MatchIndex];
		if (!Match.Contains(Controller))
		{
			continue;
		}

		if (!Match.IsReady())
		{
			return false;
		}

		TryCreatePendingRolePickMatch(MatchIndex);

		return true;
	}

	return false;
}

void UOutlierMatchmakingSubsystem::Cancel(AController* Controller)
{
	if (!Controller)
	{
		return;
	}

	if (AOutlierPlayerState* PS = Controller->GetPlayerState<AOutlierPlayerState>())
	{
		PS->ClearPendingLobbyState();
	}

	auto RemoveController = [Controller](TArray<FOutlierMatchRequest>& Queue)
	{
		Queue.RemoveAll([Controller](const FOutlierMatchRequest& Request)
		{
			return Request.Controller == Controller;
		});
	};

	RemoveController(WaitingPlayers);
	RemoveController(WaitingShooters);
	RemoveController(WaitingPartners);

	for (auto PartyIt = PendingPartiesByCode.CreateIterator(); PartyIt; ++PartyIt)
	{
		if (PartyIt.Value().LeaderController != Controller)
		{
			continue;
		}

		const FString RemovedPartyCode = PartyIt.Key();
		PartyIt.RemoveCurrent();
		NotifyPartyResult(Controller, EOutlierPartyRequestResult::Left, RemovedPartyCode);
	}

	for (int32 MatchIndex = PendingRolePickMatches.Num() - 1; MatchIndex >= 0; --MatchIndex)
	{
		FOutlierPendingRolePickMatch& Match = PendingRolePickMatches[MatchIndex];
		if (!Match.Contains(Controller))
		{
			continue;
		}

		AController* OtherController = Match.FirstController == Controller
			? Match.SecondController.Get()
			: Match.FirstController.Get();
		const bool bRequeueRemainingPlayer = Match.ShouldRequeueRemainingPlayer();

		PendingRolePickMatches.RemoveAt(MatchIndex);

		if (OtherController)
		{
			int32 OtherSlotIndex = INDEX_NONE;
			if (AOutlierPlayerState* OtherPS = OtherController->GetPlayerState<AOutlierPlayerState>())
			{
				OtherSlotIndex = OtherPS->GetPendingLobbySlotIndex();
				OtherPS->ClearPendingLobbyState();
			}

			if (bRequeueRemainingPlayer)
			{
				FOutlierMatchRequest Request;
				Request.Controller = OtherController;
				Request.PendingLobbySlotIndex = OtherSlotIndex;
				Request.RequestTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
				WaitingPlayers.Add(Request);
			}
			else
			{
				NotifyPartyResult(OtherController, EOutlierPartyRequestResult::Disbanded);
			}
		}

		if (!bRequeueRemainingPlayer)
		{
			NotifyPartyResult(Controller, EOutlierPartyRequestResult::Left);
		}
	}

	LastPartyJoinAttemptTimes.Remove(TWeakObjectPtr<AController>(Controller));

	TryCreateMatch();
}

void UOutlierMatchmakingSubsystem::ReleaseMatch(int32 PairId)
{
	if (PairId == INDEX_NONE)
	{
		return;
	}

	if (const int32* ArenaId = ActivePairArenaIds.Find(PairId))
	{
		if (UWorld* World = GetWorld())
		{
			if (UOutlierArenaPoolSubsystem* ArenaPool = World->GetSubsystem<UOutlierArenaPoolSubsystem>())
			{
				ArenaPool->ReleaseArena(*ArenaId);
			}
		}

		ActivePairArenaIds.Remove(PairId);
	}

	if (const FGuid* MatchId = ActivePairMatchIds.Find(PairId))
	{
		ActiveMatchAssignments.Remove(*MatchId);
		ActivePairMatchIds.Remove(PairId);
	}
}

bool UOutlierMatchmakingSubsystem::BuildMatchAssignment(AController* FirstController, AController* SecondController, EOutlierPlayerRole FirstRole, EOutlierPlayerRole SecondRole, FOutlierMatchAssignment& OutAssignment) const
{
	OutAssignment = FOutlierMatchAssignment();

	const bool bRolesAreComplementary =
		(FirstRole == EOutlierPlayerRole::Shooter &&
			SecondRole == EOutlierPlayerRole::Partner) ||
		(FirstRole == EOutlierPlayerRole::Partner &&
			SecondRole == EOutlierPlayerRole::Shooter);

	if (!FirstController
		|| !SecondController
		|| FirstController == SecondController
		|| !bRolesAreComplementary)
	{
		return false;
	}

	UOutlierLobbyIdentitySubsystem* Identity =
		GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierLobbyIdentitySubsystem>()
		: nullptr;

	if (!Identity)
	{
		return false;
	}

	FGuid FirstPlayerId;
	FGuid SecondPlayerId;

	if (!Identity->TryGetPlayerId(FirstController, FirstPlayerId)
		|| !Identity->TryGetPlayerId(SecondController, SecondPlayerId))
	{
		return false;
	}

	const bool bFirstIsShooter =
		FirstRole == EOutlierPlayerRole::Shooter;

	OutAssignment.MatchId = FGuid::NewGuid();
	OutAssignment.Shooter.PlayerId = bFirstIsShooter ? FirstPlayerId : SecondPlayerId;
	OutAssignment.Shooter.Role	   = EOutlierPlayerRole::Shooter;
	OutAssignment.Partner.PlayerId = bFirstIsShooter ? SecondPlayerId : FirstPlayerId;
	OutAssignment.Partner.Role     = EOutlierPlayerRole::Partner;

	return OutAssignment.IsValid();
}

bool UOutlierMatchmakingSubsystem::TryGetMatchAssignment(const FGuid& MatchId, FOutlierMatchAssignment& OutAssignment) const
{
	OutAssignment = FOutlierMatchAssignment();

	if (const FOutlierMatchAssignment* Found = ActiveMatchAssignments.Find(MatchId))
	{
		OutAssignment = *Found;
		return true;
	}

	return false;
}

void UOutlierMatchmakingSubsystem::TryCreateMatch()
{
	switch (MatchmakingMode)
	{
	case EOutlierMatchmakingMode::PairThenRolePick:
		TryCreatePairThenRolePickMatch();
		break;

	case EOutlierMatchmakingMode::RoleQueue:
		TryCreateRoleQueueMatch();
		break;

	default:
		break;
	}
}

void UOutlierMatchmakingSubsystem::TryCreatePairThenRolePickMatch()
{
	UE_LOG(LogTemp, Verbose, TEXT("[Matchmaking] TryCreatePairThenRolePickMatch: WaitingPlayers=%d"), WaitingPlayers.Num());

	while (WaitingPlayers.Num() >= 2)
	{
		FOutlierMatchRequest First = WaitingPlayers[0];
		FOutlierMatchRequest Second = WaitingPlayers[1];

		WaitingPlayers.RemoveAt(0, 2);

		if (!First.Controller || !Second.Controller)
		{
			UE_LOG(LogTemp, Error, TEXT("[Matchmaking] PairThenRolePick: one of the controllers is NULL, skipping"));
			continue;
		}

		CreatePendingRolePickMatch(
			First,
			Second,
			EOutlierPendingMatchSource::SoloQueue);
	}
}

bool UOutlierMatchmakingSubsystem::CreatePendingRolePickMatch(
	const FOutlierMatchRequest& First,
	const FOutlierMatchRequest& Second,
	EOutlierPendingMatchSource Source)
{
	if (!First.Controller || !Second.Controller || First.Controller == Second.Controller)
	{
		return false;
	}

	AOutlierPlayerState* FirstPS = First.Controller->GetPlayerState<AOutlierPlayerState>();
	AOutlierPlayerState* SecondPS = Second.Controller->GetPlayerState<AOutlierPlayerState>();
	if (!FirstPS || !SecondPS)
	{
		return false;
	}

	const auto IsValidLobbySlot = [](int32 SlotIndex)
	{
		return SlotIndex == 0 || SlotIndex == 1;
	};

	int32 FirstSlotIndex = 0;
	int32 SecondSlotIndex = 1;
	const bool bFirstHasPreferredSlot = IsValidLobbySlot(First.PendingLobbySlotIndex);
	const bool bSecondHasPreferredSlot = IsValidLobbySlot(Second.PendingLobbySlotIndex);
	if (bFirstHasPreferredSlot && bSecondHasPreferredSlot)
	{
		FirstSlotIndex = First.PendingLobbySlotIndex;
		SecondSlotIndex = First.PendingLobbySlotIndex != Second.PendingLobbySlotIndex
			? Second.PendingLobbySlotIndex
			: 1 - FirstSlotIndex;
	}
	else if (bFirstHasPreferredSlot)
	{
		FirstSlotIndex = First.PendingLobbySlotIndex;
		SecondSlotIndex = 1 - FirstSlotIndex;
	}
	else if (bSecondHasPreferredSlot)
	{
		SecondSlotIndex = Second.PendingLobbySlotIndex;
		FirstSlotIndex = 1 - SecondSlotIndex;
	}

	FOutlierPendingRolePickMatch PendingMatch;
	PendingMatch.PendingMatchId = NextPendingMatchId++;
	PendingMatch.FirstController = First.Controller;
	PendingMatch.SecondController = Second.Controller;
	PendingMatch.CreatedTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	PendingMatch.Source = Source;

	FirstPS->SetPendingLobbyMatchId(PendingMatch.PendingMatchId);
	FirstPS->SetPendingLobbySlotIndex(FirstSlotIndex);
	FirstPS->SetPendingLobbyRole(EOutlierPlayerRole::None);

	SecondPS->SetPendingLobbyMatchId(PendingMatch.PendingMatchId);
	SecondPS->SetPendingLobbySlotIndex(SecondSlotIndex);
	SecondPS->SetPendingLobbyRole(EOutlierPlayerRole::None);

	PendingRolePickMatches.Add(PendingMatch);

	UE_LOG(LogTemp, Display,
		TEXT("[Matchmaking] Pending role pick created Id=%d Source=%s First=%s Second=%s"),
		PendingMatch.PendingMatchId,
		Source == EOutlierPendingMatchSource::PremadeParty
			? TEXT("Party")
			: TEXT("Solo"),
		*First.Controller->GetName(),
		*Second.Controller->GetName());
	return true;
}

FString UOutlierMatchmakingSubsystem::GenerateUniquePartyCode() const
{
	for (int32 Attempt = 0; Attempt < MaxPartyCodeGenerationAttempts; ++Attempt)
	{
		const FString PartyCode = OutlierPartyCode::Generate();
		if (!PendingPartiesByCode.Contains(PartyCode))
		{
			return PartyCode;
		}
	}

	return FString();
}

void UOutlierMatchmakingSubsystem::NotifyPartyResult(
	AController* Controller,
	EOutlierPartyRequestResult Result,
	const FString& PartyCode) const
{
	if (AFrontendPlayerController* FrontendController =
		Cast<AFrontendPlayerController>(Controller))
	{
		FrontendController->ClientNotifyPartyResult(Result, PartyCode);
	}
}

void UOutlierMatchmakingSubsystem::TryCreateRoleQueueMatch()
{
	while (WaitingShooters.Num() > 0 && WaitingPartners.Num() > 0)
	{
		const FOutlierMatchRequest Shooter = WaitingShooters[0];
		const FOutlierMatchRequest Partner = WaitingPartners[0];
		WaitingShooters.RemoveAt(0);
		WaitingPartners.RemoveAt(0);

		// Listen Server에서는 매치 생성 중 Controller 교체가 Logout과 Cancel을 동기적으로 호출할 수 있다.
		// 큐 항목을 먼저 소비해야 같은 항목을 재진입 경로에서 다시 제거하지 않는다.
		if (!CreateMatch(
			Shooter.Controller,
			Partner.Controller,
			EOutlierPlayerRole::Shooter,
			EOutlierPlayerRole::Partner))
		{
			WaitingShooters.Insert(Shooter, 0);
			WaitingPartners.Insert(Partner, 0);
			break;
		}
	}
}

bool UOutlierMatchmakingSubsystem::CreateMatch(
	AController* FirstController,
	AController* SecondController,
	EOutlierPlayerRole FirstRole,
	EOutlierPlayerRole SecondRole)
{
	if (!FirstController || !SecondController)
	{
		return false;
	}

	FOutlierMatchAssignment Assignment;

	if (!BuildMatchAssignment(
		FirstController,
		SecondController,
		FirstRole,
		SecondRole,
		Assignment))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[Matchmaking] Failed to build MatchAssignment"));
		return false;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[Matchmaking] Assignment Match=%s Shooter=%s Partner=%s"),
		*Assignment.MatchId.ToString(),
		*Assignment.Shooter.PlayerId.ToString(),
		*Assignment.Partner.PlayerId.ToString());

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const UOutlierArenaSettings* ArenaSettings = GetDefault<UOutlierArenaSettings>();
	if (ArenaSettings && ArenaSettings->ShouldUseExternalArenaHandoff(World->GetNetMode()))
	{
		UOutlierArenaProcessSubsystem* ProcessSubsystem = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UOutlierArenaProcessSubsystem>()
			: nullptr;
		FString ArenaAddress = ArenaSettings->StaticArenaAddress;
		int32 ArenaSlotId = INDEX_NONE;
		const bool bUseProcessManager = ArenaSettings->bUseProcessManager;
		if (bUseProcessManager
			&& (!ProcessSubsystem
				|| !ProcessSubsystem->IsLobbyManagerActive()
				|| !ProcessSubsystem->TryAllocateReadySlot(
					Assignment.MatchId,
					ArenaAddress,
					ArenaSlotId)))
		{
			UE_LOG(LogTemp, Verbose,
				TEXT("[Matchmaking] Waiting for a Ready Arena Slot"));
			return false;
		}

		AController* ShooterController = FirstRole == EOutlierPlayerRole::Shooter
			? FirstController
			: SecondController;
		AController* PartnerController = FirstRole == EOutlierPlayerRole::Partner
			? FirstController
			: SecondController;

		AFrontendPlayerController* FrontendShooter =
			Cast<AFrontendPlayerController>(ShooterController);
		AFrontendPlayerController* FrontendPartner =
			Cast<AFrontendPlayerController>(PartnerController);

		FOutlierArenaHandoffRequest ShooterRequest;
		ShooterRequest.MatchId = Assignment.MatchId;
		ShooterRequest.PlayerId = Assignment.Shooter.PlayerId;
		ShooterRequest.Role = EOutlierPlayerRole::Shooter;

		FOutlierArenaHandoffRequest PartnerRequest;
		PartnerRequest.MatchId = Assignment.MatchId;
		PartnerRequest.PlayerId = Assignment.Partner.PlayerId;
		PartnerRequest.Role = EOutlierPlayerRole::Partner;

		const FString ShooterUrl = OutlierArenaHandoff::BuildTravelUrl(
			ArenaAddress,
			ShooterRequest);
		const FString PartnerUrl = OutlierArenaHandoff::BuildTravelUrl(
			ArenaAddress,
			PartnerRequest);

		if (!FrontendShooter || !FrontendPartner
			|| ShooterUrl.IsEmpty() || PartnerUrl.IsEmpty())
		{
			if (bUseProcessManager && ProcessSubsystem)
			{
				ProcessSubsystem->ReleaseAllocation(Assignment.MatchId);
			}
			UE_LOG(LogTemp, Error,
				TEXT("[Matchmaking] Static arena handoff setup is invalid"));
			return false;
		}

		const int32 PairId = NextPairId++;
		ActiveMatchAssignments.Add(Assignment.MatchId, Assignment);
		ActivePairMatchIds.Add(PairId, Assignment.MatchId);

		UE_LOG(LogTemp, Display,
			TEXT("[Matchmaking] Handoff Match=%s Slot=%d Address=%s"),
			*Assignment.MatchId.ToString(),
			ArenaSlotId,
			*ArenaAddress);

		FrontendShooter->ClientHandoffToArena(ShooterUrl);
		FrontendPartner->ClientHandoffToArena(PartnerUrl);
		ReleaseMatch(PairId);
		return true;
	}

	UOutlierArenaPoolSubsystem* ArenaPool =
		World->GetSubsystem<UOutlierArenaPoolSubsystem>();

	if (!ArenaPool)
	{
		return false;
	}

	FOutlierArenaInstance* Arena = ArenaPool->AcquireArena();
	if (!Arena)
	{
		return false;
	}

	const int32 PairId = NextPairId++;

	AOutlierGameMode* GameMode = World->GetAuthGameMode<AOutlierGameMode>();
	if (!GameMode)
	{
		ArenaPool->ReleaseArena(Arena->ArenaId);
		return false;
	}

	Arena->PairId = PairId;
	ActivePairArenaIds.Add(PairId, Arena->ArenaId);
	ActiveMatchAssignments.Add(Assignment.MatchId, Assignment);
	ActivePairMatchIds.Add(PairId, Assignment.MatchId);

	GameMode->StartMatchedPair(
		FirstController,
		SecondController,
		PairId,
		Arena->ArenaId,
		FirstRole,
		SecondRole
	);
	return true;
}

void UOutlierMatchmakingSubsystem::HandleArenaSlotReady()
{
	TryDispatchReadyPendingMatches();
	TryCreateRoleQueueMatch();
}

void UOutlierMatchmakingSubsystem::TryDispatchReadyPendingMatches()
{
	for (int32 MatchIndex = 0; MatchIndex < PendingRolePickMatches.Num();)
	{
		FOutlierPendingRolePickMatch& Match = PendingRolePickMatches[MatchIndex];
		if (!Match.IsReady())
		{
			++MatchIndex;
			continue;
		}

		if (!TryCreatePendingRolePickMatch(MatchIndex))
		{
			break;
		}
	}
}

bool UOutlierMatchmakingSubsystem::TryCreatePendingRolePickMatch(int32 MatchIndex)
{
	if (!PendingRolePickMatches.IsValidIndex(MatchIndex)
		|| !PendingRolePickMatches[MatchIndex].IsReady())
	{
		return false;
	}

	FOutlierPendingRolePickMatch PendingMatch = PendingRolePickMatches[MatchIndex];
	PendingRolePickMatches.RemoveAt(MatchIndex);

	// StartMatchedPair가 기존 Frontend Controller를 교체하면 Logout -> Cancel이 재진입한다.
	// 매치를 먼저 소비하고, Arena가 아직 준비되지 않은 경우에만 원래 위치로 복구한다.
	if (!CreateMatch(
		PendingMatch.ShooterController.Get(),
		PendingMatch.PartnerController.Get(),
		EOutlierPlayerRole::Shooter,
		EOutlierPlayerRole::Partner))
	{
		PendingRolePickMatches.Insert(
			MoveTemp(PendingMatch),
			FMath::Min(MatchIndex, PendingRolePickMatches.Num()));
		return false;
	}

	return true;
}
