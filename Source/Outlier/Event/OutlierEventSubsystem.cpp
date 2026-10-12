#include "Event/OutlierEventSubsystem.h"

#include "Audio/OutlierAudioSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Event/OutlierEventSettings.h"
#include "Event/OutlierEventDataAsset.h"
#include "GameFramework/PlayerController.h"
#include "LocalPlayerUISubSystem.h"
#include "Outlier.h"
#include "OutlierPlayerState.h"

namespace
{
	template <typename RowType>
	const RowType* FindTaggedRow(UDataTable* Table, FGameplayTag Tag, FGameplayTag RowType::* TagField)
	{
		if (!Table || !Tag.IsValid() || Table->GetRowStruct() != RowType::StaticStruct())
		{
			return nullptr;
		}
		const RowType* Row = Table->FindRow<RowType>(Tag.GetTagName(), TEXT("OutlierEvent"), false);
		if (Row && (Row->*TagField) != Tag)
		{
			UE_LOG(LogOutlier, Warning, TEXT("[Event] Row '%s' must have a matching tag."), *Tag.ToString());
			return nullptr;
		}
		return Row;
	}

	const TCHAR* RegionName(EOutlierInfoRegion Region)
	{
		switch (Region)
		{
		case EOutlierInfoRegion::Upper: return TEXT("Upper");
		case EOutlierInfoRegion::Right: return TEXT("GIF");
		case EOutlierInfoRegion::BottomTTS: return TEXT("TTS");
		default: return TEXT("?");
		}
	}

	// Registers ChainFrom -> Row. A broken link is reported instead of silently never running.
	void AddChainLink(TMultiMap<FGameplayTag, FGameplayTag>& Chains, EOutlierInfoRegion Region, FGameplayTag Row,
		FGameplayTag ChainFrom, bool bMarkedChain, bool bPredecessorExists)
	{
		if (!Row.IsValid() || !ChainFrom.IsValid())
		{
			return;
		}
		if (!bMarkedChain)
		{
			UE_LOG(LogOutlier, Warning, TEXT("[Event] %s row '%s' has ChainFromTag '%s' but its TriggerType is not CHAIN."),
				RegionName(Region), *Row.ToString(), *ChainFrom.ToString());
		}
		if (!bPredecessorExists)
		{
			UE_LOG(LogOutlier, Warning, TEXT("[Event] %s row '%s' chains from '%s', which is not a %s row of this role; it never runs."),
				RegionName(Region), *Row.ToString(), *ChainFrom.ToString(), RegionName(Region));
			return;
		}
		Chains.AddUnique(ChainFrom, Row);
	}

	// A chain leading back to its own source would replay forever, so that source's links are dropped.
	void RemoveChainCycles(TMultiMap<FGameplayTag, FGameplayTag>& Chains, EOutlierInfoRegion Region)
	{
		TArray<FGameplayTag> Sources;
		Chains.GetKeys(Sources);
		for (const FGameplayTag& Source : Sources)
		{
			TArray<FGameplayTag> Pending;
			Chains.MultiFind(Source, Pending);
			TSet<FGameplayTag> Visited;
			while (!Pending.IsEmpty())
			{
				const FGameplayTag Tag = Pending.Pop();
				if (Tag == Source)
				{
					UE_LOG(LogOutlier, Warning, TEXT("[Event] %s chain from '%s' loops back to itself; its links are disabled."),
						RegionName(Region), *Source.ToString());
					Chains.Remove(Source);
					break;
				}
				bool bAlreadyVisited = false;
				Visited.Add(Tag, &bAlreadyVisited);
				if (!bAlreadyVisited)
				{
					TArray<FGameplayTag> Next;
					Chains.MultiFind(Tag, Next);
					Pending.Append(Next);
				}
			}
		}
	}
}

void UOutlierEventSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<ULocalPlayerUISubSystem>();
}

void UOutlierEventSubsystem::Deinitialize()
{
	if (AOutlierPlayerState* PlayerState = CachedPlayerState.Get()) PlayerState->OnPlayerRoleChanged.RemoveAll(this);
	if (ULocalPlayerUISubSystem* UI = GetLocalPlayer()->GetSubsystem<ULocalPlayerUISubSystem>())
	{
		UI->ClearEventInfo();
	}
	if (UOutlierAudioSubsystem* Audio = BoundAudioSubsystem.Get())
	{
		Audio->OnVoiceStarted.RemoveAll(this);
		Audio->OnVoiceFinished.RemoveAll(this);
		Audio->OnVoiceUnavailable.RemoveAll(this);
		Audio->StopLocalVoices(GetLocalPlayer());
	}
	Super::Deinitialize();
}

