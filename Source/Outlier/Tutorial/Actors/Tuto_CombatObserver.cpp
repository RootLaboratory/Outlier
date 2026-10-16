#include "Tutorial/Actors/Tuto_CombatObserver.h"

#include "Enemy/EnemyAdaptationSubsystem.h"
#include "Components/BillboardComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/IConsoleManager.h"
#include "Outlier.h"
#include "OutlierPlayerState.h"
#include "Room/RoomVolume.h"
#include "Save/OutlierSaveSubSystem.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideTargetComponent.h"

namespace
{
#if !UE_BUILD_SHIPPING
	TAutoConsoleVariable<int32> CVarCombatObserverDebug(TEXT("Outlier.Tuto.CombatObserver.Debug"), 0,
		TEXT("1: log every combat/adaptation event the observer receives and why it fires or skips a slot. 0: off."));
#endif

	void Trace(const ATuto_CombatObserver* Observer, const FString& Message)
	{
#if !UE_BUILD_SHIPPING
		if (CVarCombatObserverDebug.GetValueOnGameThread() != 0)
			UE_LOG(LogOutlier, Log, TEXT("[CombatObserverDebug][%s] %s"), *GetNameSafe(Observer), *Message);
#endif
	}

	const TCHAR* SlotName(int32 Slot)
	{
		switch (Slot)
		{
		case ATuto_CombatObserver::ReinforcementSlot: return TEXT("Reinforcement");
		case ATuto_CombatObserver::ResistanceSlot: return TEXT("Resistance");
		case ATuto_CombatObserver::ClearedSlot: return TEXT("Cleared");
		default: return TEXT("?");
		}
	}

#if !UE_BUILD_SHIPPING
	// 관찰은 서버에서만 실행되므로 PIE 클라이언트 창은 같은 프로세스의 서버 월드로 전달한다.
	UWorld* FindServerWorld(UWorld* World)
	{
		if (World && World->GetNetMode() != NM_Client) return World;
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* Candidate = Context.World();
				if (Candidate && Candidate->WorldType == EWorldType::PIE && Candidate->GetNetMode() != NM_Client)
					return Candidate;
			}
		}
		UE_LOG(LogOutlier, Warning, TEXT("[CombatObserverDebug] No server world in this process; run it on the host."));
		return nullptr;
	}

	template <typename FunctionType>
	void ForEachObserver(UWorld* World, FunctionType&& Function)
	{
		UWorld* ServerWorld = FindServerWorld(World);
		if (!ServerWorld) return;
		int32 Count = 0;
		for (TActorIterator<ATuto_CombatObserver> It(ServerWorld); It; ++It, ++Count) Function(**It);
		if (Count == 0)
			UE_LOG(LogOutlier, Warning, TEXT("[CombatObserverDebug][%s] No Tuto_CombatObserver alive (not placed, or already destroyed by RoomCleared)."),
				*ServerWorld->GetName());
	}

	FAutoConsoleCommandWithWorldAndArgs CombatObserverDumpCommand(TEXT("Outlier.Tuto.CombatObserver.Dump"),
		TEXT("Dump each observer's RoomTag, slots and the observed room's combat state."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			ForEachObserver(World, [](const ATuto_CombatObserver& Observer)
			{
				UE_LOG(LogOutlier, Log, TEXT("[CombatObserverDebug] %s"), *Observer.BuildDebugReport());
			});
		}));

	FAutoConsoleCommandWithWorldAndArgs FireCommand(TEXT("Outlier.Tuto.CombatObserver.Fire"),
		TEXT("Fire <0|1|2>: run the Reinforcement/Resistance/Cleared handler without combat. One-shot record applies."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const int32 Slot = Args.IsEmpty() ? INDEX_NONE : FCString::Atoi(*Args[0]);
			if (Slot < ATuto_CombatObserver::ReinforcementSlot || Slot > ATuto_CombatObserver::ClearedSlot)
			{
				UE_LOG(LogOutlier, Warning, TEXT("[CombatObserverDebug] Usage: Outlier.Tuto.CombatObserver.Fire <0|1|2>"));
				return;
			}
			ForEachObserver(World, [Slot](ATuto_CombatObserver& Observer) { Observer.DebugFireSlot(Slot); });
		}));

	FAutoConsoleCommandWithWorldAndArgs CombatObserverResetCommand(TEXT("Outlier.Tuto.CombatObserver.Reset"),
		TEXT("Clear every observer's one-shot slot record so the slots can fire again."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			ForEachObserver(World, [](ATuto_CombatObserver& Observer)
			{
				Observer.ResetSubmittedEventsForDebug();
				UE_LOG(LogOutlier, Log, TEXT("[CombatObserverDebug][%s] One-shot slots reset."), *Observer.GetName());
			});
		}));
