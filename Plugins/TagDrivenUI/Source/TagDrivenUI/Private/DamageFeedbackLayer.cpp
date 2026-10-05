// Fill out your copyright notice in the Description page of Project Settings.

#include "DamageFeedbackLayer.h"

#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/RetainerBox.h"
#include "DamageFeedbackIndicator.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RedDamageFeedbackWidget.h"
#include "ShieldDamageFeedbackWidget.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogDamageFeedbackLayer, Log, All);

void UDamageFeedbackLayer::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// 피격마다 만들고 지우지 않도록 위젯 수명 동안 한 번만 미리 만들어둔다.
	BuildPool(FeedbackCanvas, RedFeedbackClass, RedPoolSize, RedScale, RedDistance, RedAlignment, RedPool);
	if (ShieldFeedbackClass && (!ShieldMaskRetainer || !ShieldMaskCanvas))
	{
		UE_LOG(LogDamageFeedbackLayer, Warning,
			TEXT("ShieldMaskRetainer and ShieldMaskCanvas must be bound in WBP_DamageFeedbackLayer: %s"), *GetName());
	}
	BuildPool(ShieldMaskCanvas, ShieldFeedbackClass, ShieldPoolSize, ShieldScale, ShieldDistance, ShieldAlignment, ShieldPool);
}

void UDamageFeedbackLayer::NativeConstruct()
{
	Super::NativeConstruct();

	// MainWidget 루트 캔버스를 꽉 채운다. 인디케이터는 이 영역의 중앙을 기준으로 잡힌다.
	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
	{
		CanvasSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		CanvasSlot->SetOffsets(FMargin(0.0f));
		CanvasSlot->SetAlignment(FVector2D::ZeroVector);
	}

	// Shield Retainer 를 Layer 전체와 같은 크기로 배치한다.
	if (ShieldMaskRetainer && ShieldMaskRetainer->GetParent() == FeedbackCanvas)
	{
		if (UCanvasPanelSlot* RetainerSlot = Cast<UCanvasPanelSlot>(ShieldMaskRetainer->Slot))
		{
			RetainerSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
			RetainerSlot->SetOffsets(FMargin(0.0f));
			RetainerSlot->SetAlignment(FVector2D::ZeroVector);
			RetainerSlot->SetAutoSize(false);
			RetainerSlot->SetZOrder(1);
		}
		else
		{
			UE_LOG(LogDamageFeedbackLayer, Warning,
				TEXT("ShieldMaskRetainer must be a direct child of FeedbackCanvas: %s"), *GetName());
		}
		ShieldMaskRetainer->SetRenderingPhase(0, 1);
	}
	else if (ShieldMaskRetainer)
	{
		UE_LOG(LogDamageFeedbackLayer, Warning,
			TEXT("ShieldMaskRetainer must be a direct child of FeedbackCanvas: %s"), *GetName());
	}
	if (ShieldMaskCanvas && ShieldMaskCanvas->GetParent() != ShieldMaskRetainer)
	{
		UE_LOG(LogDamageFeedbackLayer, Warning,
			TEXT("ShieldMaskCanvas must be the child of ShieldMaskRetainer: %s"), *GetName());
	}

	// Construct 시점의 실제 뷰포트 크기를 초기값으로 사용한다.
	if (const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer())
	{
		if (const UGameViewportClient* ViewportClient = LocalPlayer->ViewportClient)
		{
			if (const FViewport* Viewport = ViewportClient->Viewport)
			{
				const FIntPoint ViewportSize = Viewport->GetSizeXY();
				if (ViewportSize.X > 0 && ViewportSize.Y > 0)
				{
					ShieldCanvasResolution = FVector2D(ViewportSize.X, ViewportSize.Y);
				}
			}
		}
	}
	if (!ViewportResizedHandle.IsValid())
	{
		ViewportResizedHandle = FViewport::ViewportResizedEvent.AddUObject(
			this, &UDamageFeedbackLayer::HandleViewportResized);
	}
	ApplyShieldUVScale();

	ClearDamageFeedback();
}

void UDamageFeedbackLayer::NativeDestruct()
{
	if (ViewportResizedHandle.IsValid())
	{
		FViewport::ViewportResizedEvent.Remove(ViewportResizedHandle);
		ViewportResizedHandle.Reset();
	}
	Super::NativeDestruct();
}

void UDamageFeedbackLayer::SetShieldCanvasResolution(const FVector2D InCanvasResolution)
{
	if (!FMath::IsFinite(InCanvasResolution.X) || !FMath::IsFinite(InCanvasResolution.Y)
		|| InCanvasResolution.X <= 0.0f || InCanvasResolution.Y <= 0.0f)
	{
		return;
	}

	ShieldCanvasResolution = InCanvasResolution;
	ApplyShieldUVScale();
}

void UDamageFeedbackLayer::HandleViewportResized(FViewport* Viewport, uint32 Unused)
{
	const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	const UGameViewportClient* ViewportClient = LocalPlayer ? LocalPlayer->ViewportClient.Get() : nullptr;
	if (!Viewport || !ViewportClient || ViewportClient->Viewport != Viewport)
	{
		return;
	}

	const FIntPoint ViewportSize = Viewport->GetSizeXY();
	if (ViewportSize.X > 0 && ViewportSize.Y > 0)
	{
		SetShieldCanvasResolution(FVector2D(ViewportSize.X, ViewportSize.Y));
	}
}

