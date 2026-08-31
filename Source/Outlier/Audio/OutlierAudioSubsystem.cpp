#include "Audio/OutlierAudioSubsystem.h"

#include "Components/AudioComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Outlier.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

namespace
{
	const TCHAR* PlaybackModeName(EOutlierAudioPlaybackMode PlaybackMode)
	{
		switch (PlaybackMode)
		{
		case EOutlierAudioPlaybackMode::TwoD:
			return TEXT("TwoD");
		case EOutlierAudioPlaybackMode::AtLocation:
			return TEXT("AtLocation");
		default:
			return TEXT("Unknown");
		}
	}

	const TCHAR* AudienceName(EOutlierAudioAudience Audience)
	{
		switch (Audience)
		{
		case EOutlierAudioAudience::Local:
			return TEXT("Local");
		case EOutlierAudioAudience::Owner:
			return TEXT("Owner");
		case EOutlierAudioAudience::Relevant:
			return TEXT("Relevant");
		default:
			return TEXT("Unknown");
		}
	}

	const TCHAR* RequestAuthorityName(EOutlierAudioRequestAuthority RequestAuthority)
	{
		switch (RequestAuthority)
		{
		case EOutlierAudioRequestAuthority::Local:
			return TEXT("Local");
		case EOutlierAudioRequestAuthority::OwningClient:
			return TEXT("OwningClient");
		case EOutlierAudioRequestAuthority::Server:
			return TEXT("Server");
		default:
			return TEXT("Unknown");
		}
	}
}

void UOutlierAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	VolumeMultipliers.Add(EOutlierAudioVolumeType::Master, 1.0f);
	VolumeMultipliers.Add(EOutlierAudioVolumeType::BGM, 1.0f);
	VolumeMultipliers.Add(EOutlierAudioVolumeType::SFX, 1.0f);
	VolumeMultipliers.Add(EOutlierAudioVolumeType::Voice, 1.0f);
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
	VolumeMultipliers.Empty();
	ActiveAudioPlaybacks.Empty();
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

		if (Definition->VolumeMultiplier < 0.0f
			|| Definition->PitchMultiplier < 0.0f)
		{
			UE_LOG(LogOutlier, Error,
				TEXT("[Audio] Definition '%s' has invalid multipliers and was skipped."),
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
			Entry.VariantIndex = VariantIndex;
			Entry.Weight = Variant.Weight;
			Entry.VolumeMultiplier = Definition->VolumeMultiplier;
			Entry.PitchMultiplier = Definition->PitchMultiplier;
			Entry.VolumeType = Definition->VolumeType;

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

bool UOutlierAudioSubsystem::PlayLocal2D(const FOutlierAudioPlayRequest& Request)
{
	return PlayAudio(Request, {
		EOutlierAudioPlaybackMode::TwoD,
		EOutlierAudioAudience::Local,
		EOutlierAudioRequestAuthority::Local });
}

bool UOutlierAudioSubsystem::PlayOwner2DFromServer(const FOutlierAudioPlayRequest& Request)
{
	return PlayAudio(Request, {
		EOutlierAudioPlaybackMode::TwoD,
		EOutlierAudioAudience::Owner,
		EOutlierAudioRequestAuthority::Server });
}

bool UOutlierAudioSubsystem::PlayRelevantAtLocationFromOwningClient(
	const FOutlierAudioPlayRequest& Request)
{
	return PlayAudio(Request, {
		EOutlierAudioPlaybackMode::AtLocation,
		EOutlierAudioAudience::Relevant,
		EOutlierAudioRequestAuthority::OwningClient });
}

bool UOutlierAudioSubsystem::PlayRelevantAtLocationFromServer(
	const FOutlierAudioPlayRequest& Request)
{
	return PlayAudio(Request, {
		EOutlierAudioPlaybackMode::AtLocation,
		EOutlierAudioAudience::Relevant,
		EOutlierAudioRequestAuthority::Server });
}

bool UOutlierAudioSubsystem::PlayAudio(
	const FOutlierAudioPlayRequest& Request,
	const FOutlierAudioExecutionPolicy& Policy)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	if (Policy.RequestAuthority == EOutlierAudioRequestAuthority::Local
		&& Policy.Audience != EOutlierAudioAudience::Local)
	{
		UE_LOG(LogOutlier, Error,
			TEXT("[Audio] Local request authority only supports a Local audience. Event='%s'."),
			*Request.EventTag.ToString());
		return false;
	}

	if (Policy.RequestAuthority == EOutlierAudioRequestAuthority::Server
		&& World->GetNetMode() == NM_Client)
	{
		UE_LOG(LogOutlier, Warning,
			TEXT("[Audio] Server-authoritative event '%s' was rejected on a client."),
			*Request.EventTag.ToString());
		return false;
	}

	if (Policy.RequestAuthority == EOutlierAudioRequestAuthority::OwningClient
		&& World->GetNetMode() == NM_Client)
	{
		AFirstPersonPlayerController* RequestingController =
			ResolveLocalRequestController(Request.EmitterActor);
		if (!RequestingController)
		{
			UE_LOG(LogOutlier, Warning,
				TEXT("[Audio] OwningClient event '%s' needs an EmitterActor owned by the local player. Controller-less emitters must start this request on the server."),
				*Request.EventTag.ToString());
			return false;
		}

		RequestingController->ServerRequestRelevantAudioAtLocation(Request);
		return true;
	}

	const FRuntimeCatalogEntry* Entry = ResolveBestEntry(Request.EventTag, Request.ContextTags);
	if (!Entry)
	{
		return false;
	}

	FOutlierResolvedAudioPlay ResolvedPlay;
	if (!BuildResolvedPlay(
		Request.EventTag,
		*Entry,
		Request,
		Policy.PlaybackMode,
		ResolvedPlay))
	{
		return false;
	}

	UE_LOG(LogOutlier, Warning,
		TEXT("[AudioSpatialDebug][ResolvedRequest] NetMode=%d Event='%s' Emitter='%s' Playback=%s Audience=%s Authority=%s ExplicitLocation=%d RequestLocation=%s ResolvedAtLocation=%d ResolvedLocation=%s Variant=%d"),
		static_cast<int32>(World->GetNetMode()),
		*Request.EventTag.ToString(),
		*GetNameSafe(Request.EmitterActor),
		PlaybackModeName(Policy.PlaybackMode),
		AudienceName(Policy.Audience),
		RequestAuthorityName(Policy.RequestAuthority),
		Request.bHasLocation,
		*Request.Location.ToCompactString(),
		ResolvedPlay.bAtLocation,
		*FVector(ResolvedPlay.Location).ToCompactString(),
		ResolvedPlay.VariantIndex);

	if (World->GetNetMode() == NM_Standalone)
	{
		return PlayResolvedAudioLocally(ResolvedPlay);
	}

	return RouteByAudience(Request, ResolvedPlay, Policy.Audience);
}

bool UOutlierAudioSubsystem::HandleServerRelevantAtLocationRequest(
	AFirstPersonPlayerController* RequestingController,
	const FOutlierAudioPlayRequest& Request)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !IsValid(RequestingController))
	{
		return false;
	}

	if (!IsEmitterOwnedByController(Request.EmitterActor, RequestingController))
	{
		UE_LOG(LogOutlier, Warning,
			TEXT("[Audio] Rejected client world-audio request. Controller='%s' Emitter='%s' Event='%s'."),
			*GetNameSafe(RequestingController),
			*GetNameSafe(Request.EmitterActor),
			*Request.EventTag.ToString());
		return false;
	}

	return PlayRelevantAtLocationFromOwningClient(Request);
}

