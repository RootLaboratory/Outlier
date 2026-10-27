#include "Tutorial/WorldGuide/OutlierWorldGuideManager.h"

#include "Components/MeshComponent.h"
#include "Blueprint/UserWidget.h"
#include "Components/WidgetComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Event/OutlierEventDataAsset.h"
#include "Event/OutlierEventSettings.h"
#include "Net/UnrealNetwork.h"
#include "Outlier.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideTargetComponent.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideDebug.h"

AOutlierWorldGuideManager::AOutlierWorldGuideManager()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

AOutlierWorldGuideManager* AOutlierWorldGuideManager::Find(UWorld* World)
{
	if (!World || World->bIsTearingDown) return nullptr;
	AOutlierWorldGuideManager* Found = nullptr;
	for (TActorIterator<AOutlierWorldGuideManager> It(World); It; ++It)
	{
		if (!IsValid(*It)) continue;
		if (Found)
		{
			UE_LOG(LogOutlier, Error, TEXT("[WorldGuide] Place only one WorldGuideManager in the world."));
			return nullptr;
		}
		Found = *It;
	}
	return Found;
}

void AOutlierWorldGuideManager::BeginPlay()
{
	Super::BeginPlay();
	LoadTables();
	// Component는 레벨 Manager보다 먼저 BeginPlay되거나, 이후에 스트리밍될 수 있다.
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		TArray<UOutlierWorldGuideTargetComponent*> Components;
		It->GetComponents<UOutlierWorldGuideTargetComponent>(Components);
		for (UOutlierWorldGuideTargetComponent* Component : Components)
			if (Component->HasBegunPlay()) RegisterTarget(Component);
	}
	RefreshPresentation();
}

void AOutlierWorldGuideManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	if (HasAuthority()) ClearGuides();
	ActiveContext = FOutlierWorldGuideContext();
	RefreshPresentation();
	for (const auto& Pair : Targets)
		for (const TWeakObjectPtr<UOutlierWorldGuideTargetComponent>& Target : Pair.Value)
			if (Target.IsValid()) Target->SetGuideManager(nullptr);
	Targets.Reset();
	Super::EndPlay(EndPlayReason);
}

void AOutlierWorldGuideManager::LoadTables()
{
	if (GuideTable && !EventTables.IsEmpty()) return;
	TArray<UOutlierEventDataAsset*> Assets;
	const UOutlierEventSettings* Settings = GetDefault<UOutlierEventSettings>();
	UOutlierEventDataAsset* Shooter = EventDataAsset ? EventDataAsset.Get() : Settings->ShooterDataAsset.LoadSynchronous();
	UOutlierEventDataAsset* Partner = PartnerEventDataAsset ? PartnerEventDataAsset.Get() : Settings->PartnerDataAsset.LoadSynchronous();
	if (Shooter) Assets.AddUnique(Shooter);
	if (Partner) Assets.AddUnique(Partner);
	for (UOutlierEventDataAsset* Asset : Assets)
	{
		UDataTable* Table = Asset->WorldGuideTable.LoadSynchronous();
		if (Table && Table->GetRowStruct() == FOutlierWorldGuideDataRow::StaticStruct())
		{
			if (GuideTable && GuideTable != Table)
			{
				UE_LOG(LogOutlier, Error, TEXT("[WorldGuide] Role DAs must share one WorldGuide DT."));
				GuideTable = nullptr;
				EventTables.Reset();
				return;
			}
			GuideTable = Table;
		}
		if (UDataTable* Events = Asset->EventTable.LoadSynchronous())
			if (Events->GetRowStruct() == FOutlierEventDataRow::StaticStruct()) EventTables.AddUnique(Events);
	}
}

bool AOutlierWorldGuideManager::TryGetDefinition(FGameplayTag WidgetID, FOutlierWorldGuideDataRow& OutRow)
{
	LoadTables();
	if (!WidgetID.IsValid() || !GuideTable || GuideTable->GetRowStruct() != FOutlierWorldGuideDataRow::StaticStruct()) return false;
	const FOutlierWorldGuideDataRow* Row = GuideTable->FindRow<FOutlierWorldGuideDataRow>(WidgetID.GetTagName(), TEXT("WorldGuide"), false);
	if (!Row || Row->WidgetID != WidgetID || !Row->IsValid()) return false;
	OutRow = *Row;
	return true;
}

