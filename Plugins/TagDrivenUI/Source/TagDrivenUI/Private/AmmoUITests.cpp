#if WITH_DEV_AUTOMATION_TESTS

#include "AmmoUI.h"
#include "Components/CanvasPanel.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Misc/AutomationTest.h"

#include <limits>

namespace AmmoUITests
{
	UAmmoUI* MakeWidget()
	{
		UAmmoUI* Widget = NewObject<UAmmoUI>();
		Widget->CurrentAmmoText = NewObject<UTextBlock>(Widget);
		Widget->MaxAmmoText = NewObject<UTextBlock>(Widget);
		Widget->AmmoCanvas = NewObject<UCanvasPanel>(Widget);
		Widget->OverLoad_Infinity = NewObject<UImage>(Widget);
		Widget->AmmoCanvas->AddChild(Widget->CurrentAmmoText);
		Widget->AmmoCanvas->AddChild(Widget->MaxAmmoText);
		Widget->AmmoCanvas->AddChild(Widget->OverLoad_Infinity);
		return Widget;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAmmoUITextColorBoundariesTest, "Outlier.UI.Ammo.TextColor.Boundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAmmoUITextColorBoundariesTest::RunTest(const FString& Parameters)
{
	UAmmoUI* Widget = AmmoUITests::MakeWidget();
	const FLinearColor MaxTextColor(0.2f, 0.4f, 0.6f, 0.8f);
	Widget->MaxAmmoText->SetColorAndOpacity(FSlateColor(MaxTextColor));

	const auto Check = [this, Widget](int32 Current, int32 Max, const FLinearColor& Expected)
	{
		Widget->SetAmmoState(Current, Max);
		TestEqual(FString::Printf(TEXT("Color at %d/%d"), Current, Max),
			Widget->CurrentAmmoText->GetColorAndOpacity().GetSpecifiedColor(), Expected);
		TestEqual(TEXT("Current state is preserved"), Widget->CurrentAmmo, Current);
		TestEqual(TEXT("Max state is preserved"), Widget->MaxAmmo, Max);
		TestEqual(TEXT("Current text matches state"), Widget->CurrentAmmoText->GetText().ToString(), FText::AsNumber(Current).ToString());
		TestEqual(TEXT("Max text matches state"), Widget->MaxAmmoText->GetText().ToString(), FText::AsNumber(Max).ToString());
	};

	Check(31, 100, Widget->NormalAmmoTextColor);
	Check(30, 100, Widget->LowAmmoTextColor);
	Check(11, 100, Widget->LowAmmoTextColor);
	Check(10, 100, Widget->CriticalAmmoTextColor);
	Check(0, 100, Widget->CriticalAmmoTextColor);
	Check(10, 30, Widget->NormalAmmoTextColor);
	Check(9, 30, Widget->LowAmmoTextColor);
	Check(4, 30, Widget->LowAmmoTextColor);
	Check(3, 30, Widget->CriticalAmmoTextColor);
	Check(3, 7, Widget->NormalAmmoTextColor);
	Check(2, 7, Widget->LowAmmoTextColor);
	Check(1, 7, Widget->LowAmmoTextColor);
	Check(0, 7, Widget->CriticalAmmoTextColor);
	Check(0, 0, Widget->NormalAmmoTextColor);
	Check(0, -1, Widget->NormalAmmoTextColor);
	Check(-1, 100, Widget->CriticalAmmoTextColor);
	Check(101, 100, Widget->NormalAmmoTextColor);
	Check(100, 100, Widget->NormalAmmoTextColor);
	Check(5, 10, Widget->NormalAmmoTextColor);
	Check(5, 50, Widget->CriticalAmmoTextColor);
	Check(5, 100, Widget->CriticalAmmoTextColor);
	TestEqual(TEXT("Max text keeps its authored color"), Widget->MaxAmmoText->GetColorAndOpacity().GetSpecifiedColor(), MaxTextColor);

	Widget->LowAmmoRatio = 0.5f;
	Widget->CriticalAmmoRatio = 0.2f;
	Widget->NormalAmmoTextColor = FLinearColor::Blue;
	Widget->LowAmmoTextColor = FLinearColor::Yellow;
	Widget->CriticalAmmoTextColor = FLinearColor::Green;
	Check(51, 100, FLinearColor::Blue);
	Check(50, 100, FLinearColor::Yellow);
	Check(21, 100, FLinearColor::Yellow);
	Check(20, 100, FLinearColor::Green);
	Widget->LowAmmoRatio = Widget->CriticalAmmoRatio = 0.3f;
	Check(30, 100, FLinearColor::Green);

	// 레거시 단일 탄수 이벤트는 마지막으로 받은 Max를 사용한다.
	Widget->AmmoCountChanged(31);
	TestEqual(TEXT("Legacy event uses cached maximum"), Widget->CurrentAmmoText->GetColorAndOpacity().GetSpecifiedColor(), FLinearColor::Blue);
	Widget->CurrentAmmoText = nullptr;
	Widget->SetAmmoState(0, 100);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAmmoUITextColorInvalidSettingsTest, "Outlier.UI.Ammo.TextColor.InvalidSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAmmoUITextColorInvalidSettingsTest::RunTest(const FString& Parameters)
{
	const float InvalidPairs[][2] = {
		{0.1f, 0.3f}, {1.1f, 0.1f}, {0.3f, -0.1f},
		{std::numeric_limits<float>::quiet_NaN(), 0.1f},
		{0.3f, std::numeric_limits<float>::infinity()}
	};
	AddExpectedError(TEXT("Invalid ammo text ratios"), EAutomationExpectedErrorFlags::Contains, UE_ARRAY_COUNT(InvalidPairs));
	for (const auto& Pair : InvalidPairs)
	{
		UAmmoUI* Widget = AmmoUITests::MakeWidget();
		Widget->LowAmmoRatio = Pair[0];
		Widget->CriticalAmmoRatio = Pair[1];
		Widget->SetAmmoState(30, 100);
		TestEqual(TEXT("Invalid settings fall back to 30% low"), Widget->CurrentAmmoText->GetColorAndOpacity().GetSpecifiedColor(), Widget->LowAmmoTextColor);
		// 연속 갱신에서도 추가 Warning 없이 기본 위험/정상 구간을 사용해야 한다.
		Widget->SetAmmoState(10, 100);
		TestEqual(TEXT("Invalid settings fall back to 10% critical"), Widget->CurrentAmmoText->GetColorAndOpacity().GetSpecifiedColor(), Widget->CriticalAmmoTextColor);
		Widget->SetAmmoState(31, 100);
		TestEqual(TEXT("Fallback restores normal color"), Widget->CurrentAmmoText->GetColorAndOpacity().GetSpecifiedColor(), Widget->NormalAmmoTextColor);
		TestTrue(TEXT("Low setting is not repaired in place"), Widget->LowAmmoRatio == Pair[0]
			|| (FMath::IsNaN(Widget->LowAmmoRatio) && FMath::IsNaN(Pair[0])));
		TestTrue(TEXT("Critical setting is not repaired in place"), Widget->CriticalAmmoRatio == Pair[1]);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAmmoUITextColorInfinityTest, "Outlier.UI.Ammo.TextColor.InfinityReturn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAmmoUITextColorInfinityTest::RunTest(const FString& Parameters)
{
	UAmmoUI* Widget = AmmoUITests::MakeWidget();
	const FLinearColor InfinityColor(0.4f, 0.6f, 0.8f, 1.0f);
	Widget->OverLoad_Infinity->SetColorAndOpacity(InfinityColor);
	Widget->SetAmmoState(3, 30);
	Widget->SetWeaponOverchargeActive(true);
	TestTrue(TEXT("Overcharge hides numbers"), Widget->CurrentAmmoText->GetVisibility() == ESlateVisibility::Collapsed);
	TestTrue(TEXT("Overcharge shows infinity"), Widget->OverLoad_Infinity->GetVisibility() == ESlateVisibility::Visible);
	Widget->SetInfiniteAmmoDisplayActive(true);
	Widget->SetWeaponOverchargeActive(false);
	TestTrue(TEXT("Independent infinite display keeps numbers hidden"), Widget->CurrentAmmoText->GetVisibility() == ESlateVisibility::Collapsed);
	Widget->SetAmmoState(30, 30);
	// 다른 표시 로직이 숨겨진 텍스트를 건드려도 복귀 시 최신 상태를 다시 적용한다.
	Widget->CurrentAmmoText->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
	Widget->SetInfiniteAmmoDisplayActive(false);
	TestTrue(TEXT("Clearing both flags restores numbers"), Widget->CurrentAmmoText->GetVisibility() == ESlateVisibility::Visible);
	TestTrue(TEXT("Clearing both flags hides infinity"), Widget->OverLoad_Infinity->GetVisibility() == ESlateVisibility::Collapsed);
	TestEqual(TEXT("Returning numbers have current normal color"), Widget->CurrentAmmoText->GetColorAndOpacity().GetSpecifiedColor(), Widget->NormalAmmoTextColor);
	TestEqual(TEXT("Infinity color is unchanged"), Widget->OverLoad_Infinity->GetColorAndOpacity(), InfinityColor);
	Widget->SetWeaponOverchargeActive(true);
	Widget->SetAmmoState(0, 30);
	Widget->SetWeaponOverchargeActive(false);
	TestEqual(TEXT("Overcharge exit also restores current critical color"), Widget->CurrentAmmoText->GetColorAndOpacity().GetSpecifiedColor(), Widget->CriticalAmmoTextColor);
	return true;
}

#endif