bool UOutlierAudioSubsystem::PlayResolvedAudioLocally(
	const FOutlierResolvedAudioPlay& ResolvedPlay)
{
	const FRuntimeCatalogEntry* Entry =
		FindResolvedEntry(ResolvedPlay.EventTag, ResolvedPlay.VariantIndex);
	if (!Entry)
	{
		UE_LOG(LogOutlier, Warning,
			TEXT("[Audio] Could not resolve delivered event '%s' variant %d."),
			*ResolvedPlay.EventTag.ToString(),
			ResolvedPlay.VariantIndex);
		return false;
	}

	FPendingPlay PendingPlay;
	PendingPlay.World = GetWorld();
	PendingPlay.SourceName = Entry->SourceName;
	PendingPlay.bAtLocation = ResolvedPlay.bAtLocation;
	PendingPlay.Location = ResolvedPlay.Location;
	PendingPlay.VolumeMultiplier = Entry->VolumeMultiplier;
	PendingPlay.PitchMultiplier = Entry->PitchMultiplier;
	PendingPlay.StartTime = ResolvedPlay.StartTime;
	PendingPlay.VolumeType = Entry->VolumeType;

	return QueueOrPlay(*Entry, PendingPlay);
}

void UOutlierAudioSubsystem::SetVolumeMultiplier(
	EOutlierAudioVolumeType VolumeType,
	float NewMultiplier)
{
	VolumeMultipliers.FindOrAdd(VolumeType) = FMath::Clamp(NewMultiplier, 0.0f, 1.0f);
	RefreshActiveAudioComponentVolumes(VolumeType);
}

float UOutlierAudioSubsystem::GetVolumeMultiplier(EOutlierAudioVolumeType VolumeType) const
{
	if (const float* VolumeMultiplier = VolumeMultipliers.Find(VolumeType))
	{
		return *VolumeMultiplier;
	}

	return 1.0f;
}

void UOutlierAudioSubsystem::StopAllLocalAudio()
{
	for (FActiveAudioPlayback& ActivePlayback : ActiveAudioPlaybacks)
	{
		if (UAudioComponent* AudioComponent = ActivePlayback.Component.Get())
		{
			AudioComponent->Stop();
		}
	}

	ActiveAudioPlaybacks.Empty();
	PendingPlaysBySound.Empty();
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

const UOutlierAudioSubsystem::FRuntimeCatalogEntry* UOutlierAudioSubsystem::FindResolvedEntry(
	FGameplayTag EventTag,
	int32 VariantIndex) const
{
	const TArray<FRuntimeCatalogEntry>* Entries = CatalogEntriesByEvent.Find(EventTag);
	if (!Entries)
	{
		return nullptr;
	}

	return Entries->FindByPredicate([VariantIndex](const FRuntimeCatalogEntry& Entry)
	{
		return Entry.VariantIndex == VariantIndex;
	});
}

bool UOutlierAudioSubsystem::BuildResolvedPlay(
	FGameplayTag EventTag,
	const FRuntimeCatalogEntry& Entry,
	const FOutlierAudioPlayRequest& Request,
	EOutlierAudioPlaybackMode PlaybackMode,
	FOutlierResolvedAudioPlay& OutResolvedPlay) const
{
	OutResolvedPlay.EventTag = EventTag;
	OutResolvedPlay.VariantIndex = Entry.VariantIndex;
	OutResolvedPlay.StartTime = FMath::Max(0.0f, Request.StartTime);

	if (PlaybackMode == EOutlierAudioPlaybackMode::TwoD)
	{
		OutResolvedPlay.bAtLocation = false;
		return true;
	}

	if (PlaybackMode != EOutlierAudioPlaybackMode::AtLocation)
	{
		UE_LOG(LogOutlier, Error,
			TEXT("[Audio] Event '%s' uses an unsupported native PlaybackMode."),
			*EventTag.ToString());
		return false;
	}

	OutResolvedPlay.bAtLocation = true;
	if (Request.bHasLocation)
	{
		OutResolvedPlay.Location = Request.Location;
		return true;
	}

	if (IsValid(Request.EmitterActor))
	{
		OutResolvedPlay.Location = Request.EmitterActor->GetActorLocation();
		return true;
	}

	UE_LOG(LogOutlier, Warning,
		TEXT("[Audio] AtLocation event '%s' requires an explicit Location or an EmitterActor."),
		*EventTag.ToString());
	return false;
}

bool UOutlierAudioSubsystem::RouteByAudience(
	const FOutlierAudioPlayRequest& Request,
	const FOutlierResolvedAudioPlay& ResolvedPlay,
	EOutlierAudioAudience Audience)
{
	switch (Audience)
	{
	case EOutlierAudioAudience::Local:
		if (GetWorld() && GetWorld()->GetNetMode() != NM_DedicatedServer)
		{
			return PlayResolvedAudioLocally(ResolvedPlay);
		}
		UE_LOG(LogOutlier, Warning,
			TEXT("[Audio] Local event '%s' was requested on a dedicated server."),
			*ResolvedPlay.EventTag.ToString());
		return false;

	case EOutlierAudioAudience::Owner:
		return RouteOwner(Request, ResolvedPlay);

	case EOutlierAudioAudience::Relevant:
		return RouteRelevant(Request.EmitterActor, ResolvedPlay);

	default:
		UE_LOG(LogOutlier, Error,
			TEXT("[Audio] Event '%s' uses an unsupported native Audience."),
			*ResolvedPlay.EventTag.ToString());
		return false;
	}
}

bool UOutlierAudioSubsystem::RouteOwner(
	const FOutlierAudioPlayRequest& Request,
	const FOutlierResolvedAudioPlay& ResolvedPlay)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	AActor* Recipient = IsValid(Request.RecipientActor)
		? Request.RecipientActor.Get()
		: Request.EmitterActor.Get();

	AFirstPersonPlayerController* TargetController =
		ResolveOwningPlayerController(Recipient);
	if (!TargetController)
	{
		UE_LOG(LogOutlier, Warning,
			TEXT("[Audio] Owner audience event '%s' requires RecipientActor or EmitterActor with an owning player controller."),
			*ResolvedPlay.EventTag.ToString());
		return false;
	}

	TargetController->ClientPlayResolvedAudio(ResolvedPlay);
	return true;
}

