#include "Audio/OutlierAudioSubsystem.h"

#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Kismet/GameplayStatics.h"
#include "Outlier.h"
#include "Sound/SoundBase.h"

void UOutlierAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ReloadCatalog();
}

void UOutlierAudioSubsystem::Deinitialize()
{
	for (TPair<FSoftObjectPath, TSharedPtr<FStreamableHandle>>& Pair : ActiveLoadHandles)
	{
		if (Pair.Value.IsValid())
		{
			Pair.Value->CancelHandle();
		}
	}

	ActiveLoadHandles.Empty();
	PendingPlaysBySound.Empty();
	CatalogEntriesByEvent.Empty();
	LoadedDefinitions.Empty();

	Super::Deinitialize();
}

bool UOutlierAudioSubsystem::ReloadCatalog()
{
	CatalogEntriesByEvent.Empty();
	LoadedDefinitions.Empty();

	UAssetManager* AssetManager = UAssetManager::GetIfInitialized();
	if (!AssetManager)
	{
		UE_LOG(LogOutlier, Error, TEXT("[Audio] Asset Manager is not initialized."));
		return false;
	}

	TArray<FPrimaryAssetId> DefinitionIds;
	if (!AssetManager->GetPrimaryAssetIdList(
		UOutlierAudioEventDefinition::PrimaryAssetType,
		DefinitionIds))
	{
		UE_LOG(LogOutlier, Warning,
			TEXT("[Audio] No Audio Event Definitions were found under /Game/Audio/Definitions."));
		return false;
	}

	TMap<FGameplayTag, FName> DefinitionByEvent;
	int32 LoadedVariantCount = 0;

	for (const FPrimaryAssetId& DefinitionId : DefinitionIds)
	{
		const FSoftObjectPath DefinitionPath = AssetManager->GetPrimaryAssetPath(DefinitionId);
		UOutlierAudioEventDefinition* Definition =
			Cast<UOutlierAudioEventDefinition>(DefinitionPath.TryLoad());

		if (!Definition)
		{
			UE_LOG(LogOutlier, Error,
				TEXT("[Audio] Failed to load Audio Event Definition '%s'."),
				*DefinitionId.ToString());
			continue;
		}

		if (!Definition->EventTag.IsValid())
		{
			UE_LOG(LogOutlier, Error,
				TEXT("[Audio] Definition '%s' has an invalid EventTag and was skipped."),
				*GetNameSafe(Definition));
			continue;
		}

		if (const FName* ExistingDefinition = DefinitionByEvent.Find(Definition->EventTag))
		{
			UE_LOG(LogOutlier, Error,
				TEXT("[Audio] Definitions '%s' and '%s' both use EventTag '%s'. The latter was skipped."),
				*ExistingDefinition->ToString(),
				*GetNameSafe(Definition),
				*Definition->EventTag.ToString());
			continue;
		}

		DefinitionByEvent.Add(Definition->EventTag, Definition->GetFName());
		LoadedDefinitions.Add(Definition);

		for (int32 VariantIndex = 0; VariantIndex < Definition->Variants.Num(); ++VariantIndex)
		{
			const FOutlierAudioVariant& Variant = Definition->Variants[VariantIndex];
			if (Variant.Sound.IsNull() || Variant.Weight <= 0.0f)
			{
				UE_LOG(LogOutlier, Error,
					TEXT("[Audio] Definition '%s' variant %d is invalid and was skipped."),
					*GetNameSafe(Definition),
					VariantIndex);
				continue;
			}

			FRuntimeCatalogEntry Entry;
			Entry.SourceName = FString::Printf(
				TEXT("%s[%d]"),
				*GetNameSafe(Definition),
				VariantIndex);
			Entry.RequiredContextTags = Variant.RequiredContextTags;
			Entry.Sound = Variant.Sound;
			Entry.Weight = Variant.Weight;

			CatalogEntriesByEvent.FindOrAdd(Definition->EventTag).Add(MoveTemp(Entry));
			++LoadedVariantCount;
		}
	}

	UE_LOG(LogOutlier, Log,
		TEXT("[Audio] Loaded %d variants from %d Audio Event Definitions."),
		LoadedVariantCount,
		LoadedDefinitions.Num());

	return LoadedVariantCount > 0;
}

bool UOutlierAudioSubsystem::PlayAudio2D(const FOutlierAudioPlayRequest& Request)
{
	const FRuntimeCatalogEntry* Entry = ResolveBestEntry(Request.EventTag, Request.ContextTags);
	if (!Entry)
	{
		return false;
	}

	FPendingPlay PendingPlay;
	PendingPlay.World = GetWorld();
	PendingPlay.SourceName = Entry->SourceName;
	PendingPlay.VolumeMultiplier = Request.VolumeMultiplier;
	PendingPlay.PitchMultiplier = Request.PitchMultiplier;
	PendingPlay.StartTime = Request.StartTime;

	return QueueOrPlay(*Entry, PendingPlay);
}

bool UOutlierAudioSubsystem::PlayAudioAtLocation(
	const FOutlierAudioPlayRequest& Request,
	FVector Location)
{
	const FRuntimeCatalogEntry* Entry = ResolveBestEntry(Request.EventTag, Request.ContextTags);
	if (!Entry)
	{
		return false;
	}

	FPendingPlay PendingPlay;
	PendingPlay.World = GetWorld();
	PendingPlay.SourceName = Entry->SourceName;
	PendingPlay.bAtLocation = true;
	PendingPlay.Location = Location;
	PendingPlay.VolumeMultiplier = Request.VolumeMultiplier;
	PendingPlay.PitchMultiplier = Request.PitchMultiplier;
	PendingPlay.StartTime = Request.StartTime;

	return QueueOrPlay(*Entry, PendingPlay);
}