bool AOutlierWorldGuideManager::TriggerEvent(FGameplayTag EventTag)
{
	if (!HasAuthority() || !EventTag.IsValid() || bEndingPlay) return false;
	LoadTables();
	FGameplayTag WidgetID;
	for (UDataTable* Table : EventTables)
	{
		const FOutlierEventDataRow* Event = Table->FindRow<FOutlierEventDataRow>(EventTag.GetTagName(), TEXT("WorldGuideEvent"), false);
		if (!Event || Event->EventTag != EventTag || !Event->WorldGuideID.IsValid()) continue;
		if (WidgetID.IsValid() && WidgetID != Event->WorldGuideID)
		{
			UE_LOG(LogOutlier, Error, TEXT("[WorldGuide] Event '%s' selects different shared guides in the role DAs."), *EventTag.ToString());
			return false;
		}
		WidgetID = Event->WorldGuideID;
	}
	FOutlierWorldGuideContext Context;
	Context.WidgetID = WidgetID;
	Context.OriginEventTag = EventTag;
	OutlierWorldGuideDebug::Trace(GetWorld(), FString::Printf(TEXT("EVENT %s -> Guide=%s Completed=%d"),
		*EventTag.ToString(), *WidgetID.ToString(), CompletedWidgetIDs.Contains(WidgetID)));
	// 이미 달성한 안내도 Queue를 거쳐 표시 없이 완료되고 후속 체인으로 이어진다.
	return EnqueueGuide(Context);
}

bool AOutlierWorldGuideManager::EnqueueGuide(FOutlierWorldGuideContext Context)
{
	if (!HasAuthority() || bEndingPlay || bClearingQueue) return false;
	FOutlierWorldGuideDataRow Row;
	if (!TryGetDefinition(Context.WidgetID, Row))
	{
		OutlierWorldGuideDebug::Trace(GetWorld(), FString::Printf(TEXT("REJECT ID=%s: missing/invalid WorldGuide DT definition."), *Context.WidgetID.ToString()));
		return false;
	}
	if (Context.VisitedWidgetIDs.Contains(Context.WidgetID))
	{
		UE_LOG(LogOutlier, Warning, TEXT("[WorldGuide] Cyclic chain to '%s' rejected."), *Context.WidgetID.ToString());
		return false;
	}
	const auto SameRequest = [&Context](const FOutlierWorldGuideContext& Existing)
	{
		// 두 역할과 게임플레이 트리거가 같은 공유 목표를 제출할 수 있다. 하나만 유지한다.
		return Existing.WidgetID == Context.WidgetID;
	};
	if ((ActiveContext.RequestID != 0 && SameRequest(ActiveContext)) || PendingContexts.ContainsByPredicate(SameRequest))
	{
		OutlierWorldGuideDebug::Trace(GetWorld(), FString::Printf(TEXT("DEDUP ID=%s already active/pending."), *Context.WidgetID.ToString()));
		return true;
	}
	// 뒤 단계가 먼저 진행됐으면 늦게 온 앞 단계는 표시하지 않고 달성으로만 기록한다.
	if (IsPassedByLaterGuide(Context.WidgetID))
	{
		CompletedWidgetIDs.Add(Context.WidgetID);
		OutlierWorldGuideDebug::Trace(GetWorld(), FString::Printf(TEXT("PASSED ID=%s: a later chain step is already reached."), *Context.WidgetID.ToString()));
		return true;
	}
	// 앞 단계를 건너뛰고 뒤 단계가 들어오면 앞 단계가 Queue를 막지 않도록 지나간 것으로 처리한다.
	SkipGuidesBefore(Context.WidgetID);
	// 이미 달성한 목표는 건너뛰며, 설정된 후속 목표는 계속 진행될 수 있다.
	Context.EndCondition = Row.EndCondition;
	if (Row.TriggerType == EOutlierEventTriggerType::CHAIN && !Context.NextWidgetID.IsValid())
		if (const FOutlierWorldGuideContext* Template = ChainContexts.FindByPredicate(
			[&Context](const FOutlierWorldGuideContext& Item) { return Item.WidgetID == Context.WidgetID; }))
			Context.NextWidgetID = Template->NextWidgetID;
	Context.VisitedWidgetIDs.Add(Context.WidgetID);
	Context.RequestID = NextRequestID++;
	PendingContexts.Add(Context);
	OutlierWorldGuideDebug::Trace(GetWorld(), FString::Printf(TEXT("QUEUE Request=%d ID=%s Origin=%s Next=%s Pending=%d"),
		Context.RequestID, *Context.WidgetID.ToString(), *Context.OriginEventTag.ToString(), *Context.NextWidgetID.ToString(), PendingContexts.Num()));
	AdvanceQueue();
	return true;
}