#endif
}

ATuto_CombatObserver::ATuto_CombatObserver()
{
	EventTriggers.SetNum(3);
	USceneComponent* GuideRoot = CreateDefaultSubobject<USceneComponent>(TEXT("GuideRoot"));
	SetRootComponent(GuideRoot);
	WorldGuideTarget = CreateDefaultSubobject<UOutlierWorldGuideTargetComponent>(TEXT("WorldGuideTarget"));
	WorldGuideTarget->SetupAttachment(GuideRoot);
#if WITH_EDITORONLY_DATA
	// 배치용 마커일 뿐이며, 게임 중에는 observer의 어떤 것도 렌더링하지 않는다.
	if (UBillboardComponent* Sprite = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite")))
	{
		Sprite->SetupAttachment(GuideRoot);
		Sprite->bIsScreenSizeScaled = true;
	}
#endif
	// 관찰은 서버 전용이다. 클라이언트에는 target anchor와 공유 파괴만 필요하다.
	bNetLoadOnClient = true;
	bReplicates = true;
}

void ATuto_CombatObserver::BeginPlay()
{
	Super::BeginPlay();
	UWorld* World = GetWorld();
	if (!HasAuthority() || !World || World->GetNetMode() == NM_Client) return;
	if (!RoomTag.IsValid())
	{
		UE_LOG(LogOutlier, Warning, TEXT("[EventTrigger] %s has no RoomTag; combat events are not observed."), *GetName());
		return;
	}
	// 다른 one-shot 액터처럼 sublevel 리로드가 복원된 월드 진행 상태로 이 observer를 다시 생성한다.
	// 그 시점에 이미 클리어된 방은 RoomCleared를 다시 보고하지 않으므로 observer가 할 일이 없다.
	const UOutlierSaveSubSystem* Saves = GetGameInstance() ? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>() : nullptr;
	if (Saves && Saves->HasWorldProgress(EOutlierWorldProgressType::CompletedEncounter, RoomTag.GetTagName()))
	{
		Trace(this, FString::Printf(TEXT("Room=%s already cleared on BeginPlay; destroyed."), *RoomTag.ToString()));
		Destroy();
		return;
	}
	// 체크포인트 이후 리로드로 다시 생성됐지만 맡은 이벤트가 모두 이미 나갔으면 더 관찰할 것이 없다.
	if (AreAllSlotsFired())
	{
		UE_LOG(LogOutlier, Log, TEXT("[EventTrigger] %s: every slot already fired in this game flow; destroyed on BeginPlay."), *GetName());
		Destroy();
		return;
	}
	if (URoomCombatSubsystem* Combat = World->GetSubsystem<URoomCombatSubsystem>())
	{
		ObservedCombat = Combat;
		Combat->OnCombatEvent.AddDynamic(this, &ThisClass::HandleCombatEvent);
		Combat->OnReinforcementStarted.AddUObject(this, &ThisClass::HandleReinforcementStarted);
		// 전투 도중의 체크포인트로 복원되면 이미 시작된 증원은 다시 알려지지 않으므로 현재 Wave를 한 번 확인한다.
		if (Combat->GetRoomState(RoomTag) == ERoomCombatState::Combat)
			TryReinforcementAtWave(Combat->GetCurrentWaveIndex(RoomTag));
	}
	if (UEnemyAdaptationSubsystem* Adaptation = World->GetSubsystem<UEnemyAdaptationSubsystem>())
	{
		ObservedAdaptation = Adaptation;
		Adaptation->OnAdaptationUpdated.AddUObject(this, &ThisClass::HandleAdaptationUpdated);
	}
	Trace(this, FString::Printf(TEXT("Observing Room=%s Combat=%d Adaptation=%d ReinforcementWave=%d"),
		*RoomTag.ToString(), ObservedCombat.IsValid() ? 1 : 0, ObservedAdaptation.IsValid() ? 1 : 0, ReinforcementWaveIndex));
}

bool ATuto_CombatObserver::IsSlotFired(int32 Slot) const
{
	const FGameplayTag Tag = GetEventTag(Slot);
	const UOutlierSaveSubSystem* Saves = GetGameInstance() ? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>() : nullptr;
	return IsEventSubmitted(Slot) || (Tag.IsValid() && Saves && Saves->HasFiredTutorialEvent(Tag));
}

bool ATuto_CombatObserver::AreAllSlotsFired() const
{
	bool bAnyConfigured = false;
	for (int32 Slot = ReinforcementSlot; Slot <= ClearedSlot; ++Slot)
	{
		if (!GetEventTag(Slot).IsValid()) continue;
		bAnyConfigured = true;
		if (!IsSlotFired(Slot)) return false;
	}
	return bAnyConfigured;
}

bool ATuto_CombatObserver::IsReinforcementWave(int32 WaveIndex) const
{
	// Wave 0은 각 phase의 시작 병력이므로 증원이 아니다.
	return ReinforcementWaveIndex == INDEX_NONE ? WaveIndex >= 1 : WaveIndex == ReinforcementWaveIndex;
}

void ATuto_CombatObserver::TryReinforcementAtWave(int32 WaveIndex)
{
	// 증원 slot이 비었거나 이미 나갔으면 플레이어를 찾을 필요가 없다.
	if (!IsReinforcementWave(WaveIndex) || !GetEventTag(ReinforcementSlot).IsValid() || IsSlotFired(ReinforcementSlot)) return;
	if (AOutlierPlayerState* Source = FindPlayerInRoom()) HandleReinforcement(Source);
}

void ATuto_CombatObserver::HandleReinforcementStarted(FGameplayTag EventRoomTag, int32 CombatPhaseIndex, int32 WaveIndex,
	int32 GameplayGeneration)
{
	if (EventRoomTag != RoomTag) return;
	Trace(this, FString::Printf(TEXT("ReinforcementStarted Phase=%d Wave=%d Gen=%d"), CombatPhaseIndex, WaveIndex, GameplayGeneration));
	TryReinforcementAtWave(WaveIndex);
}

void ATuto_CombatObserver::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopObserving();
	Super::EndPlay(EndPlayReason);
}