bool UOutlierAudioSubsystem::RouteRelevant(
	AActor* EmitterActor,
	const FOutlierResolvedAudioPlay& ResolvedPlay)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return false;
	}

	if (World->GetNetMode() == NM_Standalone)
	{
		return PlayResolvedAudioLocally(ResolvedPlay);
	}

	int32 DeliveryCount = 0;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		AFirstPersonPlayerController* PlayerController =
			Cast<AFirstPersonPlayerController>(It->Get());
		if (!PlayerController)
		{
			continue;
		}

		if (IsValid(EmitterActor))
		{
			const AActor* ViewTarget = PlayerController->GetViewTarget();
			if (!EmitterActor->IsNetRelevantFor(
				PlayerController,
				ViewTarget,
				PlayerController->GetFocalLocation()))
			{
				continue;
			}
		}

		PlayerController->ClientPlayResolvedAudio(ResolvedPlay);

		const float ApproximateDistance = ResolvedPlay.bAtLocation
			? FVector::Distance(PlayerController->GetFocalLocation(), ResolvedPlay.Location)
			: 0.0f;
		UE_LOG(LogOutlier, Warning,
			TEXT("[AudioSpatialDebug][RelevantDelivery] Event='%s' Controller='%s' AtLocation=%d Location=%s ApproxDistance=%.1f"),
			*ResolvedPlay.EventTag.ToString(),
			*GetNameSafe(PlayerController),
			ResolvedPlay.bAtLocation,
			*FVector(ResolvedPlay.Location).ToCompactString(),
			ApproximateDistance);
		++DeliveryCount;
	}

	if (DeliveryCount == 0)
	{
		UE_LOG(LogOutlier, Verbose,
			TEXT("[Audio] Relevant event '%s' had no player recipients."),
			*ResolvedPlay.EventTag.ToString());
	}

	return DeliveryCount > 0;
}