void AOutlierWorldGuideManager::AdvanceQueue()
{
	if (bChangingQueue || bEndingPlay || ActiveContext.RequestID != 0) return;
	TGuardValue<bool> ChangingGuard(bChangingQueue, true);
	while (ActiveContext.RequestID == 0 && !PendingContexts.IsEmpty() && !bEndingPlay)
	{
		const FOutlierWorldGuideContext Next = PendingContexts[0];
		PendingContexts.RemoveAt(0);
		if (CompletedWidgetIDs.Contains(Next.WidgetID))
		{
			FinishContext(Next, true);
			continue;
		}
		ActiveContext = Next;
		OutlierWorldGuideDebug::Trace(GetWorld(), FString::Printf(TEXT("ACTIVE Request=%d ID=%s Targets=%d Pending=%d"),
			Next.RequestID, *Next.WidgetID.ToString(), GetTargetCount(Next.WidgetID), PendingContexts.Num()));
		RefreshPresentation();
		ForceNetUpdate();
	}
}

void AOutlierWorldGuideManager::FinishContext(const FOutlierWorldGuideContext& Context, bool bCompleted, FGameplayTag NextWidgetID)
{
	OutlierWorldGuideDebug::Trace(GetWorld(), FString::Printf(TEXT("%s Request=%d ID=%s Next=%s"),
		bCompleted ? TEXT("COMPLETE") : TEXT("CANCEL"), Context.RequestID, *Context.WidgetID.ToString(),
		*(NextWidgetID.IsValid() ? NextWidgetID : Context.NextWidgetID).ToString()));
	if (bCompleted)
	{
		CompletedWidgetIDs.Add(Context.WidgetID);
		const FGameplayTag Successor = NextWidgetID.IsValid() ? NextWidgetID : Context.NextWidgetID;
		if (Successor.IsValid() && !bEndingPlay)
		{
			FOutlierWorldGuideContext Next;
			Next.WidgetID = Successor;
			Next.OriginEventTag = Context.OriginEventTag;
			Next.VisitedWidgetIDs = Context.VisitedWidgetIDs;
			EnqueueGuide(Next);
		}
	}
	OnGuideFinished.Broadcast(Context, bCompleted);
}

bool AOutlierWorldGuideManager::FinishMatching(FGameplayTag Tag, bool bByEvent, bool bCompleted, bool bOverlapOnly, FGameplayTag NextWidgetID)
{
	if (!HasAuthority() || !Tag.IsValid() || bEndingPlay || bChangingQueue) return false;
	const auto Matches = [Tag, bByEvent, bOverlapOnly](const FOutlierWorldGuideContext& Context)
	{
		return (bByEvent ? Context.OriginEventTag == Tag : Context.WidgetID == Tag)
			&& (!bOverlapOnly || Context.EndCondition == EOutlierEventEndCondition::Overlap);
	};
	TArray<FOutlierWorldGuideContext> Finished;
	{
		TGuardValue<bool> ChangingGuard(bChangingQueue, true);
		// 콜백/Chain 생성 전에 매칭을 확보한다. 새로 Enqueue된 후속 목표는 완료하지 않는다.
		for (int32 Index = PendingContexts.Num() - 1; Index >= 0; --Index)
		{
			if (Matches(PendingContexts[Index]))
			{
				Finished.Insert(PendingContexts[Index], 0);
				PendingContexts.RemoveAt(Index);
			}
		}
		if (ActiveContext.RequestID != 0 && Matches(ActiveContext))
		{
			Finished.Insert(ActiveContext, 0);
			ActiveContext = FOutlierWorldGuideContext();
			RefreshPresentation();
			ForceNetUpdate();
		}
		for (const FOutlierWorldGuideContext& Context : Finished) FinishContext(Context, bCompleted, NextWidgetID);
	}
	AdvanceQueue();
	return !Finished.IsEmpty();
}