void ATuto_CombatObserver::HandleReinforcement(AOutlierPlayerState* SourcePlayerState)
{
	const bool bSent = TriggerEvent(ReinforcementSlot, SourcePlayerState);
	Trace(this, FString::Printf(TEXT("Reinforcement slot -> %s"), bSent ? TEXT("sent") : TEXT("rejected (already sent/empty tag/recipients)")));
}

void ATuto_CombatObserver::HandleFirstResistance(AOutlierPlayerState* SourcePlayerState)
{
	const bool bSent = TriggerEvent(ResistanceSlot, SourcePlayerState);
	Trace(this, FString::Printf(TEXT("Resistance slot -> %s"), bSent ? TEXT("sent") : TEXT("rejected (already sent/empty tag/recipients)")));
}

void ATuto_CombatObserver::HandleRoomCleared(AOutlierPlayerState* SourcePlayerState)
{
	const bool bSent = TriggerEvent(ClearedSlot, SourcePlayerState);
	Trace(this, FString::Printf(TEXT("Cleared slot -> %s"), bSent ? TEXT("sent") : TEXT("rejected (already sent/empty tag/recipients)")));
}

void ATuto_CombatObserver::HandleCombatEvent(FGameplayTag EventRoomTag, ERoomCombatEvent Event,
	int32 CombatPhaseIndex, int32 GameplayGeneration)
{
	if (EventRoomTag != RoomTag) return;
	const URoomCombatSubsystem* Combat = ObservedCombat.Get();
	Trace(this, FString::Printf(TEXT("CombatEvent %s Phase=%d Gen=%d -> RoomState=%s CurrentPhase=%d"),
		*UEnum::GetValueAsString(Event), CombatPhaseIndex, GameplayGeneration,
		Combat ? *UEnum::GetValueAsString(Combat->GetRoomState(RoomTag)) : TEXT("None"),
		Combat ? Combat->GetCurrentCombatPhaseIndex(RoomTag) : INDEX_NONE));
	if (Event != ERoomCombatEvent::RoomCleared) return;
	if (AOutlierPlayerState* Source = FindPlayerInRoom()) HandleRoomCleared(Source);
	// 클리어된 방에서는 증원과 저항이 더 이상 발생하지 않으므로 observer를 종료한다.
	Trace(this, TEXT("Room cleared; observer stops and is destroyed."));
	StopObserving();
	Destroy();
}