void UOutlierEventSubsystem::ClearPresentation()
{
	if (ULocalPlayerUISubSystem* UI = GetLocalPlayer()->GetSubsystem<ULocalPlayerUISubSystem>())
	{
		UI->ClearEventInfo();
	}
	if (UOutlierAudioSubsystem* Audio = BoundAudioSubsystem.Get())
	{
		Audio->StopLocalVoices(GetLocalPlayer());
	}
}

bool UOutlierEventSubsystem::ApplyDataAsset(UOutlierEventDataAsset* DataAsset)
{
	if (bApplyingDataAsset) return false;
	TGuardValue<bool> ApplyingGuard(bApplyingDataAsset, true);
	UDataTable* NewEvent = DataAsset ? DataAsset->EventTable.LoadSynchronous() : nullptr;
	UDataTable* NewTTS = DataAsset ? DataAsset->TTSTable.LoadSynchronous() : nullptr;
	UDataTable* NewInput = DataAsset ? DataAsset->InputTable.LoadSynchronous() : nullptr;
	UDataTable* NewGoal = DataAsset ? DataAsset->GoalTable.LoadSynchronous() : nullptr;
	UDataTable* NewGif = DataAsset ? DataAsset->GifTable.LoadSynchronous() : nullptr;
	if (DataAsset && ((!DataAsset->EventTable.IsNull() && !NewEvent)
		|| (!DataAsset->TTSTable.IsNull() && !NewTTS)
		|| (!DataAsset->InputTable.IsNull() && !NewInput)
		|| (!DataAsset->GoalTable.IsNull() && !NewGoal)
		|| (!DataAsset->GifTable.IsNull() && !NewGif)
		|| (NewEvent && NewEvent->GetRowStruct() != FOutlierEventDataRow::StaticStruct())
		|| (NewTTS && NewTTS->GetRowStruct() != FOutlierTTSDataRow::StaticStruct())
		|| (NewInput && NewInput->GetRowStruct() != FOutlierInputDataRow::StaticStruct())
		|| (NewGoal && NewGoal->GetRowStruct() != FOutlierGoalDataRow::StaticStruct())
		|| (NewGif && NewGif->GetRowStruct() != FOutlierGifDataRow::StaticStruct())))
	{
		UE_LOG(LogOutlier, Warning, TEXT("[Event] Data asset '%s' has unavailable tables or incompatible row structures."), *GetNameSafe(DataAsset));
		return false;
	}
	ClearPresentation();
	CachedDataAsset = DataAsset;
	EventTable = NewEvent;
	TTSTable = NewTTS;
	InputTable = NewInput;
	GoalTable = NewGoal;
	GifTable = NewGif;
	RebuildChains();
	return true;
}

