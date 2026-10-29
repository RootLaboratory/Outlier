#include "UI/UpgradeDescWidget.h"

#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "FileMediaSource.h"
#include "IMediaEventSink.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "Outlier.h"
#include "PopupRetainerBox.h"

void UUpgradeDescWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (PopupRetainer)
	{
		PopupRetainer->OnClosed.AddUniqueDynamic(this, &UUpgradeDescWidget::HandlePopupClosed);
		PopupRetainer->ResetPopup();
	}
	if (Background && !bDefaultBackgroundBrushCached)
	{
		DefaultBackgroundBrush = Background->GetBrush();
		bDefaultBackgroundBrushCached = true;
	}
	if (DefaultPopupDesignSize.X <= 0.0f || DefaultPopupDesignSize.Y <= 0.0f)
	{
		DefaultPopupDesignSize = GetPopupDesignSize();
	}
	if (PopupRetainer && Background)
	{
		if (USizeBox* RootSizeBox = Cast<USizeBox>(PopupRetainer->GetContent()))
		{
			if (DefaultPopupDesignSize.X > 0.0f && DefaultPopupDesignSize.Y > 0.0f)
			{
				RootSizeBox->SetMinDesiredWidth(0.0f);
				RootSizeBox->SetMinDesiredHeight(0.0f);
				RootSizeBox->SetWidthOverride(DefaultPopupDesignSize.X);
				RootSizeBox->SetHeightOverride(DefaultPopupDesignSize.Y);
			}
		}
	}
	bDescriptionOpen = false;
	UnbindPreviewVisibility();
	StopPreviewMedia();
	CurrentPreviewMedia.Reset();

	RefreshTextBlocks();
	SetVisibility(ESlateVisibility::Collapsed);
}

void UUpgradeDescWidget::NativeDestruct()
{
	bDescriptionOpen = false;
	UnbindPreviewVisibility();
	StopPreviewMedia();
	CurrentPreviewMedia.Reset();
	if (PopupRetainer)
	{
		PopupRetainer->OnClosed.RemoveDynamic(this, &ThisClass::HandlePopupClosed);
	}
	Super::NativeDestruct();
}

void UUpgradeDescWidget::InjectNodeData(
	const FOutlierUpgradeNodeRow& InNodeData,
	EOutlierUpgradeNodeState InNodeState,
	int32 InCurrentNodeCount,
	bool bInCanAfford)
{
	StopPreviewMedia();
	CurrentPreviewMedia.Reset();
	UpdateNodeData(NAME_None, InNodeData, InNodeState, InCurrentNodeCount, bInCanAfford);
}

void UUpgradeDescWidget::UpdateNodeData(
	FName InNodeRowName,
	const FOutlierUpgradeNodeRow& InNodeData,
	EOutlierUpgradeNodeState InNodeState,
	int32 InCurrentNodeCount,
	bool bInCanAfford)
{
	const bool bNodeChanged = CurrentNodeRowName != InNodeRowName;
	CurrentNodeRowName = InNodeRowName;
	CurrentNodeData = InNodeData;
	CurrentNodeState = InNodeState;
	CurrentNodeCount = InCurrentNodeCount;
	bCanAfford = bInCanAfford;

	RefreshTextBlocks();
	// 비용/상태 갱신은 재생을 유지한다. 노드가 바뀌면 이전 영상을 버리고 다음 명시적 열기를 기다린다.
	if (bNodeChanged)
	{
		StopPreviewMedia();
		CurrentPreviewMedia.Reset();
	}
	InvalidateLayoutAndVolatility();
	ForceLayoutPrepass();

	if (PopupRetainer)
	{
		PopupRetainer->RequestRender();
	}
}

void UUpgradeDescWidget::ShowNodeData(
	FName InNodeRowName,
	const FOutlierUpgradeNodeRow& InNodeData,
	EOutlierUpgradeNodeState InNodeState,
	int32 InCurrentNodeCount,
	bool bInCanAfford,
	TSoftObjectPtr<UFileMediaSource> InPreviewMedia)
{
	// 명시적 열기는 PlayPopUp에서 한 번만 시작한다. 데이터 주입 중 중복으로 열지 않는다.
	bDescriptionOpen = false;
	UpdateNodeData(InNodeRowName, InNodeData, InNodeState, InCurrentNodeCount, bInCanAfford);
	// 영상의 설정 주체는 노드다. 캐시된 설명창에도 열 때마다 현재 노드의 값을 전달한다.
	CurrentPreviewMedia = InPreviewMedia;
	SetVisibility(ESlateVisibility::HitTestInvisible);
	InvalidateLayoutAndVolatility();
	ForceLayoutPrepass();
	PlayPopUp(true);
}

