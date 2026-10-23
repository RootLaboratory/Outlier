#include "UI/RightGifBindedWidget.h"
#include "Audio/OutlierAudioFeedback.h"

#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "Event/OutlierEventSubsystem.h"
#include "FileMediaSource.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "Outlier.h"
#include "UI/GifInputWidget.h"
#include "UI/GifSubjectWidget.h"

void URightGifBindedWidget::NativeOnInitialized()
{
	GifPlayer = NewObject<UMediaPlayer>(this, NAME_None, RF_Transient);
	GifPlayer->PlayOnOpen = false;
	GifPlayer->OnMediaOpened.AddDynamic(this, &ThisClass::HandleGifMediaOpened);
	GifPlayer->OnMediaOpenFailed.AddDynamic(this, &ThisClass::HandleGifMediaOpenFailed);
	GifPlayer->OnEndReached.AddDynamic(this, &ThisClass::HandleGifMediaEndReached);
	GifTexture = NewObject<UMediaTexture>(this, NAME_None, RF_Transient);
	GifTexture->AutoClear = true;
	GifTexture->ClearColor = FLinearColor::Black;
	GifTexture->SetMediaPlayer(GifPlayer);
	GifTexture->UpdateResource();
	Super::NativeOnInitialized();
	ResetInfoContent();
}

void URightGifBindedWidget::NativeDestruct()
{
	StopGifMedia();
	Super::NativeDestruct();
}

void URightGifBindedWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	if (IsInfoPaused())
	{
		if (GifPlayer && GifPlayer->IsPlaying())
		{
			bGifPausedWithWorld = GifPlayer->Pause();
		}
		return;
	}
	if (bGifPausedWithWorld && GifPlayer)
	{
		GifPlayer->Play();
		bGifPausedWithWorld = false;
	}
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!bWaitMediaEnd) return;
	if (!bMediaEndPending)
	{
		MediaSafetyRemaining -= InDeltaTime;
		if (MediaSafetyRemaining > 0.0f) return;
		UE_LOG(LogOutlier, Warning, TEXT("[GIF UI] '%s' media end timed out."), *CurrentGifData.Tag.ToString());
	}
	const FGameplayTag EventTag = MediaEventTag;
	StopGifMedia();
	FinishSource(EventTag, true);
}

bool URightGifBindedWidget::TryResolveGifData(const FOutlierInfoWidgetRequest& Request, FOutlierGifDataRow& OutData) const
{
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	UOutlierEventSubsystem* Events = LocalPlayer ? LocalPlayer->GetSubsystem<UOutlierEventSubsystem>() : nullptr;
	if (!Events) return false;
	return Request.GifTag.IsValid()
		? Events->TryGetGifData(Request.GifTag, OutData)
		: Events->TryGetEventGifData(Request.EventTag, OutData);
}

bool URightGifBindedWidget::PrepareInfoRequest(FOutlierInfoWidgetRequest& Request) const
{
	FOutlierGifDataRow Data;
	if (!TryResolveGifData(Request, Data)) return false;
	// 영상이 있으면 재생 종료를 Source로 삼고, 없으면 GIF DT의 표시 시간을 사용한다.
	const bool bMediaEnd = Data.bFinishOnMediaEnd && !Data.GifMedia.IsNull() && Request.Duration == 0.0f
		&& (Request.EndCondition == EOutlierInfoEndCondition::Time || Request.EndCondition == EOutlierInfoEndCondition::Source);
	if (bMediaEnd)
	{
		Request.EndCondition = EOutlierInfoEndCondition::Source;
	}
	else if (Request.EndCondition == EOutlierInfoEndCondition::Source)
	{
		Request.EndCondition = EOutlierInfoEndCondition::Time;
	}
	// 이벤트만 지정한 요청을 포함해 chain은 해석된 GIF row를 키로 한다.
	Request.GifTag = Data.Tag;
	if (Request.EndCondition == EOutlierInfoEndCondition::Time && Request.Duration == 0.0f)
	{
		Request.Duration = Data.Time;
	}
	return Super::PrepareInfoRequest(Request);
}

bool URightGifBindedWidget::PresentInfo(const FOutlierInfoWidgetRequest& Request)
{
	ResetInfoContent();
	if (!TryResolveGifData(Request, CurrentGifData)) return false;
	FInputDisplayDefinition Display;
	if (CurrentGifData.InputTag.IsValid())
	{
		ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
		UOutlierEventSubsystem* Events = LocalPlayer ? LocalPlayer->GetSubsystem<UOutlierEventSubsystem>() : nullptr;
		if (!Events || !Events->TryGetInputDisplayData(CurrentGifData.InputTag, Display)) return false;
	}
	if (DescriptionText) DescriptionText->SetText(CurrentGifData.Description);
	if (CurrentGifData.bSubjectIconExist && !IsValid(CurrentGifData.SubjectIconTexture))
	{
		UE_LOG(LogOutlier, Warning, TEXT("[GIF UI] '%s' enables Subject Icon Exist without a valid SubjectIconTexture."), *CurrentGifData.Tag.ToString());
	}
	if (SubjectWidget)
	{
		SubjectWidget->SetSubjectData(CurrentGifData.Subject, CurrentGifData.bSubjectIconExist, CurrentGifData.SubjectIconTexture);
	}
	if (InputWidget)
	{
		InputWidget->SetInputData(Display, CurrentGifData.InputDisplayDesc);
	}
	StartGifMedia(Request);
	return true;
}

