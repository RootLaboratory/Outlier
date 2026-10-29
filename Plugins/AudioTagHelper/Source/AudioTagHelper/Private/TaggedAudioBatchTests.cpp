#if WITH_DEV_AUTOMATION_TESTS

#include "TaggedAudioBatch.h"
#include "TaggedAudioBatchSettings.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "AnimationBlueprintLibrary.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Shooter/Anim/AnimNotify_TaggedAudio.h"
#include "Shooter/Anim/AnimNotify_MakeNoise.h"
#include "Shooter/Anim/AnimNotifyState_TaggedAudio.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTaggedAudioBatchTest,
	"Outlier.Editor.Audio.TaggedNotifyBatch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTaggedAudioBatchTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UTaggedAudioBatchSettings> Settings(NewObject<UTaggedAudioBatchSettings>());
	Settings->Mode = ETaggedAudioBatchMode::Add;
	TStrongObjectPtr<UAnimSequence> Source(NewObject<UAnimSequence>(GetTransientPackage(), NAME_None, RF_Transactional));
	TStrongObjectPtr<UAnimSequence> Target(NewObject<UAnimSequence>(GetTransientPackage(), NAME_None, RF_Transactional));
	const auto SetLength = [](UAnimSequence* Animation, int32 Frames)
	{
		auto& Controller = Animation->GetController();
		Controller.InitializeModel();
		Controller.SetFrameRate(FFrameRate(30, 1), false);
		Controller.SetNumberOfFrames(FFrameNumber(Frames), false);
	};
	SetLength(Source.Get(), 30);
	SetLength(Target.Get(), 60);
	Settings->Times = { 0.25f, 0.25f, 0.75f };
	Settings->SocketName = TEXT("foot_l");
	auto Rows = TaggedAudioBatch::Preview(*Settings, { Source.Get() });
	TestEqual(TEXT("Repeated input remains visible for review"), Rows.Num(), 3);
	TestFalse(TEXT("Repeated input is skipped"), Rows[1].SkipReason.IsEmpty());
	TestEqual(TEXT("Only distinct placements are added"), TaggedAudioBatch::Apply(*Settings, Rows), 2);
	TestEqual(TEXT("Repeated application is harmless"), TaggedAudioBatch::Apply(*Settings, Rows), 0);

	UAnimationBlueprintLibrary::AddAnimationNotifyTrack(Target.Get(), TEXT("Gameplay"));
	FAnimNotifyEvent& Existing = Target->Notifies.AddDefaulted_GetRef();
	Existing.NotifyName = TEXT("ExistingGameplayNotify");
	Existing.Link(Target.Get(), 0.1f);
	const FGuid ExistingGuid = Existing.Guid = FGuid::NewGuid();
	Settings->Mode = ETaggedAudioBatchMode::Copy;
	Settings->SourceAnimation = Source.Get();
	Rows = TaggedAudioBatch::Preview(*Settings, { Target.Get() });
	TestEqual(TEXT("Only TaggedAudio events are copied"), Rows.Num(), 2);
	TestEqual(TEXT("Normalized copy scales first contact"), Rows[0].Placement.Time, 0.5f);
	TestEqual(TEXT("Normalized copy scales second contact"), Rows[1].Placement.Time, 1.5f);
	TestEqual(TEXT("Copy applies distinct objects"), TaggedAudioBatch::Apply(*Settings, Rows), 2);
	TestEqual(TEXT("Existing gameplay notify survives batch addition"), Target->Notifies.Num(), 3);
	TestTrue(TEXT("Existing gameplay identity is preserved"), Target->Notifies.ContainsByPredicate(
		[ExistingGuid](const auto& Event) { return Event.Guid == ExistingGuid && Event.NotifyName == TEXT("ExistingGameplayNotify"); }));
	for (const auto& Event : Target->Notifies)
	{
		if (auto* Notify = Cast<UAnimNotify_TaggedAudio>(Event.Notify))
		{
			TestTrue(TEXT("Copied notify belongs to its target"), Notify->GetOuter() == Target.Get());
			TestEqual(TEXT("Copied notify retains socket"), Notify->SocketName, FName(TEXT("foot_l")));
			TestTrue(TEXT("Surface query uses world mesh"), Notify->bQuerySurface && Notify->bWorldMeshOnly);
		}
	}
	if (GEditor && GEditor->Trans)
	{
		TestTrue(TEXT("Batch transaction can be undone"), GEditor->UndoTransaction());
		TestEqual(TEXT("Undo restores preexisting notify only"), Target->Notifies.Num(), 1);
		TestTrue(TEXT("Batch transaction can be redone"), GEditor->RedoTransaction());
		TestEqual(TEXT("Redo restores batch additions"), Target->Notifies.Num(), 3);
	}
	else AddError(TEXT("Editor transaction buffer is required for Undo verification"));

	Settings->Mode = ETaggedAudioBatchMode::Add;
	Settings->AudioContextTag = FGameplayTag::RequestGameplayTag(TEXT("Audio.Context.Player.01.Movement.Land"));
	Rows = TaggedAudioBatch::Preview(*Settings, { Target.Get() });
	TestTrue(TEXT("Gameplay-owned landing cannot be duplicated by notify"),
		Rows.ContainsByPredicate([](const auto& Row) { return Row.SkipReason.Contains(TEXT("상태 이벤트")); }));
	TestEqual(TEXT("Rejected gameplay-owned rows never modify target"), TaggedAudioBatch::Apply(*Settings, Rows), 0);
	Settings->AudioContextTag = FGameplayTag::RequestGameplayTag(TEXT("Audio.Context.Player.01.Movement.Footstep"));
	Settings->bLoop = true;
	Rows = TaggedAudioBatch::Preview(*Settings, { Target.Get() });
	TestTrue(TEXT("Footsteps cannot use loop states"), Rows[0].SkipReason.Contains(TEXT("단발")));

	Settings->AudioContextTag = FGameplayTag::RequestGameplayTag(TEXT("Audio.Context.Player.01.Type.Rifle.Load"));
	Settings->bQuerySurface = false;
	Settings->Times = { 0.2f, 0.9f };
	Settings->Duration = 0.3f;
	Rows = TaggedAudioBatch::Preview(*Settings, { Source.Get() });
	TestTrue(TEXT("Valid loop is accepted"), Rows[0].SkipReason.IsEmpty());
	TestFalse(TEXT("Out-of-bounds loop is skipped instead of truncated"), Rows[1].SkipReason.IsEmpty());
	TestEqual(TEXT("Only valid loop is added"), TaggedAudioBatch::Apply(*Settings, Rows), 1);
	Settings->Mode = ETaggedAudioBatchMode::Copy;
	Rows = TaggedAudioBatch::Preview(*Settings, { Target.Get() });
	const auto* Loop = Rows.FindByPredicate([](const auto& Row) { return Row.Placement.bLoop; });
	TestNotNull(TEXT("Loop is included in source copy"), Loop);
	if (Loop)
	{
		TestEqual(TEXT("Loop start is normalized"), Loop->Placement.Time, 0.4f);
		TestEqual(TEXT("Loop duration is normalized"), Loop->Placement.Duration, 0.6f);
	}
	Settings->bNormalizedTime = false;
	Rows = TaggedAudioBatch::Preview(*Settings, { Target.Get() });
	Loop = Rows.FindByPredicate([](const auto& Row) { return Row.Placement.bLoop; });
	if (Loop) TestEqual(TEXT("Absolute copy retains seconds"), Loop->Placement.Time, 0.2f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTaggedAudioMarkerBatchTest,
	"Outlier.Editor.Audio.TaggedNotifyMarkers", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTaggedAudioMarkerBatchTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UTaggedAudioBatchSettings> Settings(NewObject<UTaggedAudioBatchSettings>());
	TStrongObjectPtr<UAnimSequence> Target(NewObject<UAnimSequence>(GetTransientPackage(), NAME_None, RF_Transactional));
	auto& Controller = Target->GetController();
	Controller.InitializeModel();
	Controller.SetFrameRate(FFrameRate(30, 1), false);
	Controller.SetNumberOfFrames(FFrameNumber(60), false);
	const auto AddMarker = [&](FName Name, float Time)
	{
		FAnimSyncMarker& Marker = Target->AuthoredSyncMarkers.AddDefaulted_GetRef();
		Marker.MarkerName = Name;
		Marker.Time = Time;
	};
	// 마커 배열 순서나 정규화 비율 대신 대상에 저장된 실제 접촉 시간을 사용한다.
	AddMarker(TEXT("R"), 1.4f);
	AddMarker(TEXT("L"), 0.3f);
	AddMarker(TEXT("Other"), 0.8f);
	Target->RefreshSyncMarkerDataFromAuthored();
	UAnimationBlueprintLibrary::AddAnimationNotifyTrack(Target.Get(), TEXT("Gameplay"));
	FAnimNotifyEvent& Gameplay = Target->Notifies.AddDefaulted_GetRef();
	Gameplay.NotifyName = TEXT("MakeNoise");
	Gameplay.Link(Target.Get(), 0.2f);
	const FGuid GameplayGuid = Gameplay.Guid = FGuid::NewGuid();

	TestTrue(TEXT("Default mode uses sync markers"), Settings->Mode == ETaggedAudioBatchMode::SyncMarkers);
	auto Rows = TaggedAudioBatch::Preview(*Settings, { Target.Get() });
	TestEqual(TEXT("Only L/R markers are used"), Rows.Num(), 2);
	if (Rows.Num() != 2) return false;
	TestEqual(TEXT("Markers are sorted by actual seconds despite normalized setting"), Rows[0].Placement.Time, 0.3f);
	TestEqual(TEXT("Right marker retains its own time"), Rows[1].Placement.Time, 1.4f);
	TestEqual(TEXT("Left socket is selected by marker name"), Rows[0].Placement.Socket, Settings->LeftSocketName);
	TestEqual(TEXT("Right socket is selected by marker name"), Rows[1].Placement.Socket, Settings->RightSocketName);
	TestTrue(TEXT("Marker footsteps query the surface on the world mesh"), Rows[0].Placement.bQuerySurface && Rows[0].Placement.bWorldMeshOnly);
	TestEqual(TEXT("Two footstep notifies are added"), TaggedAudioBatch::Apply(*Settings, Rows), 2);
	TestEqual(TEXT("Reapplying the same plan does not duplicate"), TaggedAudioBatch::Apply(*Settings, Rows), 0);
	Rows = TaggedAudioBatch::Preview(*Settings, { Target.Get() });
	TestEqual(TEXT("Fresh preview of aligned events does nothing"), TaggedAudioBatch::Apply(*Settings, Rows), 0);

	FGuid LeftGuid;
	UAnimNotify_TaggedAudio* LeftNotify = nullptr;
	for (FAnimNotifyEvent& Event : Target->Notifies)
	{
		if (auto* Notify = Cast<UAnimNotify_TaggedAudio>(Event.Notify))
		{
			if (Notify->SocketName == Settings->LeftSocketName)
			{
				LeftGuid = Event.Guid;
				LeftNotify = Notify;
				Notify->TraceDistance = 123.0f;
			}
		}
	}
	TestNotNull(TEXT("Left footstep exists"), LeftNotify);
	if (!LeftNotify) return false;
	for (FAnimSyncMarker& Marker : Target->AuthoredSyncMarkers)
	{
		if (Marker.MarkerName == TEXT("L")) Marker.Time = 0.5f;
		if (Marker.MarkerName == TEXT("R")) Marker.Time = 1.6f;
	}
	Target->RefreshSyncMarkerDataFromAuthored();
	Settings->Mode = ETaggedAudioBatchMode::AlignExisting;
	Rows = TaggedAudioBatch::Preview(*Settings, { Target.Get() });
	TestTrue(TEXT("Movement preview includes old and new time"), TaggedAudioBatch::Describe(Rows).Contains(TEXT("0.3000s → 0.5000s")));
	TestEqual(TEXT("Align-only moves existing events"), TaggedAudioBatch::Apply(*Settings, Rows), 2);
	TestEqual(TEXT("Alignment creates no new notify"), Target->Notifies.Num(), 3);
	const auto FindLeft = [&]() -> const FAnimNotifyEvent*
	{
		return Target->Notifies.FindByPredicate([&](const auto& Event) { return Event.Guid == LeftGuid; });
	};
	const FAnimNotifyEvent* LeftEvent = FindLeft();
	TestNotNull(TEXT("Moved event keeps its GUID"), LeftEvent);
	if (LeftEvent)
	{
		TestEqual(TEXT("Moved event uses marker time"), LeftEvent->GetTime(), 0.5f);
		TestTrue(TEXT("Notify object is preserved"), LeftEvent->Notify.Get() == LeftNotify);
		TestEqual(TEXT("Existing query settings are preserved"), LeftNotify->TraceDistance, 123.0f);
	}
	TestTrue(TEXT("Gameplay notify is untouched"), Target->Notifies.ContainsByPredicate([&](const auto& Event)
	{
		return Event.Guid == GameplayGuid && Event.NotifyName == TEXT("MakeNoise") && Event.GetTime() == 0.2f;
	}));
	TestEqual(TEXT("All sync markers are preserved"), Target->AuthoredSyncMarkers.Num(), 3);
	TestEqual(TEXT("Stale movement plan cannot move events again"), TaggedAudioBatch::Apply(*Settings, Rows), 0);
	if (GEditor && GEditor->Trans)
	{
		TestTrue(TEXT("Alignment supports Undo"), GEditor->UndoTransaction());
		LeftEvent = FindLeft();
		if (LeftEvent) TestEqual(TEXT("Undo restores old notify time"), LeftEvent->GetTime(), 0.3f);
		else AddError(TEXT("Undo lost the original event"));
		TestTrue(TEXT("Alignment supports Redo"), GEditor->RedoTransaction());
		LeftEvent = FindLeft();
		if (LeftEvent) TestEqual(TEXT("Redo restores marker time"), LeftEvent->GetTime(), 0.5f);
		else AddError(TEXT("Redo lost the original event"));
	}
	else AddError(TEXT("Editor transaction buffer is required for Undo verification"));

	AddMarker(TEXT("L"), 1.9f);
	Rows = TaggedAudioBatch::Preview(*Settings, { Target.Get() });
	TestEqual(TEXT("Count mismatch produces one explanatory row"), Rows.Num(), 1);
	TestTrue(TEXT("Count mismatch is visible"), Rows[0].SkipReason.Contains(TEXT("개수가 다름")));
	TestEqual(TEXT("Count mismatch never edits events"), TaggedAudioBatch::Apply(*Settings, Rows), 0);
	Settings->Mode = ETaggedAudioBatchMode::SyncMarkers;
	Rows = TaggedAudioBatch::Preview(*Settings, { Target.Get() });
	TestEqual(TEXT("Add-and-align also rejects ambiguous partial placement"), TaggedAudioBatch::Apply(*Settings, Rows), 0);
	Target->Notifies.RemoveAll([](const auto& Event) { return Event.Notify && Event.Notify->IsA<UAnimNotify_TaggedAudio>(); });
	Settings->Mode = ETaggedAudioBatchMode::AlignExisting;
	Rows = TaggedAudioBatch::Preview(*Settings, { Target.Get() });
	TestEqual(TEXT("Align-only cannot add missing footsteps"), TaggedAudioBatch::Apply(*Settings, Rows), 0);
	TestEqual(TEXT("Align-only leaves gameplay notify alone"), Target->Notifies.Num(), 1);
	Target->AuthoredSyncMarkers.Reset();
	Rows = TaggedAudioBatch::Preview(*Settings, { Target.Get() });
	TestTrue(TEXT("Missing markers are explained"), Rows[0].SkipReason.Contains(TEXT("Sync Marker가 없음")));
	TestEqual(TEXT("Missing markers never edit target"), TaggedAudioBatch::Apply(*Settings, Rows), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNamedNotifyMarkerBatchTest,
	"Outlier.Editor.Audio.NamedNotifyMarkers", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNamedNotifyMarkerBatchTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UTaggedAudioBatchSettings> Settings(NewObject<UTaggedAudioBatchSettings>());
	TStrongObjectPtr<UAnimSequence> Target(NewObject<UAnimSequence>(GetTransientPackage(), NAME_None, RF_Transactional));
	auto& Controller = Target->GetController();
	Controller.InitializeModel();
	Controller.SetFrameRate(FFrameRate(30, 1), false);
	Controller.SetNumberOfFrames(FFrameNumber(60), false);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FAnimSyncMarker& Marker = Target->AuthoredSyncMarkers.AddDefaulted_GetRef();
		Marker.MarkerName = Index == 0 ? TEXT("L") : TEXT("R");
		Marker.Time = Index == 0 ? 0.4f : 1.4f;
	}
	UAnimationBlueprintLibrary::AddAnimationNotifyTrack(Target.Get(), TEXT("AI"));
	UAnimationBlueprintLibrary::AddAnimationNotifyTrack(Target.Get(), TEXT("Other"));
	const int32 OriginalTrackCount = Target->AnimNotifyTracks.Num();
	auto* Noise = NewObject<UAnimNotify_MakeNoise>(Target.Get(), NAME_None, RF_Transactional);
	Noise->Loudness = 0.75f;
	Noise->MaxRange = 321.0f;
	Noise->NoiseTag = TEXT("CustomNoise");
	UAnimationBlueprintLibrary::AddAnimationNotifyEventObject(Target.Get(), 0.2f, Noise, TEXT("AI"));
	auto* OtherNoise = NewObject<UAnimNotify_MakeNoise>(Target.Get(), NAME_None, RF_Transactional);
	UAnimationBlueprintLibrary::AddAnimationNotifyEventObject(Target.Get(), 1.2f, OtherNoise, TEXT("AI"));
	const FGuid NoiseGuid = Target->Notifies[0].Guid;
	const int32 NoiseTrack = Target->Notifies[0].TrackIndex;
	FAnimNotifyEvent& Unrelated = Target->Notifies.AddDefaulted_GetRef();
	Unrelated.NotifyName = TEXT("Unrelated");
	Unrelated.Guid = FGuid::NewGuid();
	Unrelated.Link(Target.Get(), 0.6f);
	const FGuid UnrelatedGuid = Unrelated.Guid;
	Settings->Mode = ETaggedAudioBatchMode::AlignNamedNotify;
	// 일반 Notify의 정렬에는 오디오 태그나 추가용 트랙 설정이 필요하지 않다.
	Settings->TrackName = NAME_None;
	Settings->AudioTypeTag = FGameplayTag();
	Settings->AudioContextTag = FGameplayTag();
	auto Rows = TaggedAudioBatch::Preview(*Settings, { Target.Get() });
	TestEqual(TEXT("MakeNoise objects are found by saved notify name"), Rows.Num(), 2);
	if (Rows.Num() != 2) return false;
	TestTrue(TEXT("Preview identifies named notify movement"), Rows[0].bNamedNotify && Rows[0].ExistingNotifyName == TEXT("MakeNoise"));
	TestTrue(TEXT("Preview shows old and new noise timing"), TaggedAudioBatch::Describe(Rows).Contains(TEXT("0.2000s → 0.4000s")));
	TestEqual(TEXT("MakeNoise events move without audio configuration"), TaggedAudioBatch::Apply(*Settings, Rows), 2);
	TestEqual(TEXT("No notify is created or removed"), Target->Notifies.Num(), 3);
	const auto FindNoise = [&]() -> const FAnimNotifyEvent*
	{
		return Target->Notifies.FindByPredicate([&](const auto& Event) { return Event.Guid == NoiseGuid; });
	};
	const FAnimNotifyEvent* Moved = FindNoise();
	TestNotNull(TEXT("Noise GUID survives alignment"), Moved);
	if (Moved)
	{
		TestEqual(TEXT("Noise time follows L marker"), Moved->GetTime(), 0.4f);
		TestTrue(TEXT("Noise object and class remain unchanged"), Moved->Notify.Get() == Noise);
		TestEqual(TEXT("Original track remains unchanged"), Moved->TrackIndex, NoiseTrack);
	}
	TestEqual(TEXT("Noise loudness is preserved"), Noise->Loudness, 0.75f);
	TestEqual(TEXT("Noise range is preserved"), Noise->MaxRange, 321.0f);
	TestEqual(TEXT("AI noise tag is preserved"), Noise->NoiseTag, FName(TEXT("CustomNoise")));
	TestTrue(TEXT("Other notify remains untouched"), Target->Notifies.ContainsByPredicate([&](const auto& Event)
	{
		return Event.Guid == UnrelatedGuid && Event.NotifyName == TEXT("Unrelated") && Event.GetTime() == 0.6f;
	}));
	TestEqual(TEXT("Named alignment creates no new track"), Target->AnimNotifyTracks.Num(), OriginalTrackCount);
	TestEqual(TEXT("Stale alignment cannot move events again"), TaggedAudioBatch::Apply(*Settings, Rows), 0);
	Rows = TaggedAudioBatch::Preview(*Settings, { Target.Get() });
	TestEqual(TEXT("Repeated preview is also harmless"), TaggedAudioBatch::Apply(*Settings, Rows), 0);
	if (GEditor && GEditor->Trans)
	{
		TestTrue(TEXT("Named alignment supports Undo"), GEditor->UndoTransaction());
		Moved = FindNoise();
		if (Moved) TestEqual(TEXT("Undo restores MakeNoise time"), Moved->GetTime(), 0.2f);
		else AddError(TEXT("Undo lost MakeNoise"));
		TestTrue(TEXT("Named alignment supports Redo"), GEditor->RedoTransaction());
		Moved = FindNoise();
		if (Moved) TestEqual(TEXT("Redo restores MakeNoise time"), Moved->GetTime(), 0.4f);
		else AddError(TEXT("Redo lost MakeNoise"));
	}
	else AddError(TEXT("Editor transaction buffer is required for Undo verification"));

	Settings->NotifyTrackFilter = TEXT("Other");
	Rows = TaggedAudioBatch::Preview(*Settings, { Target.Get() });
	TestEqual(TEXT("Track filter prevents changes outside selected track"), TaggedAudioBatch::Apply(*Settings, Rows), 0);
	Settings->NotifyTrackFilter = TEXT("AI");
	TestEqual(TEXT("Matching track finds both noises"), TaggedAudioBatch::Preview(*Settings, { Target.Get() }).Num(), 2);
	Settings->NotifyNameFilter = TEXT("Missing");
	Rows = TaggedAudioBatch::Preview(*Settings, { Target.Get() });
	TestEqual(TEXT("Missing name creates no notify"), TaggedAudioBatch::Apply(*Settings, Rows), 0);
	Settings->NotifyNameFilter = TEXT("MakeNoise");
	Target->AuthoredSyncMarkers.RemoveAt(1);
	Rows = TaggedAudioBatch::Preview(*Settings, { Target.Get() });
	TestTrue(TEXT("Count mismatch is reported for MakeNoise"), Rows[0].SkipReason.Contains(TEXT("개수가 다름")));
	TestEqual(TEXT("Count mismatch preserves all noise events"), TaggedAudioBatch::Apply(*Settings, Rows), 0);

	// 객체가 없는 Skeleton Notify도 같은 이름 필터로 시간만 정렬한다.
	Target->Notifies.Reset();
	FAnimNotifyEvent& SkeletonEvent = Target->Notifies.AddDefaulted_GetRef();
	SkeletonEvent.NotifyName = TEXT("MakeNoise");
	SkeletonEvent.Guid = FGuid::NewGuid();
	SkeletonEvent.TrackIndex = NoiseTrack;
	SkeletonEvent.Link(Target.Get(), 0.1f);
	Rows = TaggedAudioBatch::Preview(*Settings, { Target.Get() });
	TestEqual(TEXT("Skeleton notify supports name alignment"), TaggedAudioBatch::Apply(*Settings, Rows), 1);
	TestFalse(TEXT("Skeleton notify does not acquire an object"), Target->Notifies[0].Notify != nullptr);
	TestEqual(TEXT("Skeleton notify uses marker time"), Target->Notifies[0].GetTime(), 0.4f);
	return true;
}

#endif