AFirstPersonPlayerController* UOutlierAudioSubsystem::ResolveOwningPlayerController(
	AActor* Actor) const
{
	if (!IsValid(Actor))
	{
		return nullptr;
	}

	if (AFirstPersonPlayerController* PlayerController =
		Cast<AFirstPersonPlayerController>(Actor))
	{
		return PlayerController;
	}

	if (APawn* Pawn = Cast<APawn>(Actor))
	{
		if (AFirstPersonPlayerController* PlayerController =
			Cast<AFirstPersonPlayerController>(Pawn->GetController()))
		{
			return PlayerController;
		}
	}

	if (AFirstPersonPlayerController* InstigatorController =
		Cast<AFirstPersonPlayerController>(Actor->GetInstigatorController()))
	{
		return InstigatorController;
	}

	for (AActor* Owner = Actor->GetOwner(); IsValid(Owner); Owner = Owner->GetOwner())
	{
		if (AFirstPersonPlayerController* PlayerController =
			Cast<AFirstPersonPlayerController>(Owner))
		{
			return PlayerController;
		}

		if (APawn* OwnerPawn = Cast<APawn>(Owner))
		{
			if (AFirstPersonPlayerController* PlayerController =
				Cast<AFirstPersonPlayerController>(OwnerPawn->GetController()))
			{
				return PlayerController;
			}
		}
	}

	return nullptr;
}

AFirstPersonPlayerController* UOutlierAudioSubsystem::ResolveLocalRequestController(
	AActor* EmitterActor) const
{
	AFirstPersonPlayerController* Controller = ResolveOwningPlayerController(EmitterActor);
	return Controller && Controller->IsLocalController() ? Controller : nullptr;
}

bool UOutlierAudioSubsystem::IsEmitterOwnedByController(
	const AActor* EmitterActor,
	const AFirstPersonPlayerController* Controller) const
{
	if (!IsValid(EmitterActor) || !IsValid(Controller))
	{
		return false;
	}

	if (EmitterActor == Controller
		|| EmitterActor == Controller->GetPawn()
		|| EmitterActor->IsOwnedBy(Controller))
	{
		return true;
	}

	return EmitterActor->GetInstigatorController() == Controller;
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
	const FPendingPlay& PendingPlay)
{
	UWorld* World = PendingPlay.World.Get();
	if (!World || !Sound)
	{
		return;
	}

	FVector ListenerLocation = FVector::ZeroVector;
	bool bHasLocalListener = false;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PlayerController = It->Get();
		if (PlayerController && PlayerController->IsLocalController())
		{
			FRotator ListenerRotation;
			PlayerController->GetPlayerViewPoint(ListenerLocation, ListenerRotation);
			bHasLocalListener = true;
			break;
		}
	}

	const FSoundAttenuationSettings* Attenuation = Sound->GetAttenuationSettingsToApply();
	const float ListenerDistance = PendingPlay.bAtLocation && bHasLocalListener
		? FVector::Distance(ListenerLocation, PendingPlay.Location)
		: -1.0f;

	UE_LOG(LogOutlier, Warning,
		TEXT("[AudioSpatialDebug][Execute] NetMode=%d Method=%s Sound='%s' Location=%s Listener=%s Distance=%.1f AttenuationAsset='%s' HasSettings=%d VolumeAttenuation=%d Spatialization=%d InnerExtents=%s Falloff=%.1f SoundMaxDistance=%.1f"),
		static_cast<int32>(World->GetNetMode()),
		PendingPlay.bAtLocation ? TEXT("AtLocation") : TEXT("2D"),
		*GetNameSafe(Sound),
		*PendingPlay.Location.ToCompactString(),
		bHasLocalListener ? *ListenerLocation.ToCompactString() : TEXT("None"),
		ListenerDistance,
		*GetNameSafe(Sound->AttenuationSettings),
		Attenuation != nullptr,
		Attenuation ? Attenuation->bAttenuate : false,
		Attenuation ? Attenuation->bSpatialize : false,
		Attenuation ? *Attenuation->AttenuationShapeExtents.ToCompactString() : TEXT("None"),
		Attenuation ? Attenuation->FalloffDistance : 0.0f,
		Sound->GetMaxDistance());

	if (PendingPlay.bAtLocation && (!Attenuation || !Attenuation->bAttenuate))
	{
		UE_LOG(LogOutlier, Warning,
			TEXT("[AudioSpatialDebug][NoDistanceAttenuation] Sound='%s' is using PlaySoundAtLocation, but its Sound asset has no enabled volume attenuation. Distance will not reduce volume."),
			*GetNameSafe(Sound));
	}

	if (PendingPlay.bAtLocation)
	{
		UAudioComponent* AudioComponent = UGameplayStatics::SpawnSoundAtLocation(
			World,
			Sound,
			PendingPlay.Location,
			FRotator::ZeroRotator,
			PendingPlay.VolumeMultiplier * GetCombinedVolumeMultiplier(PendingPlay.VolumeType),
			PendingPlay.PitchMultiplier,
			PendingPlay.StartTime,
			Sound->AttenuationSettings,
			nullptr,
			true);
		TrackActiveAudioComponent(
			AudioComponent,
			PendingPlay.VolumeMultiplier,
			PendingPlay.VolumeType);
		return;
	}

	UAudioComponent* AudioComponent = UGameplayStatics::SpawnSound2D(
		World,
		Sound,
		PendingPlay.VolumeMultiplier * GetCombinedVolumeMultiplier(PendingPlay.VolumeType),
		PendingPlay.PitchMultiplier,
		PendingPlay.StartTime,
		nullptr,
		false,
		true);
	TrackActiveAudioComponent(
		AudioComponent,
		PendingPlay.VolumeMultiplier,
		PendingPlay.VolumeType);
}