bool AOutlierWorldGuideManager::CompleteWidget(FGameplayTag WidgetID, FGameplayTag NextWidgetID)
{
	// 앞 단계를 건너뛰고 이 단계를 달성했으면 앞 단계가 활성으로 남아 Queue를 막지 않게 한다.
	if (HasAuthority() && !bEndingPlay && !bChangingQueue && WidgetID.IsValid()) SkipGuidesBefore(WidgetID);
	if (FinishMatching(WidgetID, false, true, false, NextWidgetID)) return true;
	// 가이드가 Queue에 들어가기 전에 상호작용이 일어날 수 있다. Chain 시작점을 만들지 않고 달성만 기록한다.
	FOutlierWorldGuideDataRow Row;
	if (!HasAuthority() || bEndingPlay || bChangingQueue || CompletedWidgetIDs.Contains(WidgetID) || !TryGetDefinition(WidgetID, Row))
	{
		AdvanceQueue();
		return false;
	}
	CompletedWidgetIDs.Add(WidgetID);
	AdvanceQueue();
	return true;
}

bool AOutlierWorldGuideManager::CompleteTarget(UOutlierWorldGuideTargetComponent* Target, FGameplayTag NextWidgetID)
{
	if (!HasAuthority() || bEndingPlay || !IsValid(Target)) return false;
	const FGameplayTag WidgetID = Target->WidgetID;
	FOutlierWorldGuideDataRow Row;
	if (!TryGetDefinition(WidgetID, Row) || Row.RequiredTargetCount <= 1) return CompleteWidget(WidgetID, NextWidgetID);
	TSet<FString>& Done = CompletedTargets.FindOrAdd(WidgetID);
	bool bAlreadyDone = false;
	Done.Add(Target->GetPathName(), &bAlreadyDone);
	if (bAlreadyDone) return false;
	// 그룹이 아직 Queue에 없어도 기록한다. 나중에 활성화되면 남은 타깃만 표시된다.
	Target->SetTargetCompleted(true);
	OutlierWorldGuideDebug::Trace(GetWorld(), FString::Printf(TEXT("TARGET DONE %s ID=%s %d/%d"),
		*Target->GetPathName(), *WidgetID.ToString(), Done.Num(), Row.RequiredTargetCount));
	return Done.Num() < Row.RequiredTargetCount || CompleteWidget(WidgetID, NextWidgetID);
}

bool AOutlierWorldGuideManager::IsChainAncestor(FGameplayTag Ancestor, FGameplayTag Descendant) const
{
	if (!Ancestor.IsValid() || !Descendant.IsValid() || Ancestor == Descendant) return false;
	FGameplayTag Current = Ancestor;
	// 순환 설정이 있어도 끝나도록 체인 길이만큼만 따라간다.
	for (int32 Step = 0; Step <= ChainContexts.Num(); ++Step)
	{
		const FOutlierWorldGuideContext* Link = ChainContexts.FindByPredicate(
			[&Current](const FOutlierWorldGuideContext& Item) { return Item.WidgetID == Current; });
		if (!Link || !Link->NextWidgetID.IsValid()) return false;
		if (Link->NextWidgetID == Descendant) return true;
		Current = Link->NextWidgetID;
	}
	return false;
}

bool AOutlierWorldGuideManager::IsPassedByLaterGuide(FGameplayTag WidgetID) const
{
	const auto IsLater = [this, WidgetID](FGameplayTag Other) { return IsChainAncestor(WidgetID, Other); };
	if (ActiveContext.RequestID != 0 && IsLater(ActiveContext.WidgetID)) return true;
	if (PendingContexts.ContainsByPredicate([&IsLater](const FOutlierWorldGuideContext& Item) { return IsLater(Item.WidgetID); })) return true;
	for (const FGameplayTag& Completed : CompletedWidgetIDs)
		if (IsLater(Completed)) return true;
	return false;
}