void UOutlierEventSubsystem::RebuildChains()
{
	TTSChains.Reset();
	UpperChains.Reset();
	GifChains.Reset();
	constexpr EOutlierEventTriggerType Chain = EOutlierEventTriggerType::CHAIN;
	if (TTSTable && TTSTable->GetRowStruct() == FOutlierTTSDataRow::StaticStruct())
	{
		TTSTable->ForeachRow<FOutlierTTSDataRow>(TEXT("OutlierEventChain"), [this](const FName&, const FOutlierTTSDataRow& Row)
		{
			AddChainLink(TTSChains, EOutlierInfoRegion::BottomTTS, Row.TTSTag, Row.ChainFromTag, Row.TriggerType == Chain,
				FindTaggedRow<FOutlierTTSDataRow>(TTSTable, Row.ChainFromTag, &FOutlierTTSDataRow::TTSTag) != nullptr);
		});
	}
	// Input and Goal share the upper region, so either may follow the other.
	auto IsUpperRow = [this](FGameplayTag Tag)
	{
		return FindTaggedRow<FOutlierInputDataRow>(InputTable, Tag, &FOutlierInputDataRow::InputTag) != nullptr
			|| FindTaggedRow<FOutlierGoalDataRow>(GoalTable, Tag, &FOutlierGoalDataRow::GoalTag) != nullptr;
	};
	if (InputTable && InputTable->GetRowStruct() == FOutlierInputDataRow::StaticStruct())
	{
		InputTable->ForeachRow<FOutlierInputDataRow>(TEXT("OutlierEventChain"), [&](const FName&, const FOutlierInputDataRow& Row)
		{
			AddChainLink(UpperChains, EOutlierInfoRegion::Upper, Row.InputTag, Row.ChainFromTag, Row.TriggerType == Chain,
				IsUpperRow(Row.ChainFromTag));
		});
	}
	if (GoalTable && GoalTable->GetRowStruct() == FOutlierGoalDataRow::StaticStruct())
	{
		GoalTable->ForeachRow<FOutlierGoalDataRow>(TEXT("OutlierEventChain"), [&](const FName&, const FOutlierGoalDataRow& Row)
		{
			AddChainLink(UpperChains, EOutlierInfoRegion::Upper, Row.GoalTag, Row.ChainFromTag, Row.TriggerType == Chain,
				IsUpperRow(Row.ChainFromTag));
		});
	}
	if (GifTable && GifTable->GetRowStruct() == FOutlierGifDataRow::StaticStruct())
	{
		GifTable->ForeachRow<FOutlierGifDataRow>(TEXT("OutlierEventChain"), [this](const FName&, const FOutlierGifDataRow& Row)
		{
			AddChainLink(GifChains, EOutlierInfoRegion::Right, Row.Tag, Row.ChainFromTag, true,
				FindTaggedRow<FOutlierGifDataRow>(GifTable, Row.ChainFromTag, &FOutlierGifDataRow::Tag) != nullptr);
		});
	}
	RemoveChainCycles(TTSChains, EOutlierInfoRegion::BottomTTS);
	RemoveChainCycles(UpperChains, EOutlierInfoRegion::Upper);
	RemoveChainCycles(GifChains, EOutlierInfoRegion::Right);
}

void UOutlierEventSubsystem::NotifyInfoCompleted(EOutlierInfoRegion Region, const FOutlierInfoWidgetRequest& Request)
{
	UWorld* World = GetWorld();
	if (!World || World->bIsTearingDown)
	{
		return;
	}
	LoadConfiguredTables();
	// Chains never cross regions: TTS -> TTS, Upper -> Upper, GIF -> GIF.
	FGameplayTag CompletedTag;
	TArray<FGameplayTag> NextTags;
	switch (Region)
	{
	case EOutlierInfoRegion::Upper:
		CompletedTag = Request.UpperTag;
		UpperChains.MultiFind(CompletedTag, NextTags, true);
		break;
	case EOutlierInfoRegion::Right:
		CompletedTag = Request.GifTag;
		GifChains.MultiFind(CompletedTag, NextTags, true);
		break;
	case EOutlierInfoRegion::BottomTTS:
		CompletedTag = Request.TTSTag.IsValid() ? Request.TTSTag : ResolveRequestTTSTag(Request.EventTag);
		TTSChains.MultiFind(CompletedTag, NextTags, true);
		break;
	default:
		return;
	}
	ULocalPlayerUISubSystem* UI = GetLocalPlayer()->GetSubsystem<ULocalPlayerUISubSystem>();
	for (const FGameplayTag& NextTag : NextTags)
	{
		bool bStarted = false;
		if (Region == EOutlierInfoRegion::Upper)
		{
			// Chained upper rows close after their DT Duration, like trigger-sent ones.
			FOutlierEventContext Context;
			Context.EndCondition = EOutlierEventEndCondition::Time;
			bStarted = SendUpper(NextTag, Context);
		}
		else if (Region == EOutlierInfoRegion::Right)
		{
			bStarted = UI && UI->ShowEventGif(NextTag, NextTag);
		}
		else
		{
			bStarted = SendTTS(NextTag);
		}
		UE_LOG(LogOutlier, Log, TEXT("[Event] %s chain '%s' -> '%s' %s."), RegionName(Region),
			*CompletedTag.ToString(), *NextTag.ToString(), bStarted ? TEXT("started") : TEXT("rejected"));
	}
}

bool UOutlierEventSubsystem::SetEventDataAsset(UOutlierEventDataAsset* DataAsset)
{
	if (!DataAsset || !ApplyDataAsset(DataAsset)) return false;
	bDataAssetOverridden = true;
	return true;
}

