#if WITH_DEV_AUTOMATION_TESTS

#include "TaggedAudioBatch.h"
#include "TaggedAudioBatchSettings.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "AnimationBlueprintLibrary.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Shooter/Anim/AnimNotify_TaggedAudio.h"
#include "Shooter/Anim/AnimNotifyState_TaggedAudio.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTaggedAudioBatchTest,
	"Outlier.Editor.Audio.TaggedNotifyBatch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTaggedAudioBatchTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UTaggedAudioBatchSettings> Settings(NewObject<UTaggedAudioBatchSettings>());
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

#endif