void UUpgradeDescWidget::HideNodeData(FName InNodeRowName)
{
	if (!InNodeRowName.IsNone() && !CurrentNodeRowName.IsNone() && InNodeRowName != CurrentNodeRowName)
	{
		return;
	}

	bClearWhenClosed = true;
	PlayPopUp(false);
}

FVector2D UUpgradeDescWidget::GetPopupDesignSize() const
{
	if (Background)
	{
		if (const UCanvasPanelSlot* BackgroundSlot = Cast<UCanvasPanelSlot>(Background->Slot))
		{
			const FVector2D Size = BackgroundSlot->GetSize();
			if (Size.X > 0.0f && Size.Y > 0.0f)
			{
				return Size;
			}
		}
		return Background->GetBrush().ImageSize;
	}
	return FVector2D::ZeroVector;
}

void UUpgradeDescWidget::ClearNodeData()
{
	bDescriptionOpen = false;
	UnbindPreviewVisibility();
	StopPreviewMedia();
	CurrentPreviewMedia.Reset();
	SetVisibility(ESlateVisibility::Collapsed);
	CurrentNodeRowName = NAME_None;
	CurrentNodeData = FOutlierUpgradeNodeRow();
	CurrentNodeState = EOutlierUpgradeNodeState::Locked;
	CurrentNodeCount = 0;
	bCanAfford = false;
	bClearWhenClosed = false;

	if (PopupRetainer)
	{
		PopupRetainer->ResetPopup();
	}
}

void UUpgradeDescWidget::PlayPopUp(bool bOpen)
{
	if (bOpen)
	{
		bClearWhenClosed = false;
		bDescriptionOpen = true;
		// 설명창과 자식 전체가 아래 노드의 호버·누르기 입력을 가로채지 않는다.
		SetVisibility(ESlateVisibility::HitTestInvisible);
		BindPreviewVisibility();
		if (!bDescriptionOpen) return;
		StartPreviewMedia();
	}
	else
	{
		bDescriptionOpen = false;
		// 닫힘 연출에는 마지막 프레임만 남기고, 열기 대기 중인 콜백은 여기서 무효화한다.
		SuspendPreviewMedia();
	}

	if (PopupRetainer)
	{
		bOpen ? PopupRetainer->PlayOpen() : PopupRetainer->PlayClose();
		return;
	}

	if (!bOpen)
	{
		HandlePopupClosed();
	}
}

void UUpgradeDescWidget::HandlePopupClosed()
{
	// 닫히는 도중 다시 열었다면 이전 완료 알림으로 새 표시를 지우지 않는다.
	if (bDescriptionOpen) return;
	UnbindPreviewVisibility();
	StopPreviewMedia();
	CurrentPreviewMedia.Reset();
	SetVisibility(ESlateVisibility::Collapsed);
	if (bClearWhenClosed)
	{
		CurrentNodeRowName = NAME_None;
		CurrentNodeData = FOutlierUpgradeNodeRow();
		CurrentNodeState = EOutlierUpgradeNodeState::Locked;
		CurrentNodeCount = 0;
		bCanAfford = false;
		bClearWhenClosed = false;
	}
}

