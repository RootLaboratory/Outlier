#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Audio/OutlierAudioSubsystem.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Sound/SoundWave.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierSurfaceAudioSelectionTest,
	"Outlier.Audio.Surface.OrderedSelection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierSurfaceAudioSelectionTest::RunTest(const FString& Parameters)
{
	const auto Tag = [](const TCHAR* Name) { return FGameplayTag::RequestGameplayTag(FName(Name)); };
	const FGameplayTag Type = Tag(TEXT("Audio.Type.Player"));
	const FGameplayTag Base = Tag(TEXT("Audio.Context.Player.01.Type.Rifle.Hit"));
	const FGameplayTag Plastic = Tag(TEXT("Audio.Context.Player.01.Type.Rifle.Hit.Plastic"));
	const FGameplayTag Metal = Tag(TEXT("Audio.Context.Player.01.Type.Rifle.Hit.Metal"));
	const FGameplayTag Marble = Tag(TEXT("Audio.Context.Player.01.Type.Rifle.Hit.Marble"));
	const FGameplayTag Glass = Tag(TEXT("Audio.Context.Player.01.Type.Rifle.Hit.Glass"));
	const FGameplayTag Other = Tag(TEXT("Audio.Context.Player.01.Type.Pistol.Hit.Plastic"));
	UOutlierAudioSubsystem* Audio = NewObject<UOutlierAudioSubsystem>(NewObject<UGameInstance>());
	GetDefault<UOutlierSurfaceAudioSettings>()->BuildMappings(Audio->SurfaceContextMappings);
	TArray<FGameplayTag> Candidates;
	Audio->GetSurfaceContextCandidates(Base, SurfaceType2, Candidates);
	TestEqual(TEXT("Metal is evaluated first"), Candidates[0], Metal);
	TestEqual(TEXT("Explicit same-event Default is next"), Candidates[1], Plastic);
	TestEqual(TEXT("Original event is last compatibility context"), Candidates.Last(), Base);
	Audio->GetSurfaceContextCandidates(Base, SurfaceType_Max, Candidates);
	TestEqual(TEXT("Unknown surface selects Default"), Candidates[0], Plastic);
	Audio->GetSurfaceContextCandidates(Plastic, SurfaceType_Default, Candidates);
	TestEqual(TEXT("Default alias is deduplicated"), Candidates.Num(), 1);
	for (const TCHAR* EventName : { TEXT("Audio.Context.Player.01.Movement.Footstep"),
		TEXT("Audio.Context.Player.01.Movement.Jump"), TEXT("Audio.Context.Player.01.Movement.Land"),
		TEXT("Audio.Context.Player.01.Type.Melee.Hit") })
	{
		const FGameplayTag Event = Tag(EventName);
		// Melee.Hit의 기존 에셋은 Plastic으로 Redirect되므로 원래 문자열에서 변형 이름을 만든다.
		const FString EventPrefix(EventName);
		const FGameplayTag EventPlastic = Tag(*(EventPrefix + TEXT(".Plastic")));
		const FGameplayTag EventMetal = Tag(*(EventPrefix + TEXT(".Metal")));
		const FGameplayTag EventMarble = Tag(*(EventPrefix + TEXT(".Marble")));
		const bool bMovement = EventPrefix.StartsWith(TEXT("Audio.Context.Player.01.Movement."));
		const TCHAR* SurfaceNames[] = { TEXT("Plastic"), TEXT("Marble"), TEXT("Metal"), TEXT("Glass"),
			TEXT("Plastic"), TEXT("Fabric"), TEXT("Plant"), TEXT("Paper") };
		for (EPhysicalSurface Surface : { SurfaceType_Default, SurfaceType1, SurfaceType2, SurfaceType3,
			SurfaceType4, SurfaceType5, SurfaceType6, SurfaceType7, SurfaceType_Max })
		{
			Audio->GetSurfaceContextCandidates(Event, Surface, Candidates);
			const bool bMeleeGlass = EventPrefix.EndsWith(TEXT("Melee.Hit")) && Surface == SurfaceType3;
			const FGameplayTag Expected = bMovement && Surface >= SurfaceType1 && Surface <= SurfaceType7
				? Tag(*(EventPrefix + TEXT(".") + SurfaceNames[Surface]))
				: Surface == SurfaceType1 ? EventMarble : Surface == SurfaceType2 ? EventMetal
				: bMeleeGlass ? Tag(*(EventPrefix + TEXT(".Glass"))) : EventPlastic;
			TestEqual(TEXT("Event selects its configured surface or Plastic"), Candidates[0], Expected);
			TestTrue(TEXT("Every event retains its own Plastic fallback"), Candidates.Contains(EventPlastic));
			Audio->GetSurfaceContextCandidates(EventPlastic, Surface, Candidates);
			TestEqual(TEXT("Plastic alias selects the same surface as its event"), Candidates[0], Expected);
			if (bMovement)
			{
				// 새 표면 태그에 음원을 아직 연결하지 않은 경우에도 같은 행동의 기본음을 사용한다.
				UOutlierAudioSubsystem::FRuntimeCatalogEntry DefaultEntry;
				DefaultEntry.RequiredContext = EventPlastic;
				DefaultEntry.Sound = NewObject<USoundWave>();
				Audio->CatalogEntriesByType.FindOrAdd(Type) = { DefaultEntry };
				const auto* Fallback = Audio->ResolveOrderedEntry(Type, Candidates);
				TestTrue(TEXT("Unconfigured movement surface sound inherits its event Plastic"),
					Fallback && Fallback->RequiredContext == EventPlastic);
			}
		}
	}
	Audio->CatalogEntriesByType.Reset();
	Audio->GetSurfaceContextCandidates(Base, SurfaceType1, Candidates);
	TestEqual(TEXT("Rifle Marble uses its registered Context"), Candidates[0], Marble);
	const FGameplayTag Pistol = Tag(TEXT("Audio.Context.Player.01.Type.Pistol.Hit"));
	for (EPhysicalSurface Surface : { SurfaceType_Default, SurfaceType1, SurfaceType2, SurfaceType3,
		SurfaceType4, SurfaceType5, SurfaceType6, SurfaceType7, SurfaceType_Max })
	{
		Audio->GetSurfaceContextCandidates(Pistol, Surface, Candidates);
		TestEqual(TEXT("Pistol uses Plastic on every surface"), Candidates[0], Other);
		TestEqual(TEXT("Pistol only retains its event parent after Plastic"), Candidates.Num(), 2);
		Audio->GetSurfaceContextCandidates(Other, Surface, Candidates);
		TestEqual(TEXT("Pistol Plastic alias has one candidate on every surface"), Candidates.Num(), 1);
		TestEqual(TEXT("Pistol Plastic alias remains Plastic"), Candidates[0], Other);
	}
	for (const auto& Redirect : {
		TPair<const TCHAR*, const TCHAR*>(TEXT("Movement.Walk.Concrete"), TEXT("Movement.Footstep.Plastic")),
		TPair<const TCHAR*, const TCHAR*>(TEXT("Movement.Run.Concrete"), TEXT("Movement.Footstep.Plastic")),
		TPair<const TCHAR*, const TCHAR*>(TEXT("Movement.Jump.Casual"), TEXT("Movement.Land.Plastic")),
		TPair<const TCHAR*, const TCHAR*>(TEXT("Movement.Jump.Suit"), TEXT("Movement.Land.Plastic")),
		TPair<const TCHAR*, const TCHAR*>(TEXT("Type.Pistol.Hit.General"), TEXT("Type.Pistol.Hit.Plastic")),
		TPair<const TCHAR*, const TCHAR*>(TEXT("Type.Pistol.Hit.Metal"), TEXT("Type.Pistol.Hit.Plastic")),
		TPair<const TCHAR*, const TCHAR*>(TEXT("Type.Rifle.Hit.General"), TEXT("Type.Rifle.Hit.Plastic")),
		TPair<const TCHAR*, const TCHAR*>(TEXT("Type.Melee.Hit"), TEXT("Type.Melee.Hit.Plastic")) })
	{
		const FString Prefix(TEXT("Audio.Context.Player.01."));
		TestEqual(TEXT("Legacy context redirects to its replacement"),
			Tag(*(Prefix + Redirect.Key)), Tag(*(Prefix + Redirect.Value)));
	}
	Audio->GetSurfaceContextCandidates(FGameplayTag(), SurfaceType2, Candidates);
	TestTrue(TEXT("Target-owned impacts have no environment sound candidates"), Candidates.IsEmpty());

	USoundWave* Sound = NewObject<USoundWave>();
	auto Add = [&](FGameplayTag Context, bool bHasSound)
	{
		UOutlierAudioSubsystem::FRuntimeCatalogEntry Entry;
		Entry.RequiredContext = Context;
		if (bHasSound) Entry.Sound = Sound;
		Audio->CatalogEntriesByType.FindOrAdd(Type).Add(Entry);
	};
	Add(FGameplayTag(), true); // 일반 Bank fallback
	Add(Base, true); // 부모 Context
	Add(Other, true); // 다른 무기
	Add(Metal, false); // NULL 표면음
	Add(Plastic, true);
	const FGameplayTag Landing = Tag(TEXT("Audio.Context.Player.01.Movement.Land"));
	const FGameplayTag LandingPlastic = Tag(TEXT("Audio.Context.Player.01.Movement.Land.Plastic"));
	Add(LandingPlastic, true);
	Audio->GetSurfaceContextCandidates(Landing, SurfaceType2, Candidates);
	const auto* LandingEntry = Audio->ResolveOrderedEntry(Type, Candidates);
	TestTrue(TEXT("Missing Metal landing inherits Land.Plastic"),
		LandingEntry && LandingEntry->RequiredContext == LandingPlastic);
	const auto* Selected = Audio->ResolveOrderedEntry(Type, { Metal, Plastic });
	TestTrue(TEXT("NULL surface audio inherits exact same-event Default"), Selected && Selected->RequiredContext == Plastic);
	Selected = Audio->ResolveOrderedEntry(Type, { Glass });
	TestNull(TEXT("Missing event never inherits parent, other weapon or Bank fallback"), Selected);
	Add(Metal, true);
	Selected = Audio->ResolveOrderedEntry(Type, { Metal, Plastic });
	TestTrue(TEXT("Configured surface wins over Default"), Selected && Selected->RequiredContext == Metal);
	Audio->FailedSurfaceSounds.Add(FSoftObjectPath(Sound));
	TestNull(TEXT("Known failed sound paths are not retried"), Audio->ResolveOrderedEntry(Type, { Metal, Plastic }));
	USoundWave* DefaultSound = NewObject<USoundWave>();
	for (auto& Entry : Audio->CatalogEntriesByType.FindChecked(Type))
		if (Entry.RequiredContext == Plastic) Entry.Sound = DefaultSound;
	Selected = Audio->ResolveOrderedEntry(Type, { Metal, Plastic });
	TestTrue(TEXT("Failed Metal asset advances to valid explicit Default"), Selected && Selected->RequiredContext == Plastic);
	FOutlierSurfaceContextMapping FourSurfaces;
	FourSurfaces.DefaultContext = Plastic;
	FourSurfaces.NoneMetalContext = Other;
	FourSurfaces.MetalContext = Metal;
	FourSurfaces.GlassContext = Glass;
	FourSurfaces.GetCandidates(SurfaceType1, Candidates);
	TestEqual(TEXT("Legacy NoneMetal setting remains readable"), Candidates[0], Other);
	FourSurfaces.MarbleContext = Marble;
	FourSurfaces.GetCandidates(SurfaceType1, Candidates);
	TestEqual(TEXT("Explicit Marble wins over legacy NoneMetal"), Candidates[0], Marble);
	FourSurfaces.GetCandidates(SurfaceType3, Candidates);
	TestEqual(TEXT("Glass uses its explicit mapping"), Candidates[0], Glass);
	FourSurfaces.PlasticContext = Other;
	FourSurfaces.FabricContext = Metal;
	FourSurfaces.PlantContext = Marble;
	FourSurfaces.PaperContext = Glass;
	for (const auto& Pair : { TPair<EPhysicalSurface, FGameplayTag>(SurfaceType4, Other),
		TPair<EPhysicalSurface, FGameplayTag>(SurfaceType5, Metal),
		TPair<EPhysicalSurface, FGameplayTag>(SurfaceType6, Marble),
		TPair<EPhysicalSurface, FGameplayTag>(SurfaceType7, Glass) })
	{
		FourSurfaces.GetCandidates(Pair.Key, Candidates);
		TestEqual(TEXT("Additional surface uses its explicit Context"), Candidates[0], Pair.Value);
		TestEqual(TEXT("Additional surface retains event Default"), Candidates.Last(), Plastic);
	}
	Audio->CatalogEntriesByType.FindOrAdd(Tag(TEXT("Audio.Type.Weapon"))) = Audio->CatalogEntriesByType[Type];
	FGameplayTag FoundType;
	bool bPending = false;
	TestFalse(TEXT("Automatic Type lookup rejects ambiguous Context"), Audio->ResolveTypeForContext(Metal, FoundType, bPending));
	TestFalse(TEXT("Ambiguous lookup clears output"), FoundType.IsValid());
	TestFalse(TEXT("Null notify source is rejected"), Audio->IsAnimationAudioSource(nullptr, true));

	// Begin에서 승인한 토큰을 준비한다. End는 시점/월드가 바뀌어 현재 소스 검증이 실패해도 소비해야 한다.
	ACharacter* Character = GetMutableDefault<ACharacter>();
	USkeletalMeshComponent* AcceptedMesh = Character->GetMesh();
	USkeletalMeshComponent* ShadowMesh = NewObject<USkeletalMeshComponent>(Character);
	Audio->AnimationAudioTokens.FindOrAdd(AcceptedMesh).Add(Metal);
	Audio->SetAnimationAudioLoop(ShadowMesh, Type, Metal, false, true);
	TestTrue(TEXT("Shadow End cannot consume accepted source token"), Audio->AnimationAudioTokens.Contains(AcceptedMesh));
	TestFalse(TEXT("Changed/unbound world rejects current source validation"), Audio->IsAnimationAudioSource(AcceptedMesh, true));
	Audio->SetAnimationAudioLoop(AcceptedMesh, Type, Metal, false, true);
	TestFalse(TEXT("Accepted End consumes token without revalidating current source"), Audio->AnimationAudioTokens.Contains(AcceptedMesh));
	Audio->AnimationAudioTokens.FindOrAdd(AcceptedMesh).Add(Metal);
	Audio->AnimationAudioTokens.FindOrAdd(ShadowMesh).Add(Metal); // 시점 변경 뒤 새 메시가 승인된 상태
	Audio->SetAnimationAudioLoop(AcceptedMesh, Type, Metal, false, true);
	TestTrue(TEXT("Old source End retains newer accepted source token"), Audio->AnimationAudioTokens.FindChecked(ShadowMesh).Contains(Metal));
	Audio->SetAnimationAudioLoop(ShadowMesh, Type, Metal, false, false);
	TestTrue(TEXT("Last accepted End clears token ownership"), Audio->AnimationAudioTokens.IsEmpty());
	UOutlierAudioSubsystem::FPendingPlay Delayed;
	Delayed.AudioInstanceId = -37;
	Delayed.bLooping = false;
	Audio->PendingPlaysBySound.FindOrAdd(FSoftObjectPath(Sound)).Add(Delayed);
	Audio->StopLoopAudioLocally(-37);
	TestTrue(TEXT("State exit cancels delayed OneShot as well as looping playback"), Audio->CancelledLoopAudioInstances.Contains(-37));
	return true;
}

#endif