void UOutlierEventSubsystem::RefreshEventDataAsset()
{
	bDataAssetOverridden = false;
	bRoleDataAssetResolved = false;
	CachedRole = EOutlierPlayerRole::None;
	ApplyDataAsset(nullptr);
	LoadConfiguredTables();
}

UOutlierEventDataAsset* UOutlierEventSubsystem::GetEventDataAsset()
{
	LoadConfiguredTables();
	return CachedDataAsset;
}

void UOutlierEventSubsystem::HandlePlayerRoleChanged(AOutlierPlayerState* PlayerState)
{
	if (PlayerState == CachedPlayerState.Get()) LoadConfiguredTables();
}

void UOutlierEventSubsystem::LoadConfiguredTables()
{
	if (bDataAssetOverridden || bApplyingDataAsset) return;
	const APlayerController* Controller = GetLocalPlayer()->GetPlayerController(GetWorld());
	AOutlierPlayerState* PlayerState = Controller ? Controller->GetPlayerState<AOutlierPlayerState>() : nullptr;
	const EOutlierPlayerRole Role = PlayerState ? PlayerState->GetPlayerRole() : EOutlierPlayerRole::None;
	if (bRoleDataAssetResolved && PlayerState == CachedPlayerState.Get() && Role == CachedRole) return;
	if (PlayerState != CachedPlayerState.Get())
	{
		if (AOutlierPlayerState* Previous = CachedPlayerState.Get()) Previous->OnPlayerRoleChanged.RemoveAll(this);
		CachedPlayerState = PlayerState;
		if (PlayerState) PlayerState->OnPlayerRoleChanged.AddUObject(this, &ThisClass::HandlePlayerRoleChanged);
	}
	CachedRole = Role;
	bRoleDataAssetResolved = true;
	const UOutlierEventSettings* Settings = GetDefault<UOutlierEventSettings>();
	UOutlierEventDataAsset* DataAsset = Role == EOutlierPlayerRole::Shooter ? Settings->ShooterDataAsset.LoadSynchronous()
		: Role == EOutlierPlayerRole::Partner ? Settings->PartnerDataAsset.LoadSynchronous() : nullptr;
	// Never keep a previous role's tables if the new role is unconfigured or invalid.
	if (!ApplyDataAsset(DataAsset)) ApplyDataAsset(nullptr);
}

bool UOutlierEventSubsystem::TryGetEventData(FGameplayTag EventTag, FOutlierEventDataRow& OutData)
{
	OutData = FOutlierEventDataRow();
	LoadConfiguredTables();
	if (const FOutlierEventDataRow* Row = FindTaggedRow<FOutlierEventDataRow>(
		EventTable, EventTag, &FOutlierEventDataRow::EventTag))
	{
		OutData = *Row;
		return true;
	}
	return false;
}

bool UOutlierEventSubsystem::TryGetTTSData(FGameplayTag TTSTag, FOutlierTTSDataRow& OutData)
{
	OutData = FOutlierTTSDataRow();
	LoadConfiguredTables();
	if (const FOutlierTTSDataRow* Row = FindTaggedRow<FOutlierTTSDataRow>(
		TTSTable, TTSTag, &FOutlierTTSDataRow::TTSTag))
	{
		OutData = *Row;
		return true;
	}
	return false;
}

bool UOutlierEventSubsystem::TryGetInputData(FGameplayTag InputTag, FOutlierInputDataRow& OutData)
{
	OutData = FOutlierInputDataRow();
	LoadConfiguredTables();
	if (const FOutlierInputDataRow* Row = FindTaggedRow<FOutlierInputDataRow>(InputTable, InputTag, &FOutlierInputDataRow::InputTag))
	{
		OutData = *Row;
		return true;
	}
	return false;
}

bool UOutlierEventSubsystem::TryGetGoalData(FGameplayTag GoalTag, FOutlierGoalDataRow& OutData)
{
	OutData = FOutlierGoalDataRow();
	LoadConfiguredTables();
	if (const FOutlierGoalDataRow* Row = FindTaggedRow<FOutlierGoalDataRow>(GoalTable, GoalTag, &FOutlierGoalDataRow::GoalTag))
	{
		OutData = *Row;
		return true;
	}
	return false;
}