void ATuto_CombatObserver::HandleAdaptationUpdated(const FEnemyAdaptationUpdateResult& Result)
{
	const URoomCombatSubsystem* Combat = ObservedCombat.Get();
	const FGameplayTag ActiveRoom = Combat ? Combat->GetActiveCombatRoomTag() : FGameplayTag();
	Trace(this, FString::Printf(TEXT("Adaptation %s Stack %d->%d State %s->%s ActiveRoom=%s"),
		*UEnum::GetValueAsString(Result.KillCategory), Result.PreviousStack, Result.CurrentStack,
		*UEnum::GetValueAsString(Result.PreviousState), *UEnum::GetValueAsString(Result.CurrentState), *ActiveRoom.ToString()));
	// 저장/체크포인트 복원은 Ignore로 보고되므로, 누적 총기 킬로 저항에 진입한 경우만 센다.
	// 해제 후 반복은 pair당 1회 slot 기록으로 거부된다.
	if (Result.KillCategory != EEnemyFinalKillCategory::Gun
		|| Result.PreviousState >= EEnemyAdaptationState::ResistanceLevel1
		|| Result.CurrentState < EEnemyAdaptationState::ResistanceLevel1
		|| !Combat || ActiveRoom != RoomTag) return;
	if (AOutlierPlayerState* Source = FindPlayerInRoom()) HandleFirstResistance(Source);
}

FString ATuto_CombatObserver::BuildDebugReport() const
{
	const URoomCombatSubsystem* Combat = ObservedCombat.Get();
	int32 RoomVolumes = 0;
	if (const UWorld* World = GetWorld())
	{
		for (TActorIterator<ARoomVolume> It(World); It; ++It)
			if (IsValid(*It) && It->GetRoomTag() == RoomTag) ++RoomVolumes;
	}
	FString Report = FString::Printf(
		TEXT("%s Room=%s RoomVolumes=%d ReinforcementWave=%d Observing(Combat=%d Adaptation=%d) ActiveRoom=%s RoomState=%s CurrentPhase=%d CurrentWave=%d"),
		*GetName(), *RoomTag.ToString(), RoomVolumes, ReinforcementWaveIndex,
		Combat ? 1 : 0, ObservedAdaptation.IsValid() ? 1 : 0,
		Combat ? *Combat->GetActiveCombatRoomTag().ToString() : TEXT("None"),
		Combat ? *UEnum::GetValueAsString(Combat->GetRoomState(RoomTag)) : TEXT("None"),
		Combat ? Combat->GetCurrentCombatPhaseIndex(RoomTag) : INDEX_NONE,
		Combat ? Combat->GetCurrentWaveIndex(RoomTag) : INDEX_NONE);
	const UOutlierSaveSubSystem* Saves = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>() : nullptr;
	for (int32 Slot = ReinforcementSlot; Slot <= ClearedSlot; ++Slot)
	{
		const FGameplayTag Tag = GetEventTag(Slot);
		Report += FString::Printf(TEXT("\n    [%d] %-13s Tag=%s Submitted=%d FiredInFlow=%d"), Slot, SlotName(Slot),
			Tag.IsValid() ? *Tag.ToString() : TEXT("(empty)"), IsEventSubmitted(Slot) ? 1 : 0,
			Saves && Saves->HasFiredTutorialEvent(Tag) ? 1 : 0);
	}
	return Report;
}

