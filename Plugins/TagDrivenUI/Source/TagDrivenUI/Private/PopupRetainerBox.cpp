#include "PopupRetainerBox.h"

#include "Materials/MaterialInstanceDynamic.h"
#include "Slate/SRetainerWidget.h"

void UPopupRetainerBox::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	SetTextureParameter(TextureParameterName);
	PopupMID = GetEffectMaterial();

	if (bResetRequested)
	{
		TryResetPopup();
	}

	if (bPendingPlayback)
	{
		TryStartPlayback();
	}
	else if (!bPlaying && !IsDesignTime())
	{
		SetRetainRendering(false);
	}
}

void UPopupRetainerBox::ReleaseSlateResources(bool bReleaseChildren)
{
	bActiveTimerRegistered = false;
	bPendingPlayback = bPendingPlayback || bPlaying;
	bResetRequested = !bPendingPlayback;
	PopupMID = nullptr;

	Super::ReleaseSlateResources(bReleaseChildren);
}

void UPopupRetainerBox::PlayOpen()
{
	PlayPopupAdvanced(OpenDirection, OpenDuration, false);
}

void UPopupRetainerBox::PlayClose()
{
	PlayPopupAdvanced(OpenDirection, CloseDuration, true);
}

void UPopupRetainerBox::PlayPopup(bool bReverse)
{
	bReverse ? PlayClose() : PlayOpen();
}

void UPopupRetainerBox::PlayPopupAdvanced(float InDirection, float InDuration, bool bReverse)
{
	Direction = FMath::Clamp(InDirection, -1.0f, 1.0f);
	Duration = FMath::Max(InDuration, KINDA_SMALL_NUMBER);
	Elapsed = 0.0f;
	bReversePlayback = bReverse;
	bPendingPlayback = true;
	bResetRequested = false;

	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	TryStartPlayback();
}

void UPopupRetainerBox::ResetPopup()
{
	bPlaying = false;
	bPendingPlayback = false;
	bReversePlayback = false;
	bResetRequested = true;

	Elapsed = 0.0f;
	Duration = 0.0f;
	Direction = OpenDirection;

	TryResetPopup();
}

bool UPopupRetainerBox::TryResetPopup()
{
	if (!MyRetainerWidget.IsValid())
	{
		return false;
	}

	SetTextureParameter(TextureParameterName);
	PopupMID = GetEffectMaterial();

	if (!PopupMID)
	{
		SetRetainRendering(false);
		return false;
	}

	PopupMID->SetScalarParameterValue(DirectionParameterName, OpenDirection);
	PopupMID->SetScalarParameterValue(AmountParameterName, 0.0f);
	RequestRender();

	SetRetainRendering(false);
	bResetRequested = false;
	return true;
}

bool UPopupRetainerBox::TryStartPlayback()
{
	if (!MyRetainerWidget.IsValid())
	{
		return false;
	}

	SetTextureParameter(TextureParameterName);
	SetRetainRendering(true);
	PopupMID = GetEffectMaterial();

	if (!PopupMID)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PopupRetainerBox] Effect material is not configured. Widget=%s"), *GetName());
		SetRetainRendering(false);
		bPendingPlayback = false;
		return false;
	}

	PopupMID->SetScalarParameterValue(DirectionParameterName, Direction);
	PopupMID->SetScalarParameterValue(AmountParameterName, bReversePlayback ? 1.0f : 0.0f);

	bPlaying = true;
	bPendingPlayback = false;
	RequestRender();

	if (!bActiveTimerRegistered)
	{
		MyRetainerWidget->RegisterActiveTimer(
			0.0f,
			FWidgetActiveTimerDelegate::CreateUObject(this, &UPopupRetainerBox::HandlePopupTick));
		bActiveTimerRegistered = true;
	}

	return true;
}

EActiveTimerReturnType UPopupRetainerBox::HandlePopupTick(double CurrentTime, float DeltaTime)
{
	if (!bPlaying || !PopupMID)
	{
		bPlaying = false;
		bActiveTimerRegistered = false;
		return EActiveTimerReturnType::Stop;
	}

	Elapsed += DeltaTime;
	const float Alpha = FMath::Clamp(Elapsed / Duration, 0.0f, 1.0f);
	const float Amount = bReversePlayback ? 1.0f - Alpha : Alpha;

	PopupMID->SetScalarParameterValue(AmountParameterName, Amount);
	RequestRender();

	if (Alpha < 1.0f)
	{
		return EActiveTimerReturnType::Continue;
	}

	const bool bFinishedClosing = bReversePlayback;
	bPlaying = false;
	bActiveTimerRegistered = false;

	if (bFinishedClosing)
	{
		SetVisibility(ESlateVisibility::Collapsed);
		SetRetainRendering(false);
		OnClosed.Broadcast();
	}
	else
	{
		SetRetainRendering(false);
		OnOpened.Broadcast();
	}

	return EActiveTimerReturnType::Stop;
}
