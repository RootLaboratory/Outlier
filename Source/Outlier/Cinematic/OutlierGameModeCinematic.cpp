#include "OutlierGameMode.h"

#include "Cinematic/OutlierCinematicTypes.h"
#include "Engine/World.h"
#include "Event/OutlierEventSubsystem.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/WorldSettings.h"
#include "Network/OutlierArenaSubsystem.h"
#include "OutlierPlayerState.h"

bool AOutlierGameMode::IsInitialIntroParticipant(
	const AFirstPersonPlayerController* PC, const FGuid& IntroId) const
{
	if (!HasAuthority() || !bPendingInitialIntro || !InitialIntroId.IsValid()
		|| IntroId != InitialIntroId || !IsValid(PC) || PC->GetWorld() != GetWorld()
		|| (PC != InitialIntroShooter.Get() && PC != InitialIntroPartner.Get())) return false;
	const AOutlierPlayerState* PS = PC->GetPlayerState<AOutlierPlayerState>();
	const APawn* Pawn = PC->GetPawn();
	const EOutlierPlayerRole ExpectedRole = PC == InitialIntroShooter.Get()
		? EOutlierPlayerRole::Shooter : EOutlierPlayerRole::Partner;
	return IsValid(PS) && PS->GetPairId() == ActiveMatchPairId && PS->GetPlayerRole() == ExpectedRole
		&& IsValid(Pawn) && Pawn->GetController() == PC && Pawn->GetWorld() == GetWorld();
}

void AOutlierGameMode::HandleCinematicContext(
	AFirstPersonPlayerController* PC, const FOutlierCinematicContext& Context)
{
	if (!IsInitialIntroParticipant(PC, Context.IntroId)) return;

	switch (Context.Event)
	{
	case EOutlierCinematicClientEvent::Prepared:
		if (bInitialIntroFailed || bInitialIntroStarted) return;
		InitialIntroPreparedPlayers.Add(PC);
		TryStartInitialIntro();
		break;

	case EOutlierCinematicClientEvent::Ready:
		if (bInitialIntroFailed || !bInitialIntroStarted || !InitialIntroPreparedPlayers.Contains(PC)) return;
		InitialIntroReadyPlayers.Add(PC);
		TryFinishInitialIntro();
		break;

	case EOutlierCinematicClientEvent::Failed:
		if (bInitialIntroFailed) return;
		bInitialIntroFailed = true;
		// 게임 시작 차단은 유지하되, 상대도 시퀀스/메시 숨김/HUD 대기에 남지 않도록 함께 정리한다.
		if (InitialIntroShooter.IsValid()) InitialIntroShooter->ClientReleaseArenaIntro(Context.IntroId);
		if (InitialIntroPartner.IsValid()) InitialIntroPartner->ClientReleaseArenaIntro(Context.IntroId);
		UE_LOG(LogTemp, Error, TEXT("[Cinematic] Client failed Intro=%s PC=%s; gameplay start remains blocked"),
			*Context.IntroId.ToString(), *GetNameSafe(PC));
		break;

	case EOutlierCinematicClientEvent::CompleteRequested:
		if (bInitialIntroFailed || !bInitialIntroStarted || !InitialIntroPreparedPlayers.Contains(PC)) return;
		UE_LOG(LogTemp, Display, TEXT("[Cinematic] Completing Intro=%s at request of %s"),
			*Context.IntroId.ToString(), *GetNameSafe(PC));
		// 양쪽 클라이언트의 카메라와 입력이 복원된 후 게임을 시작한다.
		if (InitialIntroShooter.IsValid()) InitialIntroShooter->ClientCompleteArenaIntro(Context.IntroId);
		if (InitialIntroPartner.IsValid()) InitialIntroPartner->ClientCompleteArenaIntro(Context.IntroId);
		break;

	default:
		break;
	}
}

