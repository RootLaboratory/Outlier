// Fill out your copyright notice in the Description page of Project Settings.

#include "DamageFeedbackLayer.h"

#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "DamageFeedbackIndicator.h"
#include "RedDamageFeedbackWidget.h"
#include "ShieldDamageFeedbackWidget.h"

void UDamageFeedbackLayer::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// 피격마다 만들고 지우지 않도록 위젯 수명 동안 한 번만 미리 만들어둔다.
	BuildPool(RedFeedbackClass, RedPoolSize, RedScale, RedDistance, RedAlignment, RedPool);
	BuildPool(ShieldFeedbackClass, ShieldPoolSize, ShieldScale, ShieldDistance, ShieldAlignment, ShieldPool);
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

	ClearDamageFeedback();
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
	TSubclassOf<UDamageFeedbackIndicator> IndicatorClass,
	const int32 PoolSize,
	const FVector2D& Scale,
	const FVector2D& Distance,
	const FVector2D& Alignment,
	TArray<TObjectPtr<UDamageFeedbackIndicator>>& OutPool)
{
	if (!FeedbackCanvas || !IndicatorClass)
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
		if (UCanvasPanelSlot* CanvasSlot = FeedbackCanvas->AddChildToCanvas(Indicator))
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