void UDamageFeedbackLayer::ApplyShieldUVScale()
{
	if (!ShieldMaskRetainer || ShieldCanvasResolution.X <= 0.0f || ShieldCanvasResolution.Y <= 0.0f
		|| ShieldMaskReferenceSize.X <= 0.0f || ShieldMaskReferenceSize.Y <= 0.0f)
	{
		return;
	}

	UMaterialInstanceDynamic* EffectMID = ShieldMaskRetainer->GetEffectMaterial();
	if (!EffectMID)
	{
		return;
	}

	// 정사각형 기준 크기에서 시작해 뷰포트 가로/세로를 각각 보정한다.
	const float ScaleX = static_cast<float>(ShieldCanvasResolution.X / ShieldMaskReferenceSize.X);
	const float ScaleY = static_cast<float>(ShieldCanvasResolution.Y / ShieldMaskReferenceSize.Y);
	EffectMID->SetVectorParameterValue(
		ShieldUVScaleParameterName, FLinearColor(ScaleX, ScaleY, 0.0f, 0.0f));
	ShieldMaskRetainer->RequestRender();
}

void UDamageFeedbackLayer::ShowDamageFeedback(AActor* InCharacter, const FVector& InDamageOrigin)
{
	if (!IsValid(InCharacter))
	{
		return;
	}

	bool bShown = false;
	if (bShowRedFeedback)
	{
		bShown |= ShowFromPool(RedPool, InCharacter, InDamageOrigin);
	}
	if (bShowShieldFeedback)
	{
		bShown |= ShowFromPool(ShieldPool, InCharacter, InDamageOrigin);
	}

	if (bShown)
	{
		// 활성 인디케이터가 생겼으니 Layer 를 다시 보이게 해서 Tick 을 켠다.
		SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UDamageFeedbackLayer::ClearDamageFeedback()
{
	for (UDamageFeedbackIndicator* Indicator : RedPool)
	{
		if (Indicator)
		{
			Indicator->ClearFeedback();
		}
	}
	for (UDamageFeedbackIndicator* Indicator : ShieldPool)
	{
		if (Indicator)
		{
			Indicator->ClearFeedback();
		}
	}

	// Collapsed 위젯은 Slate 가 Tick 하지 않는다. 사용 중인 인디케이터가 없으면 Layer Tick 을 이걸로 끈다.
	SetVisibility(ESlateVisibility::Collapsed);
}

void UDamageFeedbackLayer::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	const bool bRedActive = TickPool(RedPool, InDeltaTime);
	const bool bShieldActive = TickPool(ShieldPool, InDeltaTime);
	if (!bRedActive && !bShieldActive)
	{
		SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UDamageFeedbackLayer::BuildPool(
	UCanvasPanel* ParentCanvas,
	TSubclassOf<UDamageFeedbackIndicator> IndicatorClass,
	const int32 PoolSize,
	const FVector2D& Scale,
	const FVector2D& Distance,
	const FVector2D& Alignment,
	TArray<TObjectPtr<UDamageFeedbackIndicator>>& OutPool)
{
	if (!ParentCanvas || !IndicatorClass)
	{
		return;
	}

	for (int32 Index = OutPool.Num(); Index < PoolSize; ++Index)
	{
		UDamageFeedbackIndicator* Indicator = CreateWidget<UDamageFeedbackIndicator>(this, IndicatorClass);
		if (!Indicator)
		{
			continue;
		}

		// 중앙 앵커에 Alignment 지점을 맞추고, 같은 지점을 회전축으로 쓴다.
		// Distance 는 앵커 기준 슬롯 위치라서 회전축도 같이 옮겨진다.
		// 슬롯 크기는 WBP desired size 를 그대로 쓴다. 화면 중앙과의 간격은 WBP 의 Spacer 가 정한다.
		if (UCanvasPanelSlot* CanvasSlot = ParentCanvas->AddChildToCanvas(Indicator))
		{
			CanvasSlot->SetAnchors(FAnchors(0.5f, 0.5f));
			CanvasSlot->SetAlignment(Alignment);
			CanvasSlot->SetPosition(Distance);
			CanvasSlot->SetAutoSize(true);
		}
		// Pivot 기준으로 키우므로 간격도 같은 비율로 따라간다.
		Indicator->SetRenderTransformPivot(Alignment);
		Indicator->SetRenderScale(Scale);
		Indicator->ClearFeedback();
		OutPool.Add(Indicator);
	}
}

bool UDamageFeedbackLayer::ShowFromPool(
	const TArray<TObjectPtr<UDamageFeedbackIndicator>>& Pool,
	AActor* InCharacter,
	const FVector& InDamageOrigin)
{
	UDamageFeedbackIndicator* Target = nullptr;
	for (UDamageFeedbackIndicator* Indicator : Pool)
	{
		if (!Indicator)
		{
			continue;
		}

		if (!Indicator->IsFeedbackActive())
		{
			Target = Indicator;
			break;
		}

		// 전부 사용 중이면 가장 오래 켜져 있던 것을 재사용한다.
		if (!Target || Indicator->GetFeedbackElapsed() > Target->GetFeedbackElapsed())
		{
			Target = Indicator;
		}
	}

	return Target && Target->StartFeedback(InCharacter, InDamageOrigin);
}

bool UDamageFeedbackLayer::TickPool(const TArray<TObjectPtr<UDamageFeedbackIndicator>>& Pool, const float DeltaTime)
{
	bool bAnyActive = false;
	for (UDamageFeedbackIndicator* Indicator : Pool)
	{
		if (Indicator && Indicator->IsFeedbackActive())
		{
			bAnyActive |= Indicator->TickFeedback(DeltaTime);
		}
	}
	return bAnyActive;
}