#if !UE_BUILD_SHIPPING
bool ATuto_CombatObserver::DebugFireSlot(int32 Slot)
{
	AOutlierPlayerState* Source = FindPlayerInRoom();
	if (!Source) return false;
	const UOutlierSaveSubSystem* Saves = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>() : nullptr;
	// game-flow 기록 적중도 slot을 제출됨으로 표시하므로, 발동 전에 두 기록을 모두 읽는다.
	const bool bWasSubmitted = IsEventSubmitted(Slot) || (Saves && Saves->HasFiredTutorialEvent(GetEventTag(Slot)));
	switch (Slot)
	{
	case ReinforcementSlot: HandleReinforcement(Source); break;
	case ResistanceSlot: HandleFirstResistance(Source); break;
	case ClearedSlot: HandleRoomCleared(Source); break;
	default: return false;
	}
	const bool bSent = !bWasSubmitted && IsEventSubmitted(Slot);
	UE_LOG(LogOutlier, Log, TEXT("[CombatObserverDebug][%s] Fire %s -> %s"), *GetName(), SlotName(Slot),
		bSent ? TEXT("sent") : bWasSubmitted ? TEXT("rejected: already sent or fired in this game flow (use Outlier.Tuto.CombatObserver.Reset)")
			: TEXT("rejected: empty tag or recipients not resolved"));
	return bSent;
}
#endif

AOutlierPlayerState* ATuto_CombatObserver::FindPlayerInRoom() const
{
	UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (!GameState) return nullptr;
	const ARoomVolume* Room = nullptr;
	for (TActorIterator<ARoomVolume> It(World); It && !Room; ++It)
		if (IsValid(*It) && It->GetRoomTag() == RoomTag) Room = *It;

	AOutlierPlayerState* AnyPaired = nullptr;
	for (APlayerState* PlayerState : GameState->PlayerArray)
	{
		AOutlierPlayerState* State = Cast<AOutlierPlayerState>(PlayerState);
		// 조종 중인 폰을 사용하므로, 적에 빙의한 파트너는 그 적의 위치로 판정한다.
		const APawn* Pawn = IsValid(State) ? State->GetPawn() : nullptr;
		if (!IsValid(Pawn) || State->GetPairId() == INDEX_NONE) continue;
		if (!AnyPaired) AnyPaired = State;
		if (Room && Room->ContainsWorldLocation(Pawn->GetActorLocation())) return State;
	}
	// 마지막 적을 방 밖에서 잡거나 클리어 순간 다른 방에 있으면 방 안에 아무도 없다.
	// 이 PS는 수신 Pair를 정할 뿐이고 Arena 워커에는 한 Pair만 있으므로, Pair가 정해진 아무 플레이어로 대체한다.
	if (AnyPaired)
	{
		UE_LOG(LogOutlier, Log, TEXT("[EventTrigger] %s found no player inside room '%s'; using paired player %s."),
			*GetName(), *RoomTag.ToString(), *GetNameSafe(AnyPaired));
		return AnyPaired;
	}
	UE_LOG(LogOutlier, Warning, TEXT("[EventTrigger] %s found no paired player for room '%s'."),
		*GetName(), *RoomTag.ToString());
	return nullptr;
}

void ATuto_CombatObserver::StopObserving()
{
	if (URoomCombatSubsystem* Combat = ObservedCombat.Get())
	{
		Combat->OnCombatEvent.RemoveDynamic(this, &ThisClass::HandleCombatEvent);
		Combat->OnReinforcementStarted.RemoveAll(this);
	}
	if (UEnemyAdaptationSubsystem* Adaptation = ObservedAdaptation.Get())
		Adaptation->OnAdaptationUpdated.RemoveAll(this);
	ObservedCombat.Reset();
	ObservedAdaptation.Reset();
}
