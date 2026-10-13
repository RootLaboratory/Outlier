#include "Tutorial/WorldGuide/OutlierWorldGuideManager.h"

#include "Components/MeshComponent.h"
#include "Blueprint/UserWidget.h"
#include "Components/WidgetComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Event/OutlierEventDataAsset.h"
#include "Event/OutlierEventSettings.h"
#include "HAL/IConsoleManager.h"
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
	// Components can begin play before their level manager, or stream in after it.
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
	if (CompletedWidgetIDs.Contains(WidgetID)) return true;
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
		// Both roles and a gameplay trigger may submit the same shared goal. Keep one occurrence.
		return Existing.WidgetID == Context.WidgetID;
	};
	if ((ActiveContext.RequestID != 0 && SameRequest(ActiveContext)) || PendingContexts.ContainsByPredicate(SameRequest))
	{
		OutlierWorldGuideDebug::Trace(GetWorld(), FString::Printf(TEXT("DEDUP ID=%s already active/pending."), *Context.WidgetID.ToString()));
		return true;
	}
	// Already achieved goals are skipped, and their configured successors can still advance.
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
		// Capture matches before callbacks/chain creation; never finish a newly enqueued successor.
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
	if (FinishMatching(WidgetID, false, true, false, NextWidgetID)) return true;
	// Interaction can occur before the guide is queued. Remember achievement, without inventing a chain origin.
	FOutlierWorldGuideDataRow Row;
	if (!HasAuthority() || bEndingPlay || bChangingQueue || CompletedWidgetIDs.Contains(WidgetID) || !TryGetDefinition(WidgetID, Row)) return false;
	CompletedWidgetIDs.Add(WidgetID);
	return true;
}