void UOutlierAudioSubsystem::TrackActiveAudioComponent(
	UAudioComponent* AudioComponent,
	float BaseVolumeMultiplier,
	EOutlierAudioVolumeType VolumeType)
{
	if (!AudioComponent)
	{
		return;
	}

	RemoveInactiveAudioComponents();

	FActiveAudioPlayback& ActivePlayback = ActiveAudioPlaybacks.AddDefaulted_GetRef();
	ActivePlayback.Component = AudioComponent;
	ActivePlayback.BaseVolumeMultiplier = BaseVolumeMultiplier;
	ActivePlayback.VolumeType = VolumeType;
}

void UOutlierAudioSubsystem::RemoveInactiveAudioComponents()
{
	ActiveAudioPlaybacks.RemoveAll(
		[](const FActiveAudioPlayback& ActivePlayback)
		{
			const UAudioComponent* AudioComponent = ActivePlayback.Component.Get();
			return !AudioComponent || !AudioComponent->IsPlaying();
		});
}

void UOutlierAudioSubsystem::RefreshActiveAudioComponentVolumes(
	EOutlierAudioVolumeType ChangedVolumeType)
{
	RemoveInactiveAudioComponents();

	for (FActiveAudioPlayback& ActivePlayback : ActiveAudioPlaybacks)
	{
		if (ChangedVolumeType != EOutlierAudioVolumeType::Master
			&& ActivePlayback.VolumeType != ChangedVolumeType)
		{
			continue;
		}

		if (UAudioComponent* AudioComponent = ActivePlayback.Component.Get())
		{
			AudioComponent->SetVolumeMultiplier(
				ActivePlayback.BaseVolumeMultiplier
				* GetCombinedVolumeMultiplier(ActivePlayback.VolumeType));
		}
	}
}

float UOutlierAudioSubsystem::GetCombinedVolumeMultiplier(
	EOutlierAudioVolumeType VolumeType) const
{
	const float MasterMultiplier =
		GetVolumeMultiplier(EOutlierAudioVolumeType::Master);
	if (VolumeType == EOutlierAudioVolumeType::Master)
	{
		return MasterMultiplier;
	}

	return MasterMultiplier * GetVolumeMultiplier(VolumeType);
}