bool UOutlierEventSubsystem::TryGetUpperData(FGameplayTag UpperTag, FOutlierUpperData& OutData)
{
	OutData = FOutlierUpperData();
	FOutlierInputDataRow Input;
	if (TryGetInputData(UpperTag, Input))
	{
		OutData.UpperTag = Input.InputTag;
		OutData.Type = EOutlierUpperInfoType::Input;
		OutData.TriggerType = Input.TriggerType;
		OutData.ChainFromTag = Input.ChainFromTag;
		OutData.Text = Input.Text;
		OutData.InputAction = Input.InputAction;
		OutData.Duration = Input.Duration;
		return true;
	}
	FOutlierGoalDataRow Goal;
	if (TryGetGoalData(UpperTag, Goal))
	{
		OutData.UpperTag = Goal.GoalTag;
		OutData.Type = EOutlierUpperInfoType::Goal;
		OutData.TriggerType = Goal.TriggerType;
		OutData.ChainFromTag = Goal.ChainFromTag;
		OutData.Text = Goal.Text;
		OutData.Duration = Goal.Duration;
		return true;
	}
	return false;
}

bool UOutlierEventSubsystem::TryGetEventUpperData(FGameplayTag EventTag, FOutlierUpperData& OutData)
{
	OutData = FOutlierUpperData();
	FOutlierEventDataRow Event;
	return TryGetEventData(EventTag, Event) && TryGetUpperData(ResolveUpperTag(Event), OutData);
}

FGameplayTag UOutlierEventSubsystem::ResolveUpperTag(const FOutlierEventDataRow& Event) const
{
	if (Event.InputTag.IsValid() && Event.GoalTag.IsValid())
	{
		UE_LOG(LogOutlier, Warning, TEXT("[Event] '%s' must select either InputTag or GoalTag for the Upper region."), *Event.EventTag.ToString());
		return FGameplayTag();
	}
	return Event.InputTag.IsValid() ? Event.InputTag : Event.GoalTag;
}

bool UOutlierEventSubsystem::SendTTS(FGameplayTag TTSTag)
{
	UWorld* World = GetWorld();
	if (!World || World->bIsTearingDown || World->GetNetMode() == NM_DedicatedServer) return false;
	FOutlierTTSDataRow TTS;
	if (!TryGetTTSData(TTSTag, TTS)) return false;
	EnsureAudioCallbacks();
	UOutlierAudioSubsystem* Audio = BoundAudioSubsystem.Get();
	return Audio && Audio->PlayLocalVoice(TTSTag, static_cast<int32>(TTS.Priority), TTSTag, GetLocalPlayer());
}

bool UOutlierEventSubsystem::SendUpper(FGameplayTag UpperTag, const FOutlierEventContext& Context)
{
	return SubmitUpper(UpperTag, UpperTag, Context);
}

bool UOutlierEventSubsystem::SubmitUpper(FGameplayTag UpperTag, FGameplayTag RequestTag, const FOutlierEventContext& Context)
{
	UWorld* World = GetWorld();
	if (!World || World->bIsTearingDown || World->GetNetMode() == NM_DedicatedServer) return false;
	FOutlierUpperData Data;
	if (!TryGetUpperData(UpperTag, Data)) return false;
	ULocalPlayerUISubSystem* UI = GetLocalPlayer()->GetSubsystem<ULocalPlayerUISubSystem>();
	if (!UI) return false;
	FOutlierInfoWidgetRequest Request;
	Request.EventTag = RequestTag;
	Request.UpperTag = UpperTag;
	Request.NextEventTag = Context.NextEventTag;
	switch (Context.EndCondition)
	{
	case EOutlierEventEndCondition::Manual:
		Request.EndCondition = EOutlierInfoEndCondition::Manual;
		break;
	case EOutlierEventEndCondition::Time:
		Request.EndCondition = EOutlierInfoEndCondition::Time;
		Request.Duration = Context.Duration;
		break;
	case EOutlierEventEndCondition::Overlap:
		Request.EndCondition = EOutlierInfoEndCondition::Overlap;
		break;
	default:
		return false;
	}
	return UI->ShowEventUpper(Request);
}

bool UOutlierEventSubsystem::TryGetGifData(FGameplayTag GifTag, FOutlierGifDataRow& OutData)
{
	OutData = FOutlierGifDataRow();
	LoadConfiguredTables();
	if (const FOutlierGifDataRow* Row = FindTaggedRow<FOutlierGifDataRow>(GifTable, GifTag, &FOutlierGifDataRow::Tag))
	{
		OutData = *Row;
		return true;
	}
	return false;
}