bool AOutlierWorldGuideManager::NotifyWidgetOverlap(FGameplayTag WidgetID)
{
	FOutlierWorldGuideDataRow Row;
	return TryGetDefinition(WidgetID, Row) && Row.EndCondition == EOutlierEventEndCondition::Overlap && CompleteWidget(WidgetID);
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
	if (bResetCompleted) CompletedWidgetIDs.Reset();
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
	if (ActiveContext.RequestID != 0 && ActiveContext.WidgetID == Target->WidgetID && GetNetMode() != NM_DedicatedServer)
	{
		AddOutline(Target);
		Target->ApplyGuide(ActiveContext);
	}
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
			if (Target.IsValid() && Target->WidgetID == ActiveContext.WidgetID && ActiveContext.RequestID != 0)
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
			// Do not overwrite a different stencil installed by another effect while the guide was active.
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

FString AOutlierWorldGuideManager::BuildDebugReport()
{
	LoadTables();
	FString Report = FString::Printf(TEXT("World=%s Net=%d Manager=%s Authority=%d\nDT=%s Rows=%d EventTables=%d OutlineStencil=%d\n"),
		*GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), *GetPathName(), HasAuthority(),
		*GetPathNameSafe(GuideTable), GuideTable ? GuideTable->GetRowNames().Num() : 0, EventTables.Num(), OutlineStencilValue);
	int32 Issues = 0;
	const auto Issue = [&Report, &Issues](const FString& Message) { ++Issues; Report += TEXT("  ISSUE: ") + Message + TEXT("\n"); };
	if (!GuideTable) Issue(TEXT("No WorldGuide DT loaded. Check the existing role DAs' WorldGuideTable."));
	if (EventTables.IsEmpty()) Issue(TEXT("No Event DT loaded. EventID -> WorldGuideID cannot resolve."));
	if (const IConsoleVariable* CustomDepth = IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepth")))
	{
		Report += FString::Printf(TEXT("r.CustomDepth=%d\n"), CustomDepth->GetInt());
		if (CustomDepth->GetInt() != 3) Issue(TEXT("Stencil outline needs r.CustomDepth=3 (Enabled with Stencil)."));
	}
	Report += FString::Printf(TEXT("Active Request=%d ID=%s Origin=%s End=%s Next=%s Targets=%d\n"),
		ActiveContext.RequestID, *ActiveContext.WidgetID.ToString(), *ActiveContext.OriginEventTag.ToString(),
		*StaticEnum<EOutlierEventEndCondition>()->GetNameStringByValue(static_cast<int64>(ActiveContext.EndCondition)),
		*ActiveContext.NextWidgetID.ToString(), GetTargetCount(ActiveContext.WidgetID));
	if (ActiveContext.RequestID != 0 && GetTargetCount(ActiveContext.WidgetID) == 0)
		Issue(TEXT("Active ID has no registered target in this world. Check WidgetID, BeginPlay and loaded level."));
	if (HasAuthority())
	{
		Report += FString::Printf(TEXT("Pending=%d Completed=%d\n"), PendingContexts.Num(), CompletedWidgetIDs.Num());
		for (int32 Index = 0; Index < PendingContexts.Num(); ++Index)
		{
			const auto& Context = PendingContexts[Index];
			Report += FString::Printf(TEXT("  Queue[%d] Request=%d ID=%s Origin=%s Next=%s\n"), Index,
				Context.RequestID, *Context.WidgetID.ToString(), *Context.OriginEventTag.ToString(), *Context.NextWidgetID.ToString());
		}
		for (const auto& ID : CompletedWidgetIDs) Report += TEXT("  Completed: ") + ID.ToString() + TEXT("\n");
	}
	else Report += TEXT("Pending/Completed live only on the server; use Outlier.WorldGuide.Dump server.\n");
	Report += FString::Printf(TEXT("ChainContexts=%d\n"), ChainContexts.Num());
	for (const auto& Context : ChainContexts)
	{
		Report += FString::Printf(TEXT("  Chain: %s -> %s\n"), *Context.WidgetID.ToString(), *Context.NextWidgetID.ToString());
		FOutlierWorldGuideDataRow Row;
		if (!TryGetDefinition(Context.WidgetID, Row) || (Context.NextWidgetID.IsValid() && !TryGetDefinition(Context.NextWidgetID, Row)))
			Issue(TEXT("Chain references a missing/invalid DT definition: ") + Context.WidgetID.ToString());
	}
	int32 Configured = 0, Unconfigured = 0;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		TArray<UOutlierWorldGuideTargetComponent*> Components;
		It->GetComponents(Components);
		for (UOutlierWorldGuideTargetComponent* Target : Components)
		{
			if (!Target->WidgetID.IsValid()) { ++Unconfigured; continue; }
			++Configured;
			const auto* Registered = Targets.Find(Target->WidgetID);
			const bool bRegistered = Registered && Registered->Contains(TWeakObjectPtr<UOutlierWorldGuideTargetComponent>(Target));
			Report += FString::Printf(TEXT("  Registered=%d %s\n"), bRegistered, *Target->DescribeDebugState());
			FOutlierWorldGuideDataRow Row;
			if (!TryGetDefinition(Target->WidgetID, Row)) Issue(TEXT("Target ID has no valid DT row: ") + Target->GetPathName());
			if (!bRegistered) Issue(TEXT("Configured target is not registered with this manager: ") + Target->GetPathName());
			if (!Target->WidgetClass) Issue(TEXT("WidgetClass is empty: ") + Target->GetPathName());
			else if (Target->WidgetClass->HasAnyClassFlags(CLASS_Abstract)) Issue(TEXT("WidgetClass is abstract: ") + Target->GetPathName());
			if (Target->DrawSize.X <= 0.0 || Target->DrawSize.Y <= 0.0) Issue(TEXT("DrawSize must be positive: ") + Target->GetPathName());
			if (Target->IsGuideVisible() && It->IsHidden()) Issue(TEXT("Active target owner is hidden: ") + It->GetPathName());
			TArray<UMeshComponent*> Meshes;
			It->GetComponents(Meshes);
			int32 OwnerMeshes = 0;
			for (const auto* Mesh : Meshes)
			{
				if (!IsValid(Mesh) || Mesh->IsA<UWidgetComponent>()) continue;
				++OwnerMeshes;
				Report += FString::Printf(TEXT("    Mesh=%s CustomDepth=%d Stencil=%d Visible=%d\n"),
					*Mesh->GetName(), Mesh->bRenderCustomDepth, Mesh->CustomDepthStencilValue, Mesh->IsVisible());
			}
			if (OwnerMeshes == 0) Report += TEXT("    No owner mesh: marker only, no mesh outline.\n");
		}
	}
	Report += FString::Printf(TEXT("Summary: ConfiguredTargets=%d EmptyIDs=%d Issues=%d\n"), Configured, Unconfigured, Issues);
	return Report;
}

void AOutlierWorldGuideManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AOutlierWorldGuideManager, ActiveContext);
}