void AOutlierGameMode::TryStartInitialIntro()
{
	if (!HasAuthority() || !bPendingInitialIntro || bInitialIntroStarted || bInitialIntroFailed
		|| bPendingInitialSaveRestore || bArenaReloadInProgress || bArenaWorkerMatchCompleting
		|| bListenHostReturnRequested || !PendingPossessions.IsEmpty() || !PendingLocalPossessions.IsEmpty()
		|| !ArenaWorkerDisconnectedPlayerIds.IsEmpty() || bListenGuestDisconnected
		|| !IsInitialIntroParticipant(InitialIntroShooter.Get(), InitialIntroId)
		|| !IsInitialIntroParticipant(InitialIntroPartner.Get(), InitialIntroId)
		|| !InitialIntroPreparedPlayers.Contains(InitialIntroShooter)
		|| !InitialIntroPreparedPlayers.Contains(InitialIntroPartner)) return;
	const UOutlierArenaSubsystem* Arena = GetWorld()->GetSubsystem<UOutlierArenaSubsystem>();
	if (!Arena || !Arena->IsArenaContentReady()) return;
	if (IsArenaWorkerProcess() && (!bArenaWorkerPairStarted
		|| !ArenaWorkerReadyPlayers.Contains(ArenaWorkerShooterController)
		|| !ArenaWorkerReadyPlayers.Contains(ArenaWorkerPartnerController))) return;

	bInitialIntroStarted = true;
	// 시퀀스 재생을 위해 월드 정지를 해제한다. 게임 시작 조건은 bPendingInitialIntro로 계속 제어한다.
	ClearArenaWorkerWorldPause();
	GetWorld()->GetWorldSettings()->ForceNetUpdate();
	UE_LOG(LogTemp, Display, TEXT("[Cinematic] Both clients prepared. Starting Intro=%s"), *InitialIntroId.ToString());
	InitialIntroShooter->ClientStartArenaIntro(InitialIntroId);
	InitialIntroPartner->ClientStartArenaIntro(InitialIntroId);
}

void AOutlierGameMode::TryFinishInitialIntro()
{
	if (!HasAuthority() || !bPendingInitialIntro || !bInitialIntroStarted || bInitialIntroFailed
		|| bArenaReloadInProgress || bPendingInitialSaveRestore || bArenaWorkerMatchCompleting
		|| bListenHostReturnRequested || bListenGuestDisconnected
		|| !PendingPossessions.IsEmpty() || !PendingLocalPossessions.IsEmpty()
		|| !ArenaWorkerDisconnectedPlayerIds.IsEmpty()
		|| !IsInitialIntroParticipant(InitialIntroShooter.Get(), InitialIntroId)
		|| !IsInitialIntroParticipant(InitialIntroPartner.Get(), InitialIntroId)
		|| !InitialIntroReadyPlayers.Contains(InitialIntroShooter)
		|| !InitialIntroReadyPlayers.Contains(InitialIntroPartner)) return;
	const UOutlierArenaSubsystem* Arena = GetWorld()->GetSubsystem<UOutlierArenaSubsystem>();
	if (!Arena || !Arena->IsArenaContentReady()) return;

	bPendingInitialIntro = false;
	UE_LOG(LogTemp, Display, TEXT("[Cinematic] Both clients restored. Completing Intro=%s"), *InitialIntroId.ToString());
	if (IsArenaWorkerProcess())
	{
		// 기존 지연 시작 처리가 실제로 게임을 시작할 때까지 입력 범위를 유지한다.
		ScheduleArenaWorkerGameplayStart();
	}
	else
	{
		ReleaseInitialIntroInput();
		BroadcastPairGameplayReadyIfReady();
	}
}

void AOutlierGameMode::ReleaseInitialIntroInput()
{
	if (!InitialIntroId.IsValid() || bPendingInitialIntro) return;
	if (InitialIntroShooter.IsValid()) InitialIntroShooter->ClientReleaseArenaIntro(InitialIntroId);
	if (InitialIntroPartner.IsValid()) InitialIntroPartner->ClientReleaseArenaIntro(InitialIntroId);
	// Listen/ArenaWorker 두 경로 모두 인트로 완료 후 실제 게임 재개 시점에 여기를 한 번 지난다.
	// EventCallOnce가 양측 PC 전달과 게임 플로우당 1회 보장을 처리한다.
	if (InitialIntroCompletedEventTag.IsValid()
		&& !UOutlierEventSubsystem::EventCallOnce(this, InitialIntroCompletedEventTag))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Cinematic] Intro completed event '%s' was not delivered."),
			*InitialIntroCompletedEventTag.ToString());
	}
	InitialIntroId.Invalidate();
	InitialIntroPreparedPlayers.Reset();
	InitialIntroReadyPlayers.Reset();
}