bool UOutlierEventSubsystem::TryGetEventGifData(FGameplayTag EventTag, FOutlierGifDataRow& OutData)
{
	OutData = FOutlierGifDataRow();
	FOutlierEventDataRow Event;
	return TryGetEventData(EventTag, Event) && TryGetGifData(Event.GifTag, OutData);
}

bool UOutlierEventSubsystem::SendEvent(FGameplayTag EventTag, const FOutlierEventContext& Context)
{
	UWorld* World = GetWorld();
	if (!EventTag.IsValid() || !World || World->bIsTearingDown || World->GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}
	FOutlierEventDataRow Event;
	if (!TryGetEventData(EventTag, Event))
	{
		UE_LOG(LogOutlier, Warning, TEXT("[Event] No event row for '%s'."), *EventTag.ToString());
		return false;
	}

	bool bTTSAccepted = false;
	if (Event.TTSTag.IsValid())
	{
		FOutlierTTSDataRow TTS;
		if (TryGetTTSData(Event.TTSTag, TTS))
		{
			EnsureAudioCallbacks();
			if (UOutlierAudioSubsystem* Audio = BoundAudioSubsystem.Get())
			{
				bTTSAccepted = Audio->PlayLocalVoice(TTS.TTSTag, static_cast<int32>(TTS.Priority), EventTag, GetLocalPlayer());
			}
		}
		else
		{
			UE_LOG(LogOutlier, Warning, TEXT("[Event] '%s' references missing TTS '%s'."),
				*EventTag.ToString(), *Event.TTSTag.ToString());
		}
	}

	const FGameplayTag UpperTag = ResolveUpperTag(Event);
	bool bUpperAccepted = false;
	bool bGifAccepted = false;
	bool bHasCanvas = false;
	if (ULocalPlayerUISubSystem* UI = GetLocalPlayer()->GetSubsystem<ULocalPlayerUISubSystem>())
	{
		bHasCanvas = UI->GetInfoCanvas() != nullptr;
		if (UpperTag.IsValid()) bUpperAccepted = SubmitUpper(UpperTag, EventTag, Context);
		if (Event.GifTag.IsValid()) bGifAccepted = UI->ShowEventGif(EventTag, Event.GifTag);
	}
	// '-' unused, 'O' accepted, 'X' rejected. Without a canvas, accepted UI requests wait in the UI subsystem.
	auto Mark = [](bool bUsed, bool bOk) { return !bUsed ? TEXT("-") : bOk ? TEXT("O") : TEXT("X"); };
	UE_LOG(LogOutlier, Log, TEXT("[Event] '%s' TTS=%s Upper=%s Gif=%s Canvas=%s"), *EventTag.ToString(),
		Mark(Event.TTSTag.IsValid(), bTTSAccepted), Mark(UpperTag.IsValid(), bUpperAccepted),
		Mark(Event.GifTag.IsValid(), bGifAccepted), bHasCanvas ? TEXT("O") : TEXT("X"));
	return bTTSAccepted || bUpperAccepted || bGifAccepted;
}

bool UOutlierEventSubsystem::CompleteInputEvent(FGameplayTag EventTag)
{
	ULocalPlayerUISubSystem* UI = GetLocalPlayer()->GetSubsystem<ULocalPlayerUISubSystem>();
	return UI && UI->CompleteInputEvent(EventTag);
}

bool UOutlierEventSubsystem::CompleteUpperEvent(FGameplayTag EventTag)
{
	ULocalPlayerUISubSystem* UI = GetLocalPlayer()->GetSubsystem<ULocalPlayerUISubSystem>();
	return UI && UI->CompleteInfoEvent(EOutlierInfoRegion::Upper, EventTag);
}

bool UOutlierEventSubsystem::CancelUpperEvent(FGameplayTag EventTag)
{
	ULocalPlayerUISubSystem* UI = GetLocalPlayer()->GetSubsystem<ULocalPlayerUISubSystem>();
	return UI && UI->HideEventUpper(EventTag);
}

bool UOutlierEventSubsystem::CancelGifEvent(FGameplayTag EventTag)
{
	ULocalPlayerUISubSystem* UI = GetLocalPlayer()->GetSubsystem<ULocalPlayerUISubSystem>();
	return UI && UI->HideEventGif(EventTag);
}

