#include "TaggedAudioBatch.h"

#include "TaggedAudioBatchSettings.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "AnimationBlueprintLibrary.h"
#include "ScopedTransaction.h"
#include "Shooter/Anim/AnimNotify_TaggedAudio.h"
#include "Shooter/Anim/AnimNotifyState_TaggedAudio.h"

UTaggedAudioBatchSettings::UTaggedAudioBatchSettings()
{
	AudioTypeTag = FGameplayTag::RequestGameplayTag(TEXT("Audio.Type.Player"), false);
	AudioContextTag = FGameplayTag::RequestGameplayTag(TEXT("Audio.Context.Player.01.Movement.Footstep"), false);
}

namespace
{
	bool ReadPlacement(const FAnimNotifyEvent& Event, FTaggedAudioPlacement& Out)
	{
		// 알 수 없는 파생 클래스의 추가 동작을 일반 TaggedAudio 설정으로 잘라 복사하지 않는다.
		if (Event.Notify && Event.Notify->GetClass() == UAnimNotify_TaggedAudio::StaticClass())
		{
			const auto* Notify = CastChecked<UAnimNotify_TaggedAudio>(Event.Notify);
			Out.Type = Notify->AudioTypeTag;
			Out.Context = Notify->AudioContextTag;
			Out.Socket = Notify->SocketName;
			Out.bQuerySurface = Notify->bQuerySurface;
			Out.TraceDistance = Notify->TraceDistance;
			Out.bWorldMeshOnly = Notify->bQuerySurface || Notify->bWorldMeshOnly;
			Out.SourceNotify = Event.Notify.Get();
		}
		else if (Event.NotifyStateClass && Event.NotifyStateClass->GetClass() == UAnimNotifyState_TaggedAudio::StaticClass())
		{
			const auto* Notify = CastChecked<UAnimNotifyState_TaggedAudio>(Event.NotifyStateClass);
			Out.bLoop = true;
			Out.Type = Notify->AudioTypeTag;
			Out.Context = Notify->AudioContextTag;
			Out.bWorldMeshOnly = Notify->bWorldMeshOnly;
			Out.bTwoD = Notify->bTwoD;
			Out.SourceNotify = Event.NotifyStateClass.Get();
		}
		else return false;
		Out.Time = Event.GetTime();
		Out.Duration = Out.bLoop ? Event.GetDuration() : 0.0f;
		return true;
	}

	bool SamePlacement(const FTaggedAudioPlacement& A, const FTaggedAudioPlacement& B)
	{
		if (A.bLoop != B.bLoop || A.Type != B.Type || A.Context != B.Context
			|| A.bWorldMeshOnly != B.bWorldMeshOnly || !FMath::IsNearlyEqual(A.Time, B.Time, 0.0001f)) return false;
		if (A.bLoop) return A.bTwoD == B.bTwoD && FMath::IsNearlyEqual(A.Duration, B.Duration, 0.0001f);
		return A.Socket == B.Socket && A.bQuerySurface == B.bQuerySurface
			&& (!A.bQuerySurface || FMath::IsNearlyEqual(A.TraceDistance, B.TraceDistance));
	}

	bool HasDuplicate(const UAnimSequenceBase& Target, const FTaggedAudioPlacement& Placement)
	{
		for (const FAnimNotifyEvent& Event : Target.Notifies)
		{
			FTaggedAudioPlacement Existing;
			if (ReadPlacement(Event, Existing) && SamePlacement(Existing, Placement)) return true;
		}
		return false;
	}

	FString Validate(const FTaggedAudioPlacement& P, float Length)
	{
		const auto Root = [](const TCHAR* Name) { return FGameplayTag::RequestGameplayTag(FName(Name), false); };
		if (!P.Type.IsValid() || !P.Type.MatchesTag(Root(TEXT("Audio.Type")))
			|| !P.Context.IsValid() || !P.Context.MatchesTag(Root(TEXT("Audio.Context")))) return TEXT("Type/Context 태그를 확인하세요");
		if (UAnimNotify_TaggedAudio::IsGameplayOwnedContext(P.Context)) return TEXT("실제 상태 이벤트가 담당하는 사운드");
		const bool bFootstep = P.Context.MatchesTag(Root(TEXT("Audio.Context.Player.01.Movement.Footstep")));
		if (P.bLoop && bFootstep) return TEXT("발걸음은 단발 Notify만 지원");
		if (!P.bLoop && P.bQuerySurface && !bFootstep) return TEXT("표면 조회는 발걸음 Context에만 사용");
		if (!P.bLoop && P.bQuerySurface && (!FMath::IsFinite(P.TraceDistance) || P.TraceDistance < 1.0f)) return TEXT("Trace Distance는 1 이상이어야 함");
		if (!FMath::IsFinite(Length) || Length <= 0.0f || !FMath::IsFinite(P.Time)
			|| P.Time < 0.0f || P.Time > Length) return TEXT("애니메이션 길이 밖의 시작 시간");
		if (P.bLoop && (!FMath::IsFinite(P.Duration) || P.Duration <= 0.0f
			|| P.Time + P.Duration > Length)) return TEXT("루프 종료가 애니메이션 범위를 벗어남");
		return FString();
	}
}