void AOutlierWorldGuideManager::SkipGuidesBefore(FGameplayTag Reached)
{
	TArray<FOutlierWorldGuideContext> Passed;
	for (int32 Index = PendingContexts.Num() - 1; Index >= 0; --Index)
	{
		if (IsChainAncestor(PendingContexts[Index].WidgetID, Reached))
		{
			Passed.Insert(PendingContexts[Index], 0);
			PendingContexts.RemoveAt(Index);
		}
	}
	if (ActiveContext.RequestID != 0 && IsChainAncestor(ActiveContext.WidgetID, Reached))
	{
		Passed.Insert(ActiveContext, 0);
		ActiveContext = FOutlierWorldGuideContext();
		RefreshPresentation();
		ForceNetUpdate();
	}
	for (const FOutlierWorldGuideContext& Context : Passed)
	{
		// 지나간 단계와 Reached 사이의 단계도 다시 표시되지 않도록 함께 달성으로 기록한다. 후속 체인은 만들지 않는다.
		for (FGameplayTag Step = Context.WidgetID; Step.IsValid() && Step != Reached;)
		{
			CompletedWidgetIDs.Add(Step);
			const FOutlierWorldGuideContext* Link = ChainContexts.FindByPredicate(
				[&Step](const FOutlierWorldGuideContext& Item) { return Item.WidgetID == Step; });
			Step = Link ? Link->NextWidgetID : FGameplayTag();
			if (CompletedWidgetIDs.Contains(Step)) break;
		}
		OutlierWorldGuideDebug::Trace(GetWorld(), FString::Printf(TEXT("SKIP Request=%d ID=%s: passed by %s"),
			Context.RequestID, *Context.WidgetID.ToString(), *Reached.ToString()));
		OnGuideFinished.Broadcast(Context, true);
	}
	if (!Passed.IsEmpty())
		UE_LOG(LogOutlier, Log, TEXT("[WorldGuide] %d earlier guide(s) passed by '%s'."), Passed.Num(), *Reached.ToString());
}

bool AOutlierWorldGuideManager::NotifyWidgetOverlap(FGameplayTag WidgetID, FGameplayTag NextWidgetID)
{
	FOutlierWorldGuideDataRow Row;
	return TryGetDefinition(WidgetID, Row) && Row.EndCondition == EOutlierEventEndCondition::Overlap
		&& CompleteWidget(WidgetID, NextWidgetID);
}

bool AOutlierWorldGuideManager::CancelWidget(FGameplayTag WidgetID) { return FinishMatching(WidgetID, false, false, false); }
bool AOutlierWorldGuideManager::CompleteEvent(FGameplayTag EventTag) { return FinishMatching(EventTag, true, true, false); }
bool AOutlierWorldGuideManager::CancelEvent(FGameplayTag EventTag) { return FinishMatching(EventTag, true, false, false); }
bool AOutlierWorldGuideManager::NotifyEventOverlap(FGameplayTag EventTag) { return FinishMatching(EventTag, true, true, true); }

void AOutlierWorldGuideManager::ClearGuides(bool bResetCompleted)
{
	if (!HasAuthority() || bChangingQueue) return;
	TGuardValue<bool> ClearingGuard(bClearingQueue, true);
	TGuardValue<bool> ChangingGuard(bChangingQueue, true);
	TArray<FOutlierWorldGuideContext> Cancelled = PendingContexts;
	if (ActiveContext.RequestID != 0) Cancelled.Insert(ActiveContext, 0);
	PendingContexts.Reset();
	ActiveContext = FOutlierWorldGuideContext();
	if (bResetCompleted)
	{
		CompletedWidgetIDs.Reset();
		CompletedTargets.Reset();
		for (const auto& Pair : Targets)
			for (const TWeakObjectPtr<UOutlierWorldGuideTargetComponent>& Target : Pair.Value)
				if (Target.IsValid()) Target->SetTargetCompleted(false);
	}
	RefreshPresentation();
	ForceNetUpdate();
	for (const FOutlierWorldGuideContext& Context : Cancelled) OnGuideFinished.Broadcast(Context, false);
}

void AOutlierWorldGuideManager::RegisterTarget(UOutlierWorldGuideTargetComponent* Target)
{
	if (!IsValid(Target) || !Target->WidgetID.IsValid() || bEndingPlay) return;
	Targets.FindOrAdd(Target->WidgetID).AddUnique(Target);
	OutlierWorldGuideDebug::Trace(GetWorld(), FString::Printf(TEXT("REGISTER %s ID=%s Class=%s TargetsForID=%d"),
		*Target->GetPathName(), *Target->WidgetID.ToString(), *GetPathNameSafe(Target->WidgetClass.Get()), GetTargetCount(Target->WidgetID)));
	Target->SetGuideManager(this);
	if (HasAuthority())
		if (const TSet<FString>* Done = CompletedTargets.Find(Target->WidgetID))
			if (Done->Contains(Target->GetPathName())) Target->SetTargetCompleted(true);
	RefreshTarget(Target);
}

void AOutlierWorldGuideManager::RefreshTarget(UOutlierWorldGuideTargetComponent* Target)
{
	if (!IsValid(Target)) return;
	if (GetNetMode() != NM_DedicatedServer && !bEndingPlay && ActiveContext.RequestID != 0
		&& ActiveContext.WidgetID == Target->WidgetID && !Target->IsTargetCompleted())
	{
		AddOutline(Target);
		Target->ApplyGuide(ActiveContext);
		return;
	}
	RemoveOutline(Target);
	Target->ClearGuide();
}