void URightGifBindedWidget::StartGifMedia(const FOutlierInfoWidgetRequest& Request)
{
	bWaitMediaEnd = Request.EndCondition == EOutlierInfoEndCondition::Source;
	MediaEventTag = Request.EventTag;
	MediaSafetyRemaining = CurrentGifData.Time;
	if (UTexture2D* Poster = CurrentGifData.PosterTexture.LoadSynchronous(); Poster && GifImage)
	{
		GifImage->SetBrushFromTexture(Poster, false);
		GifImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	if (CurrentGifData.GifMedia.IsNull() || !GifPlayer) return;
	UFileMediaSource* Media = CurrentGifData.GifMedia.LoadSynchronous();
	GifPlayer->SetLooping(!bWaitMediaEnd);
	bMediaOpening = Media && GifPlayer->OpenSource(Media);
	if (!bMediaOpening)
	{
		UE_LOG(LogOutlier, Warning, TEXT("[GIF UI] '%s' failed to open GifMedia."), *CurrentGifData.Tag.ToString());
	}
}

void URightGifBindedWidget::StopGifMedia()
{
	bGifPausedWithWorld = false;
	bMediaOpening = false;
	bWaitMediaEnd = false;
	bMediaEndPending = false;
	MediaPlayedCount = 0;
	MediaSafetyRemaining = 0.0f;
	MediaEventTag = FGameplayTag();
	if (GifPlayer) GifPlayer->Close();
	if (GifImage)
	{
		GifImage->SetBrushResourceObject(nullptr);
		GifImage->SetVisibility(ESlateVisibility::Hidden);
	}
}

void URightGifBindedWidget::HandleGifMediaOpened(FString OpenedUrl)
{
	if (!bMediaOpening || !GifPlayer) return;
	bMediaOpening = false;
	if (GifImage)
	{
		GifImage->SetBrushResourceObject(GifTexture);
		GifImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	// 열기 실패 대비 시간을 실제 재생 길이로 교체한다.
	const float Duration = static_cast<float>(GifPlayer->GetDuration().GetTotalSeconds());
	if (bWaitMediaEnd && Duration > 0.0f)
	{
		MediaSafetyRemaining = Duration * FMath::Max(1, CurrentGifData.PlayCount) + 1.0f;
	}
	GifPlayer->SetLooping(!bWaitMediaEnd);
	bGifPausedWithWorld = IsInfoPaused();
	if (!bGifPausedWithWorld) GifPlayer->Play();
}

void URightGifBindedWidget::HandleGifMediaOpenFailed(FString FailedUrl)
{
	if (!bMediaOpening) return;
	bMediaOpening = false;
	UE_LOG(LogOutlier, Warning, TEXT("[GIF UI] '%s' failed to open '%s'."), *CurrentGifData.Tag.ToString(), *FailedUrl);
}

void URightGifBindedWidget::HandleGifMediaEndReached()
{
	if (!bWaitMediaEnd || bMediaEndPending || !GifPlayer) return;
	if (++MediaPlayedCount < FMath::Max(1, CurrentGifData.PlayCount))
	{
		GifPlayer->Rewind();
		bGifPausedWithWorld = IsInfoPaused();
		if (!bGifPausedWithWorld) GifPlayer->Play();
		return;
	}
	bMediaEndPending = true;
}

void URightGifBindedWidget::ResetInfoContent()
{
	StopGifMedia();
	CurrentGifData = FOutlierGifDataRow();
	if (DescriptionText) DescriptionText->SetText(FText::GetEmpty());
	if (SubjectWidget) SubjectWidget->ClearSubjectData();
	if (InputWidget) InputWidget->ClearInputData();
}

void URightGifBindedWidget::InfoFinished(const FOutlierInfoWidgetRequest& Request, bool bCompleted)
{
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	if (UOutlierEventSubsystem* Events = bCompleted && LocalPlayer ? LocalPlayer->GetSubsystem<UOutlierEventSubsystem>() : nullptr)
	{
		Events->NotifyInfoCompleted(EOutlierInfoRegion::Right, Request);
	}
}

void URightGifBindedWidget::StartInfoPresentation()
{
	Super::StartInfoPresentation();
	OutlierAudioFeedback::Local(this, TEXT("Audio.Context.UI.Ingame.Tutorial.Explain"));
}
