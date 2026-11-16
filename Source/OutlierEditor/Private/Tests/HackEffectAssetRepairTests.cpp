#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "BaseWidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "UI/BreathingWidget.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHackEffectTimerSourceAssetTest,
	"Outlier.UI.Enemy.HackEffectTimerSourceAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHackEffectTimerSourceAssetTest::RunTest(const FString& Parameters)
{
	UBaseWidgetBlueprint* Blueprint = LoadObject<UBaseWidgetBlueprint>(nullptr,
		TEXT("/Game/Blueprints/Widget/Enemy/WBP_HackEffectTimer.WBP_HackEffectTimer"));
	if (!TestNotNull(TEXT("Canonical timer blueprint loads"), Blueprint)
		|| !TestNotNull(TEXT("Timer source widget tree exists"), Blueprint->WidgetTree.Get()))
	{
		return false;
	}
	UWidgetTree* Tree = Blueprint->WidgetTree;
	Tree->ForEachWidget([this](UWidget* Widget)
	{
		AddInfo(FString::Printf(TEXT("Timer source widget: Name=%s Class=%s Parent=%s"),
			*Widget->GetName(), *Widget->GetClass()->GetPathName(), *GetNameSafe(Widget->GetParent())));
	});
	UWidget* Frame = Tree->FindWidget(TEXT("TimerFrame"));
	UCanvasPanel* Canvas = Frame ? Cast<UCanvasPanel>(Frame->GetParent()) : nullptr;
	UCanvasPanelSlot* FrameSlot = Frame ? Cast<UCanvasPanelSlot>(Frame->Slot) : nullptr;
	if (!TestNotNull(TEXT("Existing timer frame belongs to a canvas"), Canvas)
		|| !TestNotNull(TEXT("Existing timer frame has canvas layout"), FrameSlot))
	{
		return false;
	}
	UWidget* NamedPulse = Tree->FindWidget(TEXT("HackEffectPulseWidget"));
	UBreathingWidget* Pulse = Cast<UBreathingWidget>(NamedPulse);
	const bool bRepair = FParse::Param(FCommandLine::Get(), TEXT("OutlierRepairHackEffectTimer"));
	if (bRepair && !NamedPulse)
	{
		// 일반 자동화 실행은 읽기 전용이다. 명시적 복구 실행에서만 누락된 자식을 추가한다.
		if (!TestFalse(TEXT("Repair preserves pre-existing unsaved timer edits"), Blueprint->GetOutermost()->IsDirty()))
		{
			return false;
		}
		UClass* BreathingClass = LoadClass<UBreathingWidget>(nullptr,
			TEXT("/Game/Blueprints/Widget/WBP_Breathing.WBP_Breathing_C"));
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/Material/Widget/MI_UI_Breathing_RoundedRectangle.MI_UI_Breathing_RoundedRectangle"));
		if (!TestNotNull(TEXT("Reusable breathing blueprint loads"), BreathingClass)
			|| !TestNotNull(TEXT("Reusable breathing material loads"), Material))
		{
			return false;
		}
		// WidgetBlueprint의 변수 추가 콜백은 새 자식마다 GUID를 등록한다.
		// UMGEditor 의존성을 추가하지 않고 같은 저장 계약을 리플렉션으로 따른다.
		FMapProperty* VariableGuids = FindFProperty<FMapProperty>(Blueprint->GetClass(), TEXT("WidgetVariableNameToGuidMap"));
		const FStructProperty* GuidValue = VariableGuids ? CastField<FStructProperty>(VariableGuids->ValueProp) : nullptr;
		if (!TestTrue(TEXT("Widget blueprint exposes the expected name-to-GUID map"),
			VariableGuids && CastField<FNameProperty>(VariableGuids->KeyProp)
			&& GuidValue && GuidValue->Struct == TBaseStructure<FGuid>::Get()))
		{
			return false;
		}
		Blueprint->Modify();
		Tree->Modify();
		Canvas->Modify();
		// Designer 자식 템플릿만 생성한다. 런타임 초기화는 WidgetTree 복제 시 기존 UMG 흐름이 수행한다.
		Pulse = NewObject<UBreathingWidget>(Tree, BreathingClass, TEXT("HackEffectPulseWidget"), RF_Transactional);
		Pulse->BreathingMaterial = Material;
		Pulse->Shape = EBreathingShape::Ellipse;
		Pulse->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (FBoolProperty* Variable = FindFProperty<FBoolProperty>(UWidget::StaticClass(), TEXT("bIsVariable")))
		{
			Variable->SetPropertyValue_InContainer(Pulse, true);
		}
		UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Pulse);
		if (!TestNotNull(TEXT("Breathing child joins the existing timer canvas"), Slot))
		{
			return false;
		}
		// 프레임과 숫자는 그대로 두고 숫자 뒤 배경만 추가한다. 배경은 모든 기존 요소보다 먼저 그린다.
		Slot->SetPosition(FVector2D(33.0, 69.0));
		Slot->SetSize(FVector2D(250.0, 50.0));
		Slot->SetAutoSize(false);
		int32 LowestOrder = FrameSlot->GetZOrder();
		for (UWidget* Child : Canvas->GetAllChildren())
		{
			if (Child != Pulse)
			{
				if (UCanvasPanelSlot* ChildSlot = Cast<UCanvasPanelSlot>(Child->Slot))
				{
					LowestOrder = FMath::Min(LowestOrder, ChildSlot->GetZOrder());
				}
			}
		}
		Slot->SetZOrder(LowestOrder - 1);
		FScriptMapHelper GuidMap(VariableGuids, VariableGuids->ContainerPtrToValuePtr<void>(Blueprint));
		const FName VariableName = Pulse->GetFName();
		if (GuidMap.FindMapIndexWithKey(&VariableName) == INDEX_NONE)
		{
			const FGuid VariableGuid = FGuid::NewGuid();
			GuidMap.AddPair(&VariableName, &VariableGuid);
		}
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		if (!TestTrue(TEXT("Repaired timer blueprint compiles"), Blueprint->Status != BS_Error))
		{
			return false;
		}
		const FString Filename = FPackageName::LongPackageNameToFilename(
			Blueprint->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		if (!TestTrue(TEXT("Save only the canonical repaired timer asset"),
			UPackage::SavePackage(Blueprint->GetOutermost(), Blueprint, *Filename, Args)))
		{
			return false;
		}
	}
	if (!TestNotNull(TEXT("Timer source binds the reusable breathing widget"), Pulse))
	{
		return false;
	}
	TestNotNull(TEXT("Timer source breathing material is assigned"), Pulse->BreathingMaterial.Get());
	TestEqual(TEXT("Timer source uses ellipse breathing shape"), Pulse->Shape, EBreathingShape::Ellipse);
	UCanvasPanelSlot* PulseSlot = Cast<UCanvasPanelSlot>(Pulse->Slot);
	if (TestNotNull(TEXT("Breathing source has canvas layout"), PulseSlot))
	{
		TestTrue(TEXT("Breathing source has positive size"), PulseSlot->GetSize().X > 0.0 && PulseSlot->GetSize().Y > 0.0);
		TestTrue(TEXT("Breathing source draws behind frame"), PulseSlot->GetZOrder() < FrameSlot->GetZOrder());
	}
	return !HasAnyErrors();
}

#endif