TArray<FTaggedAudioBatchRow> TaggedAudioBatch::Preview(const UTaggedAudioBatchSettings& Settings,
	const TArray<UAnimSequenceBase*>& Targets)
{
	TArray<FTaggedAudioPlacement> Templates;
	if (Settings.Mode == ETaggedAudioBatchMode::Copy && Settings.SourceAnimation)
	{
		for (const FAnimNotifyEvent& Event : Settings.SourceAnimation->Notifies)
		{
			FTaggedAudioPlacement Placement;
			if (ReadPlacement(Event, Placement)) Templates.Add(Placement);
		}
	}
	else if (Settings.Mode == ETaggedAudioBatchMode::Add)
	{
		for (float Time : Settings.Times)
		{
			FTaggedAudioPlacement Placement;
			Placement.bLoop = Settings.bLoop;
			Placement.Type = Settings.AudioTypeTag;
			Placement.Context = Settings.AudioContextTag;
			Placement.Socket = Settings.SocketName;
			Placement.bQuerySurface = !Settings.bLoop && Settings.bQuerySurface;
			Placement.TraceDistance = Settings.TraceDistance;
			Placement.bWorldMeshOnly = Placement.bQuerySurface || Settings.bWorldMeshOnly;
			Placement.bTwoD = Settings.bTwoD;
			Placement.Time = Time;
			Placement.Duration = Settings.bLoop ? Settings.Duration : 0.0f;
			Templates.Add(Placement);
		}
	}

	TArray<FTaggedAudioBatchRow> Rows;
	TSet<UAnimSequenceBase*> SeenTargets;
	for (UAnimSequenceBase* Target : Targets)
	{
		if (!IsValid(Target) || SeenTargets.Contains(Target)) continue;
		SeenTargets.Add(Target);
		if (Templates.IsEmpty())
		{
			FTaggedAudioBatchRow& Row = Rows.AddDefaulted_GetRef();
			Row.Target = Target;
			Row.SkipReason = TEXT("추가 시간 또는 복사할 TaggedAudio Notify가 없음");
			continue;
		}
		for (const FTaggedAudioPlacement& Template : Templates)
		{
			FTaggedAudioBatchRow Row;
			Row.Target = Target;
			Row.Placement = Template;
			float Scale = 1.0f;
			if (Settings.bNormalizedTime)
			{
				const float SourceLength = Settings.Mode == ETaggedAudioBatchMode::Copy ? Settings.SourceAnimation->GetPlayLength() : 1.0f;
				if (SourceLength <= 0.0f) Row.SkipReason = TEXT("기준 애니메이션 길이가 0");
				else Scale = Target->GetPlayLength() / SourceLength;
			}
			Row.Placement.Time *= Scale;
			Row.Placement.Duration *= Scale;
			if (!Target->IsA<UAnimSequence>() && !Target->IsA<UAnimMontage>()) Row.SkipReason = TEXT("Sequence/Montage만 지원");
			if (Settings.TrackName.IsNone()) Row.SkipReason = TEXT("트랙 이름이 비어 있음");
			if (Row.SkipReason.IsEmpty()) Row.SkipReason = Validate(Row.Placement, Target->GetPlayLength());
			if (Row.SkipReason.IsEmpty() && (HasDuplicate(*Target, Row.Placement)
				|| Rows.ContainsByPredicate([&](const auto& Existing)
				{
					return Existing.Target == Target && Existing.SkipReason.IsEmpty() && SamePlacement(Existing.Placement, Row.Placement);
				}))) Row.SkipReason = TEXT("동일 설정/시간의 Notify가 이미 존재");
			Rows.Add(MoveTemp(Row));
		}
	}
	return Rows;
}