void UUpgradeDescWidget::RefreshTextBlocks()
{
	if (Name)
	{
		Name->SetText(CurrentNodeData.DisplayName);
	}

	if (Desc)
	{
		Desc->SetText(CurrentNodeData.Desc);
	}

	if (Cost)
	{
		Cost->SetText(BuildCostNeedText());
		Cost->SetVisibility(CurrentNodeState == EOutlierUpgradeNodeState::Activated
			? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}

	RefreshCostTextStyle();
	RefreshStatusImage();
	RefreshBackground();
}

void UUpgradeDescWidget::StartPreviewMedia()
{
	StopPreviewMedia();
	if (!bDescriptionOpen || !MediaImage || IsDesignTime() || IsRunningDedicatedServer()
		|| (GetWorld() && GetWorld()->GetNetMode() == NM_DedicatedServer)) return;

	const TSoftObjectPtr<UFileMediaSource> MediaReference = CurrentPreviewMedia;
	if (MediaReference.IsNull()) return;
	UFileMediaSource* Source = MediaReference.LoadSynchronous();
	if (!Source)
	{
		UE_LOG(LogOutlier, Warning, TEXT("[Upgrade Preview] Node=%s failed to load %s"),
			*CurrentNodeRowName.ToString(), *MediaReference.ToSoftObjectPath().ToString());
		return;
	}

	// 같은 URL을 다시 열어도 이전 요청과 섞이지 않도록 재생 세션마다 플레이어를 분리한다.
	PreviewPlayer = NewObject<UMediaPlayer>(this, NAME_None, RF_Transient);
	PreviewPlayer->PlayOnOpen = false;
	PreviewPlayer->SetLooping(true);
	PreviewTexture = NewObject<UMediaTexture>(this, NAME_None, RF_Transient);
	PreviewTexture->AutoClear = true;
	PreviewTexture->ClearColor = FLinearColor::Black;
	PreviewTexture->SetMediaPlayer(PreviewPlayer);
	PreviewTexture->UpdateResource();
	const TWeakObjectPtr<UMediaPlayer> SessionPlayer = PreviewPlayer;
	PreviewMediaEventHandle = PreviewPlayer->OnMediaEvent().AddWeakLambda(this,
		[this, SessionPlayer](EMediaEvent Event)
		{
			HandlePreviewMediaEvent(SessionPlayer.Get(), Event);
		});
	bPreviewOpening = true;
	if (!PreviewPlayer->OpenSource(Source))
	{
		UE_LOG(LogOutlier, Warning, TEXT("[Upgrade Preview] Node=%s failed to open %s"),
			*CurrentNodeRowName.ToString(), *GetNameSafe(Source));
		StopPreviewMedia();
	}
}

void UUpgradeDescWidget::SuspendPreviewMedia()
{
	const bool bWasOpening = bPreviewOpening;
	bPreviewOpening = false;
	if (PreviewPlayer)
	{
		PreviewPlayer->OnMediaEvent().Remove(PreviewMediaEventHandle);
		// 열기 중에는 중단할 프레임이 없다. Pause 불가 매체도 Close하여 숨은 재생을 남기지 않는다.
		if (bWasOpening || (PreviewPlayer->IsPlaying() && !PreviewPlayer->Pause()))
		{
			PreviewPlayer->Close();
		}
	}
	PreviewMediaEventHandle.Reset();
}

void UUpgradeDescWidget::StopPreviewMedia()
{
	SuspendPreviewMedia();
	if (PreviewPlayer) PreviewPlayer->Close();
	if (PreviewTexture) PreviewTexture->SetMediaPlayer(nullptr);
	PreviewPlayer = nullptr;
	PreviewTexture = nullptr;
	if (MediaImage)
	{
		MediaImage->SetBrushResourceObject(nullptr);
		MediaImage->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UUpgradeDescWidget::HandlePreviewMediaEvent(UMediaPlayer* EventPlayer, EMediaEvent Event)
{
	if (!EventPlayer || EventPlayer != PreviewPlayer || !bDescriptionOpen || !bPreviewOpening) return;
	if (Event == EMediaEvent::MediaOpenFailed)
	{
		UE_LOG(LogOutlier, Warning, TEXT("[Upgrade Preview] Node=%s failed to open %s"),
			*CurrentNodeRowName.ToString(), *EventPlayer->GetUrl());
		StopPreviewMedia();
	}
	else if (Event == EMediaEvent::MediaOpened)
	{
		bPreviewOpening = false;
		MediaImage->SetBrushResourceObject(PreviewTexture);
		MediaImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		EventPlayer->SetLooping(true);
		EventPlayer->Play();
		if (PopupRetainer) PopupRetainer->RequestRender();
	}
}

void UUpgradeDescWidget::BindPreviewVisibility()
{
	UnbindPreviewVisibility();
	// 화면뿐 아니라 중간 Panel의 숨김도 받는다. UserWidget 경계는 Outer로 이어 간다.
	UWidget* Widget = this;
	while (Widget)
	{
		if (!Widget->IsVisible())
		{
			ClearNodeData();
			return;
		}
		PreviewVisibilityOwners.Add(Widget);
		Widget->AddFieldValueChangedDelegate(UWidget::FFieldNotificationClassDescriptor::Visibility,
			UWidget::FFieldValueChangedDelegate::CreateWeakLambda(this,
				[this](UObject* Object, UE::FieldNotification::FFieldId)
				{
					HandlePreviewVisibilityChanged(CastChecked<UWidget>(Object)->GetVisibility());
				}));
		if (UPanelWidget* Parent = Widget->GetParent())
		{
			Widget = Parent;
		}
		else
		{
			Widget = Widget->GetTypedOuter<UUserWidget>();
		}
	}
}

void UUpgradeDescWidget::UnbindPreviewVisibility()
{
	for (const TWeakObjectPtr<UWidget>& Owner : PreviewVisibilityOwners)
	{
		if (Owner.IsValid())
		{
			Owner->RemoveAllFieldValueChangedDelegates(UWidget::FFieldNotificationClassDescriptor::Visibility, this);
		}
	}
	PreviewVisibilityOwners.Reset();
}

void UUpgradeDescWidget::HandlePreviewVisibilityChanged(ESlateVisibility InVisibility)
{
	if (InVisibility == ESlateVisibility::Hidden || InVisibility == ESlateVisibility::Collapsed)
	{
		ClearNodeData();
	}
}

void UUpgradeDescWidget::RefreshBackground()
{
	const bool bPurchased = CurrentNodeState == EOutlierUpgradeNodeState::Activated;
	if (Background)
	{
		if (DefaultPopupDesignSize.X > 0.0f && DefaultPopupDesignSize.Y > 0.0f)
		{
			FVector2D PopupSize = DefaultPopupDesignSize;
			if (DefaultBackgroundTexture && DefaultBackgroundTexture->GetSizeX() > 0)
			{
				PopupSize.Y = PopupSize.X * DefaultBackgroundTexture->GetSizeY() / DefaultBackgroundTexture->GetSizeX();
			}
			if (bPurchased)
			{
				if (PurchasedBackgroundTexture && PurchasedBackgroundTexture->GetSizeX() > 0)
				{
					const float PurchasedHeight = PopupSize.X * PurchasedBackgroundTexture->GetSizeY() / PurchasedBackgroundTexture->GetSizeX();
					PopupSize.Y = FMath::Clamp(PurchasedHeight, 1.0f, PopupSize.Y);
				}
			}

			if (UCanvasPanelSlot* BackgroundSlot = Cast<UCanvasPanelSlot>(Background->Slot))
			{
				BackgroundSlot->SetSize(PopupSize);
			}
			if (PopupRetainer)
			{
				if (USizeBox* RootSizeBox = Cast<USizeBox>(PopupRetainer->GetContent()))
				{
					RootSizeBox->SetWidthOverride(PopupSize.X);
					RootSizeBox->SetHeightOverride(PopupSize.Y);
				}
			}
		}

		UTexture2D* Texture = bPurchased ? PurchasedBackgroundTexture.Get() : DefaultBackgroundTexture.Get();
		FSlateBrush Brush = bDefaultBackgroundBrushCached ? DefaultBackgroundBrush : Background->GetBrush();
		if (Texture)
		{
			Brush.SetResourceObject(Texture);
		}
		Background->SetBrush(Brush);
	}

}

void UUpgradeDescWidget::RefreshCostTextStyle()
{
	const FSlateColor& CostColor = CurrentNodeState == EOutlierUpgradeNodeState::Activated || bCanAfford
		? DefaultCostTextColor
		: InsufficientCostTextColor;

	if (Cost)
	{
		Cost->SetColorAndOpacity(CostColor);
	}
}

void UUpgradeDescWidget::RefreshStatusImage()
{
	if (!StatusImage)
	{
		return;
	}

	UTexture2D* Texture = AvailableTexture;
	if (CurrentNodeState == EOutlierUpgradeNodeState::Activated)
	{
		Texture = PurchasedDisabledTexture;
	}
	else if (CurrentNodeState == EOutlierUpgradeNodeState::Locked)
	{
		Texture = PrerequisiteDisabledTexture;
	}
	else if (!bCanAfford)
	{
		Texture = InsufficientDisabledTexture;
	}

	// 상태 이미지는 구매 입력을 받지 않는다. 미설정 상태로 바뀌면 이전 이미지를 남기지 않는다.
	StatusImage->SetBrushFromTexture(Texture);
	StatusImage->SetVisibility(ESlateVisibility::HitTestInvisible);
}

FText UUpgradeDescWidget::BuildCostNeedText() const
{
	FFormatOrderedArguments Arguments;
	Arguments.Add(FText::AsNumber(CurrentNodeData.Cost));
	Arguments.Add(FText::AsNumber(CurrentNodeCount));
	return FText::Format(CostNeedTextFormat, Arguments);
}