void AOutlierWorldGuideManager::UnregisterTarget(UOutlierWorldGuideTargetComponent* Target)
{
	if (!Target) return;
	RemoveOutline(Target);
	Target->ClearGuide();
	Target->SetGuideManager(nullptr);
	for (auto& Pair : Targets) Pair.Value.RemoveAll([Target](const auto& Item) { return !Item.IsValid() || Item.Get() == Target; });
}

int32 AOutlierWorldGuideManager::GetTargetCount(FGameplayTag WidgetID) const
{
	const auto* Items = Targets.Find(WidgetID);
	return Items ? Items->FilterByPredicate([](const auto& Item) { return Item.IsValid(); }).Num() : 0;
}

void AOutlierWorldGuideManager::RefreshPresentation()
{
	TArray<TWeakObjectPtr<UOutlierWorldGuideTargetComponent>> PreviouslyOutlined;
	OutlinedTargets.GetKeys(PreviouslyOutlined);
	for (const auto& Target : PreviouslyOutlined)
	{
		RemoveOutline(Target.Get());
		if (Target.IsValid()) Target->ClearGuide();
	}
	if (GetNetMode() == NM_DedicatedServer || bEndingPlay || ActiveContext.RequestID == 0) return;
	if (const auto* Registered = Targets.Find(ActiveContext.WidgetID))
	{
		const auto Snapshot = *Registered;
		for (const auto& Target : Snapshot)
		{
			if (Target.IsValid() && Target->WidgetID == ActiveContext.WidgetID && ActiveContext.RequestID != 0
				&& !Target->IsTargetCompleted())
			{
				AddOutline(Target.Get());
				Target->ApplyGuide(ActiveContext);
			}
		}
	}
}

void AOutlierWorldGuideManager::AddOutline(UOutlierWorldGuideTargetComponent* Target)
{
	if (!Target || !Target->GetOwner() || OutlinedTargets.Contains(Target)) return;
	TArray<UMeshComponent*> Meshes;
	Target->GetOwner()->GetComponents<UMeshComponent>(Meshes);
	auto& Used = OutlinedTargets.Add(Target);
	for (UMeshComponent* Mesh : Meshes)
	{
		if (!IsValid(Mesh) || Mesh->IsA<UWidgetComponent>()) continue;
		FOutlierGuideStencilState& State = StencilStates.FindOrAdd(Mesh);
		if (State.Users++ == 0)
		{
			State.bOriginalCustomDepth = Mesh->bRenderCustomDepth;
			State.OriginalStencil = Mesh->CustomDepthStencilValue;
			State.AppliedStencil = FMath::Clamp(OutlineStencilValue, 1, 255);
			Mesh->SetRenderCustomDepth(true);
			Mesh->SetCustomDepthStencilValue(State.AppliedStencil);
		}
		Used.Add(Mesh);
	}
}

void AOutlierWorldGuideManager::RemoveOutline(UOutlierWorldGuideTargetComponent* Target)
{
	if (!Target) return;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> Used;
	if (!OutlinedTargets.RemoveAndCopyValue(Target, Used)) return;
	for (const auto& MeshKey : Used)
	{
		FOutlierGuideStencilState* State = StencilStates.Find(MeshKey);
		if (!State || --State->Users > 0) continue;
		if (UPrimitiveComponent* Mesh = MeshKey.Get())
		{
			// 가이드 활성 중 다른 효과가 설정한 Stencil은 덮어쓰지 않는다.
			if (Mesh->CustomDepthStencilValue == State->AppliedStencil)
			{
				Mesh->SetCustomDepthStencilValue(State->OriginalStencil);
				Mesh->SetRenderCustomDepth(State->bOriginalCustomDepth);
			}
		}
		StencilStates.Remove(MeshKey);
	}
}

void AOutlierWorldGuideManager::OnRep_ActiveContext()
{
	OutlierWorldGuideDebug::Trace(GetWorld(), FString::Printf(TEXT("REPLICATED Request=%d ID=%s Targets=%d"),
		ActiveContext.RequestID, *ActiveContext.WidgetID.ToString(), GetTargetCount(ActiveContext.WidgetID)));
	RefreshPresentation();
}

void AOutlierWorldGuideManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AOutlierWorldGuideManager, ActiveContext);
}