int32 TaggedAudioBatch::Apply(const UTaggedAudioBatchSettings& Settings, const TArray<FTaggedAudioBatchRow>& Rows)
{
	const FScopedTransaction Transaction(NSLOCTEXT("TaggedAudioBatch", "Apply", "Tagged Audio 노티파이 일괄 추가"));
	TSet<UAnimSequenceBase*> Modified;
	int32 Added = 0;
	for (const FTaggedAudioBatchRow& Row : Rows)
	{
		UAnimSequenceBase* Target = Row.Target.Get();
		const auto& P = Row.Placement;
		if (!Target || !Row.SkipReason.IsEmpty() || !Validate(P, Target->GetPlayLength()).IsEmpty()
			|| HasDuplicate(*Target, P) || Settings.TrackName.IsNone()) continue;
		if (!Modified.Contains(Target))
		{
			Target->SetFlags(RF_Transactional);
			Target->Modify();
			Modified.Add(Target);
			TArray<FName> Tracks;
			UAnimationBlueprintLibrary::GetAnimationNotifyTrackNames(Target, Tracks);
			if (!Tracks.Contains(Settings.TrackName)) UAnimationBlueprintLibrary::AddAnimationNotifyTrack(Target, Settings.TrackName);
		}

		// Notify는 대상 애니메이션 소유로 새로 만든다. 원본 객체나 기존 Notify 배열을 수정하지 않는다.
		if (P.bLoop)
		{
			auto* Notify = P.SourceNotify.IsValid() ? DuplicateObject(CastChecked<UAnimNotifyState_TaggedAudio>(P.SourceNotify.Get()), Target,
				MakeUniqueObjectName(Target, UAnimNotifyState_TaggedAudio::StaticClass()))
				: NewObject<UAnimNotifyState_TaggedAudio>(Target, NAME_None, RF_Transactional);
			Notify->SetFlags(RF_Transactional);
			Notify->AudioTypeTag = P.Type;
			Notify->AudioContextTag = P.Context;
			Notify->bWorldMeshOnly = P.bWorldMeshOnly;
			Notify->bTwoD = P.bTwoD;
			UAnimationBlueprintLibrary::AddAnimationNotifyStateEventObject(Target, P.Time, P.Duration, Notify, Settings.TrackName);
		}
		else
		{
			auto* Notify = P.SourceNotify.IsValid() ? DuplicateObject(CastChecked<UAnimNotify_TaggedAudio>(P.SourceNotify.Get()), Target,
				MakeUniqueObjectName(Target, UAnimNotify_TaggedAudio::StaticClass()))
				: NewObject<UAnimNotify_TaggedAudio>(Target, NAME_None, RF_Transactional);
			Notify->SetFlags(RF_Transactional);
			Notify->AudioTypeTag = P.Type;
			Notify->AudioContextTag = P.Context;
			Notify->SocketName = P.Socket;
			Notify->bQuerySurface = P.bQuerySurface;
			Notify->TraceDistance = P.TraceDistance;
			Notify->bWorldMeshOnly = P.bWorldMeshOnly;
			UAnimationBlueprintLibrary::AddAnimationNotifyEventObject(Target, P.Time, Notify, Settings.TrackName);
		}
		++Added;
	}
	for (UAnimSequenceBase* Target : Modified)
	{
		Target->SortNotifies();
		Target->RefreshCacheData();
		Target->MarkPackageDirty();
	}
	return Added;
}

FString TaggedAudioBatch::Describe(const TArray<FTaggedAudioBatchRow>& Rows)
{
	FString Text;
	for (const auto& Row : Rows)
	{
		Text += FString::Printf(TEXT("%s | %.4fs%s | %s | %s\n"), *GetPathNameSafe(Row.Target.Get()), Row.Placement.Time,
			Row.Placement.bLoop ? *FString::Printf(TEXT(" ~ %.4fs"), Row.Placement.Time + Row.Placement.Duration) : TEXT(""),
			*Row.Placement.Context.ToString(), Row.SkipReason.IsEmpty() ? TEXT("추가") : *Row.SkipReason);
		const auto& P = Row.Placement;
		Text += FString::Printf(TEXT("  %s | %s | Socket=%s | Query=%d (%.4f) | WorldMesh=%d | 2D=%d\n"),
			*P.Type.ToString(), P.bLoop ? TEXT("Loop") : TEXT("OneShot"), *P.Socket.ToString(),
			P.bQuerySurface, P.TraceDistance, P.bWorldMeshOnly, P.bTwoD);
	}
	return Text.IsEmpty() ? TEXT("Content Browser에서 Animation Sequence 또는 Montage를 선택하세요.") : Text;
}