const UOutlierAudioSubsystem::FRuntimeCatalogEntry* UOutlierAudioSubsystem::ResolveBestEntry(
	FGameplayTag EventTag,
	const FGameplayTagContainer& ContextTags) const
{
	if (!EventTag.IsValid())
	{
		UE_LOG(LogOutlier, Warning, TEXT("[Audio] Play request contains an invalid EventTag."));
		return nullptr;
	}

	const TArray<FRuntimeCatalogEntry>* Candidates = CatalogEntriesByEvent.Find(EventTag);
	if (!Candidates)
	{
		UE_LOG(LogOutlier, Warning,
			TEXT("[Audio] No catalog rows exist for event '%s'."),
			*EventTag.ToString());
		return nullptr;
	}

	int32 BestSpecificity = INDEX_NONE;
	TArray<const FRuntimeCatalogEntry*> BestMatches;

	for (const FRuntimeCatalogEntry& Candidate : *Candidates)
	{
		if (Candidate.RequiredContextTags.Num() > 0
			&& !ContextTags.HasAll(Candidate.RequiredContextTags))
		{
			continue;
		}

		const int32 Specificity = Candidate.RequiredContextTags.Num();
		if (Specificity > BestSpecificity)
		{
			BestSpecificity = Specificity;
			BestMatches.Reset();
			BestMatches.Add(&Candidate);
		}
		else if (Specificity == BestSpecificity)
		{
			BestMatches.Add(&Candidate);
		}
	}

	if (BestMatches.IsEmpty())
	{
		UE_LOG(LogOutlier, Warning,
			TEXT("[Audio] Event '%s' has no row matching the supplied context tags."),
			*EventTag.ToString());
		return nullptr;
	}

	float TotalWeight = 0.0f;
	for (const FRuntimeCatalogEntry* Match : BestMatches)
	{
		TotalWeight += Match->Weight;
	}

	float Selection = FMath::FRandRange(0.0f, TotalWeight);
	for (const FRuntimeCatalogEntry* Match : BestMatches)
	{
		Selection -= Match->Weight;
		if (Selection <= 0.0f)
		{
			return Match;
		}
	}

	return BestMatches.Last();
}

bool UOutlierAudioSubsystem::QueueOrPlay(
	const FRuntimeCatalogEntry& Entry,
	const FPendingPlay& PendingPlay)
{
	if (!PendingPlay.World.IsValid())
	{
		return false;
	}

	if (USoundBase* LoadedSound = Entry.Sound.Get())
	{
		ExecutePlay(LoadedSound, PendingPlay);
		return true;
	}

	const FSoftObjectPath SoundPath = Entry.Sound.ToSoftObjectPath();
	if (!SoundPath.IsValid())
	{
		UE_LOG(LogOutlier, Error,
			TEXT("[Audio] Variant '%s' contains an invalid Sound asset path."),
			*Entry.SourceName);
		return false;
	}

	PendingPlaysBySound.FindOrAdd(SoundPath).Add(PendingPlay);
	if (ActiveLoadHandles.Contains(SoundPath))
	{
		return true;
	}

	TSharedPtr<FStreamableHandle> LoadHandle =
		UAssetManager::GetStreamableManager().RequestAsyncLoad(
			SoundPath,
			FStreamableDelegate::CreateUObject(
				this,
				&UOutlierAudioSubsystem::HandleSoundLoaded,
				SoundPath));

	if (!LoadHandle.IsValid())
	{
		PendingPlaysBySound.Remove(SoundPath);
		UE_LOG(LogOutlier, Error,
			TEXT("[Audio] Failed to start async load for '%s'."),
			*SoundPath.ToString());
		return false;
	}

	ActiveLoadHandles.Add(SoundPath, MoveTemp(LoadHandle));
	return true;
}

void UOutlierAudioSubsystem::HandleSoundLoaded(FSoftObjectPath SoundPath)
{
	TSharedPtr<FStreamableHandle> CompletedHandle;
	ActiveLoadHandles.RemoveAndCopyValue(SoundPath, CompletedHandle);

	TArray<FPendingPlay> PendingPlays;
	PendingPlaysBySound.RemoveAndCopyValue(SoundPath, PendingPlays);

	USoundBase* LoadedSound = Cast<USoundBase>(SoundPath.ResolveObject());
	if (!LoadedSound)
	{
		UE_LOG(LogOutlier, Error,
			TEXT("[Audio] Async load completed without a valid USoundBase for '%s'."),
			*SoundPath.ToString());
		return;
	}

	for (const FPendingPlay& PendingPlay : PendingPlays)
	{
		ExecutePlay(LoadedSound, PendingPlay);
	}
}

void UOutlierAudioSubsystem::ExecutePlay(
	USoundBase* Sound,
	const FPendingPlay& PendingPlay) const
{
	UWorld* World = PendingPlay.World.Get();
	if (!World || !Sound)
	{
		return;
	}

	if (PendingPlay.bAtLocation)
	{
		UGameplayStatics::PlaySoundAtLocation(
			World,
			Sound,
			PendingPlay.Location,
			PendingPlay.VolumeMultiplier,
			PendingPlay.PitchMultiplier,
			PendingPlay.StartTime);
		return;
	}

	UGameplayStatics::PlaySound2D(
		World,
		Sound,
		PendingPlay.VolumeMultiplier,
		PendingPlay.PitchMultiplier,
		PendingPlay.StartTime);
}