bool UOutlierEventSubsystem::NotifyOverlap(FGameplayTag EventTag)
{
	ULocalPlayerUISubSystem* UI = GetLocalPlayer()->GetSubsystem<ULocalPlayerUISubSystem>();
	return UI && UI->NotifyInfoOverlap(EventTag);
}

void UOutlierEventSubsystem::CancelInputEvent()
{
	if (ULocalPlayerUISubSystem* UI = GetLocalPlayer()->GetSubsystem<ULocalPlayerUISubSystem>())
	{
		UI->CancelInputInfos();
	}
}

FGameplayTag UOutlierEventSubsystem::GetActiveInputEventTag() const
{
	ULocalPlayerUISubSystem* UI = GetLocalPlayer()->GetSubsystem<ULocalPlayerUISubSystem>();
	return UI ? UI->GetActiveInputEventTag() : FGameplayTag();
}

void UOutlierEventSubsystem::EnsureAudioCallbacks()
{
	UOutlierAudioSubsystem* Audio = GetLocalPlayer()->GetGameInstance()->GetSubsystem<UOutlierAudioSubsystem>();
	if (Audio == BoundAudioSubsystem.Get())
	{
		return;
	}
	BoundAudioSubsystem = Audio;
	if (Audio)
	{
		Audio->OnVoiceStarted.AddUObject(this, &ThisClass::HandleVoiceStarted);
		Audio->OnVoiceFinished.AddUObject(this, &ThisClass::HandleVoiceFinished);
		Audio->OnVoiceUnavailable.AddUObject(this, &ThisClass::HandleVoiceUnavailable);
	}
}

FGameplayTag UOutlierEventSubsystem::ResolveRequestTTSTag(FGameplayTag RequestTag)
{
	// SendTTS requests by TTSTag, SendEvent by EventTag.
	FOutlierTTSDataRow TTS;
	FOutlierEventDataRow Event;
	return TryGetTTSData(RequestTag, TTS) ? RequestTag
		: TryGetEventData(RequestTag, Event) ? Event.TTSTag : FGameplayTag();
}

void UOutlierEventSubsystem::HandleVoiceStarted(ULocalPlayer* Listener, FGameplayTag RequestTag)
{
	if (Listener != GetLocalPlayer())
	{
		return;
	}
	if (ULocalPlayerUISubSystem* UI = GetLocalPlayer()->GetSubsystem<ULocalPlayerUISubSystem>())
	{
		UI->ShowEventScript(RequestTag, ResolveRequestTTSTag(RequestTag));
	}
}

void UOutlierEventSubsystem::HandleVoiceUnavailable(ULocalPlayer* Listener, FGameplayTag RequestTag)
{
	ULocalPlayerUISubSystem* UI = Listener == GetLocalPlayer()
		? GetLocalPlayer()->GetSubsystem<ULocalPlayerUISubSystem>() : nullptr;
	const FGameplayTag TTSTag = UI ? ResolveRequestTTSTag(RequestTag) : FGameplayTag();
	FOutlierTTSDataRow TTS;
	if (!TTSTag.IsValid() || !TryGetTTSData(TTSTag, TTS))
	{
		return;
	}
	// Without a voice to follow, the subtitle stays long enough to read its script.
	int32 Characters = 0;
	for (const TCHAR Character : TTS.Script.ToString())
	{
		if (!FChar::IsWhitespace(Character)) ++Characters;
	}
	const UOutlierEventSettings* Settings = GetDefault<UOutlierEventSettings>();
	const float Duration = FMath::Clamp(Characters * Settings->FallbackSecondsPerCharacter,
		Settings->FallbackMinSeconds, FMath::Max(Settings->FallbackMinSeconds, Settings->FallbackMaxSeconds));
	UE_LOG(LogOutlier, Warning, TEXT("[Event] Voice '%s' unavailable for '%s'; subtitle shown for %.1fs (%d chars)."),
		*TTSTag.ToString(), *RequestTag.ToString(), Duration, Characters);
	UI->ShowEventScript(RequestTag, TTSTag, Duration);
}

void UOutlierEventSubsystem::HandleVoiceFinished(ULocalPlayer* Listener, FGameplayTag RequestTag, bool bCompleted)
{
	if (Listener == GetLocalPlayer())
	{
		if (ULocalPlayerUISubSystem* UI = GetLocalPlayer()->GetSubsystem<ULocalPlayerUISubSystem>())
		{
			UI->FinishEventScript(RequestTag, bCompleted);
		}
	}
}
