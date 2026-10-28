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

	bool IsMarkerMode(const UTaggedAudioBatchSettings& Settings)
	{
		return Settings.Mode == ETaggedAudioBatchMode::SyncMarkers || Settings.Mode == ETaggedAudioBatchMode::AlignExisting
			|| Settings.Mode == ETaggedAudioBatchMode::AlignNamedNotify;
	}

	void PreviewMarkers(const UTaggedAudioBatchSettings& Settings, UAnimSequenceBase* Target,
		TArray<FTaggedAudioBatchRow>& Rows)
	{
		const auto Skip = [&](const TCHAR* Reason)
		{
			FTaggedAudioBatchRow& Row = Rows.AddDefaulted_GetRef();
			Row.Target = Target;
			Row.SkipReason = Reason;
		};
		const UAnimSequence* Sequence = Cast<UAnimSequence>(Target);
		const bool bNamedNotify = Settings.Mode == ETaggedAudioBatchMode::AlignNamedNotify;
		if (!Sequence) { Skip(TEXT("L/R 마커 배치는 Animation Sequence만 지원")); return; }
		if (!bNamedNotify && Settings.TrackName.IsNone()) { Skip(TEXT("트랙 이름이 비어 있음")); return; }
		if (bNamedNotify && Settings.NotifyNameFilter.IsNone()) { Skip(TEXT("대상 Notify 이름이 비어 있음")); return; }
		if (Settings.LeftMarkerName.IsNone() || Settings.RightMarkerName.IsNone()
			|| Settings.LeftMarkerName == Settings.RightMarkerName)
		{
			Skip(TEXT("서로 다른 좌/우 마커 이름을 지정하세요"));
			return;
		}
		const FGameplayTag Footstep = FGameplayTag::RequestGameplayTag(TEXT("Audio.Context.Player.01.Movement.Footstep"), false);
		if (!bNamedNotify && (!Footstep.IsValid() || !Settings.AudioContextTag.MatchesTag(Footstep)))
		{
			Skip(TEXT("L/R 마커 배치는 발걸음 Context만 지원"));
			return;
		}
		TArray<FAnimSyncMarker> Markers;
		for (const FAnimSyncMarker& Marker : Sequence->AuthoredSyncMarkers)
		{
			if (Marker.MarkerName == Settings.LeftMarkerName || Marker.MarkerName == Settings.RightMarkerName)
				Markers.Add(Marker);
		}
		Markers.Sort([](const auto& A, const auto& B) { return A.Time < B.Time; });
		if (Markers.IsEmpty()) { Skip(TEXT("대상에 L/R Sync Marker가 없음")); return; }
		for (int32 Index = 0; Index < Markers.Num(); ++Index)
		{
			if (!FMath::IsFinite(Markers[Index].Time) || Markers[Index].Time < 0.0f || Markers[Index].Time > Target->GetPlayLength()
				|| (Index > 0 && FMath::IsNearlyEqual(Markers[Index - 1].Time, Markers[Index].Time, 0.0001f)))
			{
				Skip(TEXT("마커 시간이 유효하지 않거나 같은 시간에 중복됨"));
				return;
			}
		}

		TArray<const FAnimNotifyEvent*> Existing;
		for (const FAnimNotifyEvent& Event : Target->Notifies)
		{
			if (bNamedNotify)
			{
				// Skeleton Notify와 객체형 Notify 모두 이름으로 찾되, 구간 길이가 있는 State는 옮기지 않는다.
				if (Event.NotifyName == Settings.NotifyNameFilter && !Event.NotifyStateClass && Event.GetDuration() == 0.0f
					&& (Settings.NotifyTrackFilter.IsNone()
						|| (Target->AnimNotifyTracks.IsValidIndex(Event.TrackIndex)
							&& Target->AnimNotifyTracks[Event.TrackIndex].TrackName == Settings.NotifyTrackFilter)))
					Existing.Add(&Event);
				continue;
			}
			FTaggedAudioPlacement P;
			if (Target->AnimNotifyTracks.IsValidIndex(Event.TrackIndex)
				&& Target->AnimNotifyTracks[Event.TrackIndex].TrackName == Settings.TrackName
				&& ReadPlacement(Event, P) && !P.bLoop && P.Type == Settings.AudioTypeTag && P.Context == Settings.AudioContextTag)
				Existing.Add(&Event);
		}
		Existing.Sort([](const FAnimNotifyEvent& A, const FAnimNotifyEvent& B) { return A.GetTime() < B.GetTime(); });
		if (Existing.IsEmpty() && (Settings.Mode == ETaggedAudioBatchMode::AlignExisting || bNamedNotify))
		{
			Skip(bNamedNotify ? TEXT("지정 이름/트랙에 위치를 조정할 단발 Notify가 없음")
				: TEXT("지정 트랙/Type/Context에 위치를 조정할 TaggedAudio가 없음"));
			return;
		}
		// 일부만 배치됐거나 중복된 상태에서는 어느 발걸음을 옮길지 추측하지 않는다.
		// 같은 수의 이벤트를 시간순으로 대응시키고 기존 객체와 모든 설정을 보존한다.
		if (!Existing.IsEmpty() && Existing.Num() != Markers.Num())
		{
			Skip(TEXT("기존 Notify와 L/R 마커 개수가 다름: 자동 대응을 건너뜀"));
			return;
		}
		TSet<FGuid> ExistingGuids;
		for (int32 Index = 0; Index < Existing.Num(); ++Index)
		{
			const FAnimNotifyEvent& Event = *Existing[Index];
			const auto* Notify = bNamedNotify ? nullptr : CastChecked<UAnimNotify_TaggedAudio>(Event.Notify);
			if (!Event.Guid.IsValid() || ExistingGuids.Contains(Event.Guid)
				|| !FMath::IsFinite(Event.GetTime()) || Event.GetTime() < 0.0f || Event.GetTime() > Target->GetPlayLength()
				|| (Index > 0 && FMath::IsNearlyEqual(Existing[Index - 1]->GetTime(), Event.GetTime(), 0.0001f))
				|| (Notify && !Notify->SocketName.IsNone() &&
					((Notify->SocketName == Settings.LeftSocketName && Markers[Index].MarkerName != Settings.LeftMarkerName)
					|| (Notify->SocketName == Settings.RightSocketName && Markers[Index].MarkerName != Settings.RightMarkerName))))
			{
				Skip(TEXT("기존 Notify 식별자/시간 또는 좌우 소켓 대응이 불분명함"));
				return;
			}
			ExistingGuids.Add(Event.Guid);
		}
		for (int32 Index = 0; Index < Markers.Num(); ++Index)
		{
			FTaggedAudioBatchRow Row;
			Row.Target = Target;
			Row.MarkerName = Markers[Index].MarkerName;
			Row.bNamedNotify = bNamedNotify;
			FTaggedAudioPlacement& P = Row.Placement;
			if (!Existing.IsEmpty())
			{
				if (bNamedNotify)
				{
					P.Time = Existing[Index]->GetTime();
					P.SourceNotify = Existing[Index]->Notify.Get();
					Row.ExistingNotifyName = Existing[Index]->NotifyName;
					Row.PreviousTrackIndex = Existing[Index]->TrackIndex;
				}
				else ReadPlacement(*Existing[Index], P);
				Row.ExistingGuid = Existing[Index]->Guid;
				Row.PreviousTime = P.Time;
			}
			else
			{
				P.Type = Settings.AudioTypeTag;
				P.Context = Settings.AudioContextTag;
				P.Socket = Row.MarkerName == Settings.LeftMarkerName ? Settings.LeftSocketName : Settings.RightSocketName;
				P.bQuerySurface = true;
				P.TraceDistance = Settings.TraceDistance;
				P.bWorldMeshOnly = true;
			}
			P.Time = Markers[Index].Time;
			if (!bNamedNotify) Row.SkipReason = Validate(P, Target->GetPlayLength());
			if (Row.SkipReason.IsEmpty() && Row.ExistingGuid.IsValid() && FMath::IsNearlyEqual(Row.PreviousTime, P.Time, 0.0001f))
				Row.SkipReason = TEXT("이미 마커 위치에 있음");
			if (!bNamedNotify && Row.SkipReason.IsEmpty() && !Row.ExistingGuid.IsValid() && HasDuplicate(*Target, P))
				Row.SkipReason = TEXT("동일 설정/시간의 Notify가 이미 존재");
			Rows.Add(MoveTemp(Row));
		}
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
		if (IsMarkerMode(Settings))
		{
			PreviewMarkers(Settings, Target, Rows);
			continue;
		}
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
	const FScopedTransaction Transaction(NSLOCTEXT("TaggedAudioBatch", "Apply", "Tagged Audio 노티파이 일괄 배치"));
	TSet<UAnimSequenceBase*> Modified;
	int32 Added = 0;
	for (const FTaggedAudioBatchRow& Row : Rows)
	{
		UAnimSequenceBase* Target = Row.Target.Get();
		const auto& P = Row.Placement;
		if (!Target || !Row.SkipReason.IsEmpty()) continue;
		if (Row.bNamedNotify)
		{
			if (Settings.Mode != ETaggedAudioBatchMode::AlignNamedNotify || !Row.ExistingGuid.IsValid()
				|| !FMath::IsFinite(P.Time) || P.Time < 0.0f || P.Time > Target->GetPlayLength()) continue;
		}
		else if (!Validate(P, Target->GetPlayLength()).IsEmpty() || Settings.TrackName.IsNone()) continue;
		FAnimNotifyEvent* ExistingEvent = Row.ExistingGuid.IsValid()
			? Target->Notifies.FindByPredicate([&](const auto& Event) { return Event.Guid == Row.ExistingGuid; }) : nullptr;
		if (Row.bNamedNotify)
		{
			// 오디오 태그 검증을 통과시키기 위해 일반 Notify를 TaggedAudio로 변환하지 않는다.
			// 미리본 GUID/이름/객체/트랙/시간이 그대로일 때 원본 이벤트의 시간만 변경한다.
			if (!ExistingEvent || ExistingEvent->NotifyName != Row.ExistingNotifyName
				|| ExistingEvent->Notify.Get() != P.SourceNotify.Get() || ExistingEvent->TrackIndex != Row.PreviousTrackIndex
				|| ExistingEvent->NotifyStateClass || ExistingEvent->GetDuration() != 0.0f
				|| !FMath::IsNearlyEqual(ExistingEvent->GetTime(), Row.PreviousTime, 0.0001f)) continue;
		}
		else if (Row.ExistingGuid.IsValid())
		{
			FTaggedAudioPlacement Current;
			FTaggedAudioPlacement Expected = P;
			Expected.Time = Row.PreviousTime;
			if (!ExistingEvent || !ReadPlacement(*ExistingEvent, Current) || !SamePlacement(Current, Expected)) continue;
		}
		// 마커가 다음 Notify의 이전 위치로 이동할 수도 있다. 함께 이동할 이벤트는 충돌로 보지 않는다.
		const bool bBlocked = !Row.bNamedNotify && Target->Notifies.ContainsByPredicate([&](const FAnimNotifyEvent& Event)
		{
			FTaggedAudioPlacement Current;
			if (!ReadPlacement(Event, Current) || !SamePlacement(Current, P)) return false;
			return !ExistingEvent || !Rows.ContainsByPredicate([&](const FTaggedAudioBatchRow& Move)
			{
				return Move.Target.Get() == Target && Move.SkipReason.IsEmpty() && Move.ExistingGuid.IsValid()
					&& Move.ExistingGuid == Event.Guid && !FMath::IsNearlyEqual(Move.Placement.Time, Current.Time, 0.0001f);
			});
		});
		if (bBlocked) continue;
		if (!Modified.Contains(Target))
		{
			Target->SetFlags(RF_Transactional);
			Target->Modify();
			Modified.Add(Target);
			if (!Row.bNamedNotify)
			{
				TArray<FName> Tracks;
				UAnimationBlueprintLibrary::GetAnimationNotifyTrackNames(Target, Tracks);
				if (!Tracks.Contains(Settings.TrackName)) UAnimationBlueprintLibrary::AddAnimationNotifyTrack(Target, Settings.TrackName);
			}
		}
		if (ExistingEvent)
		{
			ExistingEvent->SetTime(P.Time);
			ExistingEvent->TriggerTimeOffset = GetTriggerTimeOffsetForType(Target->CalculateOffsetForNotify(P.Time));
			++Added;
			continue;
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
		if (!Row.MarkerName.IsNone())
		{
			Text += FString::Printf(TEXT("  Marker=%s | Notify=%s | %s\n"), *Row.MarkerName.ToString(),
				*Row.ExistingGuid.ToString(), Row.ExistingGuid.IsValid()
					? *FString::Printf(TEXT("이동 %.4fs → %.4fs"), Row.PreviousTime, Row.Placement.Time) : TEXT("새로 추가"));
		}
		Text += FString::Printf(TEXT("%s | %.4fs%s | %s | %s\n"), *GetPathNameSafe(Row.Target.Get()), Row.Placement.Time,
			Row.Placement.bLoop ? *FString::Printf(TEXT(" ~ %.4fs"), Row.Placement.Time + Row.Placement.Duration) : TEXT(""),
			Row.bNamedNotify ? *Row.ExistingNotifyName.ToString() : *Row.Placement.Context.ToString(),
			Row.SkipReason.IsEmpty() ? (Row.ExistingGuid.IsValid() ? TEXT("이동") : TEXT("추가")) : *Row.SkipReason);
		const auto& P = Row.Placement;
		if (Row.bNamedNotify)
		{
			Text += FString::Printf(TEXT("  기존 Notify 유지 | TrackIndex=%d | Object=%s\n"), Row.PreviousTrackIndex, *GetPathNameSafe(P.SourceNotify.Get()));
			continue;
		}
		Text += FString::Printf(TEXT("  %s | %s | Socket=%s | Query=%d (%.4f) | WorldMesh=%d | 2D=%d\n"),
			*P.Type.ToString(), P.bLoop ? TEXT("Loop") : TEXT("OneShot"), *P.Socket.ToString(),
			P.bQuerySurface, P.TraceDistance, P.bWorldMeshOnly, P.bTwoD);
	}
	return Text.IsEmpty() ? TEXT("Content Browser에서 Animation Sequence 또는 Montage를 선택하세요.") : Text;
}
