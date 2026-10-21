#if WITH_DEV_AUTOMATION_TESTS

#include "UI/UpgradeNodeGroupWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Upgrade/OutlierUpgradeComponent.h"
#include "UI/UpgradeNodeWidget.h"

// 입력/구매 권한 테스트와 분리해 실제 PS 통보 -> 연결선 MID 갱신 경로를 검증한다.
struct FUpgradeConnectionLineTestAccess
{
	static UUpgradeNodeGroupWidget* MakeGroup(AOutlierPlayerState* State, UMaterialInterface* Material)
	{
		UUpgradeNodeGroupWidget* Group = NewObject<UUpgradeNodeGroupWidget>();
		Group->WidgetTree = NewObject<UWidgetTree>(Group);
		UCanvasPanel* Canvas = Group->WidgetTree->ConstructWidget<UCanvasPanel>();
		Group->WidgetTree->RootWidget = Canvas;
		Group->NodeWidgetHost = Canvas;
		Group->PlayerState = State;
		Group->LineFillMaterial = Material;
		Group->LineFillColor = FLinearColor(0.2f, 0.4f, 0.8f);
		Group->LineFillDuration = 1.0f;

		UDataTable* Table = NewObject<UDataTable>(Group);
		Table->RowStruct = FOutlierUpgradeNodeRow::StaticStruct();
		FOutlierUpgradeNodeRow Row;
		Row.Role = EOutlierUpgradeRole::Shooter;
		Row.bInitiallyActivated = true;
		Table->AddRow(TEXT("Root"), Row);
		Row.bInitiallyActivated = false;
		Row.ParentId = TEXT("Root");
		Table->AddRow(TEXT("A"), Row);
		Table->AddRow(TEXT("B"), Row);
		Row.ParentId = TEXT("A");
		Table->AddRow(TEXT("EndA"), Row);
		Table->AddRow(TEXT("EndA2"), Row);
		Row.ParentId = TEXT("B");
		Table->AddRow(TEXT("EndB"), Row);
		Group->UpgradeDataTable = Table;
		Group->UpgradeComponent = NewObject<UOutlierUpgradeComponent>(Group);
		Group->UpgradeComponent->SetUpgradeRole(EOutlierUpgradeRole::Shooter);
		Group->UpgradeComponent->SetUpgradeDataTable(Table);

		const FName NodeRows[] = { TEXT("Root"), TEXT("A"), TEXT("B"), TEXT("EndA"), TEXT("EndA2"), TEXT("EndB") };
		for (const FName NodeRow : NodeRows)
		{
			const FName WidgetName(*FString::Printf(TEXT("Node_%s"), *NodeRow.ToString()));
			UUpgradeNodeWidget* Node = NewObject<UUpgradeNodeWidget>(Group->WidgetTree, WidgetName);
			Node->NodeRowName = NodeRow;
			Canvas->AddChild(Node);
		}
		const FName Sources[] = { TEXT("Node_A"), TEXT("Node_A"), TEXT("Node_B"), TEXT("Node_Root") };
		const FName Destinations[] = { TEXT("Node_EndA"), TEXT("Node_EndA2"), TEXT("Node_EndB"), TEXT("Node_A") };
		for (int32 Index = 0; Index < 4; ++Index)
		{
			FUpgradeLineBinding Binding;
			Binding.ImageName = FName(*FString::Printf(TEXT("Line%d"), Index));
			Binding.SourceNodeWidgetName = Sources[Index];
			Binding.DestinationNodeWidgetName = Destinations[Index];
			Binding.LineTexture = NewObject<UTexture2D>(Group);
			Binding.bUseHorizontal = Index == 1;
			Binding.bReverse = Index == 2;
			UImage* Image = Group->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), Binding.ImageName);
			Image->SetBrushFromTexture(Binding.LineTexture);
			Canvas->AddChild(Image);
			Group->ConnectionLines.Add(Binding);
		}
		Group->BindUpgradePresentationChanged();
		Group->RebuildConnectionLines();
		return Group;
	}

	static const TArray<FUpgradeLineRuntime>& Lines(UUpgradeNodeGroupWidget* Group) { return Group->RuntimeLines; }
	static bool Advance(UUpgradeNodeGroupWidget* Group, float Seconds) { return Group->AdvanceLineFills(Seconds); }
	static void Refresh(UUpgradeNodeGroupWidget* Group) { Group->RefreshConnectionLines(false); }
	static void Destruct(UUpgradeNodeGroupWidget* Group) { Group->NativeDestruct(); }
	static bool HasTicker(UUpgradeNodeGroupWidget* Group) { return Group->LineFillTickerHandle.IsValid(); }
	static TArray<FName> ImageOptions(UUpgradeNodeGroupWidget* Group) { return Group->GetLineImageOptions(); }
	static TArray<FName> NodeOptions(UUpgradeNodeGroupWidget* Group) { return Group->GetLineNodeOptions(); }
	static void Hide(UUpgradeNodeGroupWidget* Group)
	{
		Group->SetVisibility(ESlateVisibility::Collapsed);
		Group->TickLineFills(0.0f);
	}
	static float Scalar(UMaterialInstanceDynamic* Material, FName Name)
	{
		float Value = -1.0f;
		Material->GetScalarParameterValue(FHashedMaterialParameterInfo(Name), Value);
		return Value;
	}
	static void ReceiveNodes(AOutlierPlayerState* State, const TArray<FName>& Rows)
	{
		State->SetRole(ROLE_SimulatedProxy);
		State->ShooterActivatedUpgradeNodeIds = Rows;
		State->OnRep_ActivatedUpgradeNodes();
	}
	static void ReceiveRestore(AOutlierPlayerState* State)
	{
		++State->ShooterUpgradeRestoreRevision;
		State->OnRep_UpgradeRestoreRevision();
	}
};

namespace UpgradeConnectionLineTests
{
	struct FFixture
	{
		UWorld* World = nullptr;
		AOutlierPlayerState* State = nullptr;
		UUpgradeNodeGroupWidget* Group = nullptr;

		FFixture()
		{
			const FName Name = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("UpgradeLineTestWorld"));
			World = UWorld::CreateWorld(EWorldType::Game, false, Name, GetTransientPackage());
			if (World)
			{
				World->AddToRoot();
				if (GEngine) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
				State = World->SpawnActor<AOutlierPlayerState>();
			}
		}
		~FFixture()
		{
			if (Group) FUpgradeConnectionLineTestAccess::Destruct(Group);
			if (World)
			{
				World->DestroyWorld(false);
				if (GEngine) GEngine->DestroyWorldContext(World);
				World->RemoveFromRoot();
			}
		}
		bool Initialize()
		{
			UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Material/Widget/M_UI_LineFill.M_UI_LineFill"));
			if (!State || !Material) return false;
			Group = FUpgradeConnectionLineTestAccess::MakeGroup(State, Material);
			return FUpgradeConnectionLineTestAccess::Lines(Group).Num() == 4;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUpgradeLinePurchaseTest, "Outlier.UI.Upgrade.Lines.PurchaseAndBranches",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUpgradeLinePurchaseTest::RunTest(const FString& Parameters)
{
	using Access = FUpgradeConnectionLineTestAccess;
	UpgradeConnectionLineTests::FFixture Context;
	if (!TestTrue(TEXT("World, line material and four mappings are available"), Context.Initialize())) return false;
	const auto& Lines = Access::Lines(Context.Group);
	TestEqual(TEXT("Initial base-loadout connection is full"), Lines[3].Progress, 1.0f);
	TestEqual(TEXT("Unpurchased source keeps its base line visible"), Lines[0].Image->GetRenderOpacity(), 1.0f);
	TestTrue(TEXT("Every line has an independent MID"), Lines[0].Material != Lines[1].Material);
	Context.State->AddActivatedUpgradeNode(EOutlierUpgradeRole::Shooter, TEXT("A"));
	TestTrue(TEXT("Authoritative purchase starts outgoing lines"), Lines[0].bAnimating && Lines[1].bAnimating);
	Access::Advance(Context.Group, 0.25f);
	TestEqual(TEXT("Progress follows duration"), Lines[0].Progress, 0.25f);
	TestEqual(TEXT("Both outgoing branches advance"), Lines[1].Progress, 0.25f);
	TestEqual(TEXT("Fill animation preserves the base line opacity"), Lines[0].Image->GetRenderOpacity(), 1.0f);
	TestEqual(TEXT("Unrelated source stays dark"), Lines[2].Progress, 0.0f);
	Access::Refresh(Context.Group);
	TestEqual(TEXT("Duplicate refresh cannot restart animation"), Lines[0].Progress, 0.25f);
	TestFalse(TEXT("Destination is not auto-purchased"), Context.State->GetActivatedUpgradeNodeIds(EOutlierUpgradeRole::Shooter).Contains(TEXT("EndA")));
	TestFalse(TEXT("Large UI delta completes all lines"), Access::Advance(Context.Group, 2.0f));
	TestEqual(TEXT("Completion clamps at one"), Lines[0].Progress, 1.0f);
	TestEqual(TEXT("Progress reaches the actual material"), Access::Scalar(Lines[0].Material, TEXT("Progress")), 1.0f);
	TestEqual(TEXT("Horizontal parameter is forwarded"), Access::Scalar(Lines[1].Material, TEXT("UseHorizontal")), 1.0f);
	TestEqual(TEXT("Reverse parameter is forwarded"), Access::Scalar(Lines[2].Material, TEXT("Reverse")), 1.0f);
	UTexture* ActualTexture = nullptr;
	Lines[0].Material->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("LineTexture")), ActualTexture);
	TestTrue(TEXT("Texture reaches the actual material"), ActualTexture == Context.Group->ConnectionLines[0].LineTexture);
	for (const FUpgradeLineRuntime& Line : Lines)
	{
		FLinearColor ActualColor = FLinearColor::Transparent;
		Line.Material->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("FillColor")), ActualColor);
		TestTrue(TEXT("Every line uses the shared fill color"), ActualColor.Equals(Context.Group->LineFillColor));
	}
	Context.Group->LineFillColor = FLinearColor(0.8f, 0.3f, 0.1f);
	Context.Group->RebuildConnectionLines();
	for (const FUpgradeLineRuntime& Line : Access::Lines(Context.Group))
	{
		FLinearColor ActualColor = FLinearColor::Transparent;
		Line.Material->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("FillColor")), ActualColor);
		TestTrue(TEXT("Rebuild applies the changed shared color to every line"), ActualColor.Equals(Context.Group->LineFillColor));
	}
	TestEqual(TEXT("Reopen/rebuild displays purchased source immediately"), Access::Lines(Context.Group)[0].Progress, 1.0f);
	TestFalse(TEXT("Initial display needs no ticker"), Access::HasTicker(Context.Group));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUpgradeLineMappingTest, "Outlier.UI.Upgrade.Lines.MappingAndDuration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUpgradeLineMappingTest::RunTest(const FString& Parameters)
{
	using Access = FUpgradeConnectionLineTestAccess;
	UpgradeConnectionLineTests::FFixture Context;
	if (!TestTrue(TEXT("Line fixture is configured"), Context.Initialize())) return false;
	// 존재하는 노드라도 실제 부모-자식 구간이 아니면 다른 가지를 밝히지 못하게 거절한다.
	Context.Group->ConnectionLines[0].DestinationNodeWidgetName = TEXT("Node_EndB");
	AddExpectedErrorPlain(TEXT("[Upgrade.Line] Invalid image/texture/parent mapping: Line0"), EAutomationExpectedErrorFlags::Contains, 1);
	Context.Group->RebuildConnectionLines();
	TestEqual(TEXT("Wrong branch mapping is rejected"), Access::Lines(Context.Group).Num(), 3);
	Context.Group->ConnectionLines[0].DestinationNodeWidgetName = NAME_None;
	AddExpectedErrorPlain(TEXT("[Upgrade.Line] Invalid image/texture/parent mapping: Line0"), EAutomationExpectedErrorFlags::Contains, 1);
	Context.Group->RebuildConnectionLines();
	TestEqual(TEXT("Unselected node is rejected"), Access::Lines(Context.Group).Num(), 3);
	Context.Group->ConnectionLines[0].DestinationNodeWidgetName = TEXT("Node_EndA");
	Context.Group->ConnectionLines.Add(Context.Group->ConnectionLines[0]);
	AddExpectedErrorPlain(TEXT("[Upgrade.Line] Invalid image/texture/parent mapping: Line0"), EAutomationExpectedErrorFlags::Contains, 1);
	Context.Group->RebuildConnectionLines();
	TestEqual(TEXT("One Image cannot share multiple runtime MIDs"), Access::Lines(Context.Group).Num(), 4);
	Context.Group->LineFillDuration = 0.0f;
	Context.State->AddActivatedUpgradeNode(EOutlierUpgradeRole::Shooter, TEXT("A"));
	TestEqual(TEXT("Zero duration immediately completes"), Access::Lines(Context.Group)[0].Progress, 1.0f);
	TestFalse(TEXT("Zero duration creates no ticker"), Access::HasTicker(Context.Group));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUpgradeLineNoSourceTest, "Outlier.UI.Upgrade.Lines.NoSourceAlwaysFilled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUpgradeLineNoSourceTest::RunTest(const FString& Parameters)
{
	using Access = FUpgradeConnectionLineTestAccess;
	UpgradeConnectionLineTests::FFixture Context;
	if (!TestTrue(TEXT("Line fixture is configured"), Context.Initialize())) return false;
	FUpgradeLineBinding& Binding = Context.Group->ConnectionLines[0];
	Binding.SourceNodeWidgetName = NAME_None;
	Binding.DestinationNodeWidgetName = TEXT("Node_A");
	Context.Group->RebuildConnectionLines();
	if (!TestEqual(TEXT("None source is accepted"), Access::Lines(Context.Group).Num(), 4)) return false;
	TestEqual(TEXT("Entry line starts fully filled before purchase"), Access::Lines(Context.Group)[0].Progress, 1.0f);
	TestEqual(TEXT("Entry line material starts fully filled"), Access::Scalar(Access::Lines(Context.Group)[0].Material, TEXT("Progress")), 1.0f);
	TestFalse(TEXT("Initial entry line needs no animation"), Access::Lines(Context.Group)[0].bAnimating);
	TestEqual(TEXT("Base line remains visible"), Access::Lines(Context.Group)[0].Image->GetRenderOpacity(), 1.0f);
	Context.State->AddActivatedUpgradeNode(EOutlierUpgradeRole::Shooter, TEXT("B"));
	TestFalse(TEXT("Unrelated purchase does not start entry line"), Access::Lines(Context.Group)[0].bAnimating);
	Context.State->AddActivatedUpgradeNode(EOutlierUpgradeRole::Shooter, TEXT("A"));
	TestFalse(TEXT("Destination purchase does not restart entry line"), Access::Lines(Context.Group)[0].bAnimating);
	Access::Advance(Context.Group, 0.5f);
	TestEqual(TEXT("Entry line remains full during other animations"), Access::Lines(Context.Group)[0].Progress, 1.0f);
	Access::Advance(Context.Group, 0.5f);
	TestEqual(TEXT("Entry line completes"), Access::Lines(Context.Group)[0].Progress, 1.0f);
	Context.Group->RebuildConnectionLines();
	TestEqual(TEXT("Purchased entry line stays full after rebuild"), Access::Lines(Context.Group)[0].Progress, 1.0f);
	Binding.SourceNodeWidgetName = TEXT("MissingNode");
	AddExpectedErrorPlain(TEXT("[Upgrade.Line] Invalid image/texture/parent mapping: Line0"), EAutomationExpectedErrorFlags::Contains, 1);
	Context.Group->RebuildConnectionLines();
	TestEqual(TEXT("Invalid source is not treated as None"), Access::Lines(Context.Group).Num(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUpgradeLinePathProgressTest, "Outlier.UI.Upgrade.Lines.PathProgressAndSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUpgradeLinePathProgressTest::RunTest(const FString& Parameters)
{
	using Access = FUpgradeConnectionLineTestAccess;
	UpgradeConnectionLineTests::FFixture Context;
	if (!TestTrue(TEXT("Line fixture is configured"), Context.Initialize())) return false;
	TestEqual(TEXT("Existing lines use axis fill by default"),
		Access::Scalar(Access::Lines(Context.Group)[0].Material, TEXT("UsePathProgress")), 0.0f);

	Context.Group->LineFillSpeed = 100.0f;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FUpgradeLineBinding& Binding = Context.Group->ConnectionLines[Index];
		Binding.PathProgressTexture = NewObject<UTexture2D>(Context.Group);
		Binding.PathProgressTexture->SRGB = false;
		Binding.LineLength = Index == 0 ? 100.0f : 200.0f;
	}
	Context.Group->RebuildConnectionLines();
	const TArray<FUpgradeLineRuntime>& Lines = Access::Lines(Context.Group);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		TestEqual(TEXT("A path texture selects path fill"),
			Access::Scalar(Lines[Index].Material, TEXT("UsePathProgress")), 1.0f);
		UTexture* ActualTexture = nullptr;
		Lines[Index].Material->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("PathProgressTexture")), ActualTexture);
		TestTrue(TEXT("Each line receives its own path texture"),
			ActualTexture == Context.Group->ConnectionLines[Index].PathProgressTexture);
	}
	TestEqual(TEXT("A neighboring line without a path texture keeps axis fill"),
		Access::Scalar(Lines[2].Material, TEXT("UsePathProgress")), 0.0f);

	// 같은 시간에 같은 거리만큼 이동하되, 각 선 전체의 진행도는 길이에 따라 달라진다.
	Context.State->AddActivatedUpgradeNode(EOutlierUpgradeRole::Shooter, TEXT("A"));
	Context.State->AddActivatedUpgradeNode(EOutlierUpgradeRole::Shooter, TEXT("B"));
	Access::Advance(Context.Group, 0.25f);
	TestEqual(TEXT("100-unit line advances by 25 percent"), Lines[0].Progress, 0.25f);
	TestEqual(TEXT("200-unit line advances by 12.5 percent"), Lines[1].Progress, 0.125f);
	TestEqual(TEXT("Both paths cover the same distance"), Lines[0].Progress * 100.0f, Lines[1].Progress * 200.0f);
	TestEqual(TEXT("A line without length uses the existing duration"), Lines[2].Progress, 0.25f);
	Access::Advance(Context.Group, -1.0f);
	TestEqual(TEXT("Negative delta cannot rewind a path"), Lines[0].Progress, 0.25f);
	Context.Group->LineFillDuration = 0.0f;
	Access::Refresh(Context.Group);
	TestEqual(TEXT("Speed mode is independent of fallback duration"), Lines[0].Progress, 0.25f);
	TestEqual(TEXT("Zero fallback duration immediately finishes an unmeasured line"), Lines[2].Progress, 1.0f);
	Access::Advance(Context.Group, 0.75f);
	TestEqual(TEXT("Shorter path completes first"), Lines[0].Progress, 1.0f);
	TestEqual(TEXT("Longer path is halfway at the same time"), Lines[1].Progress, 0.5f);
	TestFalse(TEXT("All paths eventually finish"), Access::Advance(Context.Group, 1.0f));
	TestEqual(TEXT("Long path clamps at one"), Lines[1].Progress, 1.0f);
	Context.State->RestoreCheckpointProgress(0, EOutlierUpgradeRole::Shooter, { TEXT("A") });
	TestEqual(TEXT("Restore also completes a distance-driven path immediately"), Lines[1].Progress, 1.0f);
	TestFalse(TEXT("Restore stops distance-driven animation"), Access::HasTicker(Context.Group));

	// 경로 지정을 해제하거나 공통 속도를 끄면 기존 머티리얼 선택/시간 설정으로 돌아간다.
	Context.State->FlushActivatedUpgradeNodes(0);
	Context.Group->ConnectionLines[0].PathProgressTexture = nullptr;
	Context.Group->LineFillSpeed = 0.0f;
	Context.Group->LineFillDuration = 1.0f;
	Context.Group->RebuildConnectionLines();
	TestEqual(TEXT("Removing a path texture restores axis fill"),
		Access::Scalar(Access::Lines(Context.Group)[0].Material, TEXT("UsePathProgress")), 0.0f);
	Context.State->AddActivatedUpgradeNode(EOutlierUpgradeRole::Shooter, TEXT("A"));
	Access::Advance(Context.Group, 0.25f);
	TestEqual(TEXT("Disabled speed restores duration for the shorter line"), Access::Lines(Context.Group)[0].Progress, 0.25f);
	TestEqual(TEXT("Disabled speed restores duration for the longer line"), Access::Lines(Context.Group)[1].Progress, 0.25f);
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUpgradeLineAutoLengthTest, "Outlier.UI.Upgrade.Lines.WidgetReferencesAndAutoLength",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUpgradeLineAutoLengthTest::RunTest(const FString& Parameters)
{
	using Access = FUpgradeConnectionLineTestAccess;
	UpgradeConnectionLineTests::FFixture Context;
	if (!TestTrue(TEXT("Line fixture is configured"), Context.Initialize())) return false;
	UCanvasPanel* Canvas = CastChecked<UCanvasPanel>(Context.Group->WidgetTree->RootWidget);
	UUpgradeNodeWidget* Source = NewObject<UUpgradeNodeWidget>(Context.Group->WidgetTree, TEXT("SourceNode"));
	UUpgradeNodeWidget* Destination = NewObject<UUpgradeNodeWidget>(Context.Group->WidgetTree, TEXT("DestinationNode"));
	Source->NodeRowName = TEXT("A");
	Destination->NodeRowName = TEXT("EndA");
	Canvas->AddChild(Source);
	Canvas->AddChild(Destination);
	FUpgradeLineBinding& Binding = Context.Group->ConnectionLines[0];
	Binding.SourceNodeWidgetName = Source->GetFName();
	Binding.DestinationNodeWidgetName = Destination->GetFName();
	TestTrue(TEXT("Image selection lists local images"), Access::ImageOptions(Context.Group).Contains(Binding.ImageName));
	TestFalse(TEXT("Image selection excludes node widgets"), Access::ImageOptions(Context.Group).Contains(Source->GetFName()));
	TestTrue(TEXT("Node selection lists local upgrade nodes"), Access::NodeOptions(Context.Group).Contains(Source->GetFName()));
	TestFalse(TEXT("Node selection excludes images"), Access::NodeOptions(Context.Group).Contains(Binding.ImageName));

	// 여백이 있고 알파가 77뿐인 직선. 이미지 대각선이나 고정 알파 임계값으로 측정하면 실패한다.
	TArray<uint8> Pixels;
	Pixels.SetNumZeroed(100 * 60 * 4);
	for (int32 X = 20; X < 80; ++X) Pixels[(30 * 100 + X) * 4 + 3] = 77;
	Binding.LineTexture->Source.Init(100, 60, 1, 1, TSF_BGRA8, Pixels.GetData());
	UImage* Image = CastChecked<UImage>(Context.Group->GetWidgetFromName(Binding.ImageName));
	UCanvasPanelSlot* Slot = CastChecked<UCanvasPanelSlot>(Image->Slot);
	Slot->SetSize(FVector2D(200, 120));
	Context.Group->LineFillSpeed = 177.0f;
	Context.Group->RebuildConnectionLines();
	if (!TestEqual(TEXT("Widget references resolve valid parent-child rows"), Access::Lines(Context.Group).Num(), 4)) return false;
	TestEqual(TEXT("Selected source resolves its data row"), Access::Lines(Context.Group)[0].ActivationNodeRowName, FName(TEXT("A")));
	TestTrue(TEXT("Transparent margins are excluded and UMG size is applied"),
		FMath::IsNearlyEqual(Access::Lines(Context.Group)[0].Length, 118.0f, 0.01f));
	TestTrue(TEXT("Measured UV extent is stored for cooked runtime"),
		Binding.MeasuredTexture == Binding.LineTexture && !Binding.MeasuredLineExtent.IsNearlyZero());
	Slot->SetSize(FVector2D(300, 120));
	Context.State->AddActivatedUpgradeNode(EOutlierUpgradeRole::Shooter, TEXT("A"));
	TestTrue(TEXT("Purchase measures the latest arranged size"),
		FMath::IsNearlyEqual(Access::Lines(Context.Group)[0].Length, 177.0f, 0.01f));
	Access::Advance(Context.Group, 0.5f);
	TestTrue(TEXT("Auto length drives speed-based duration"),
		FMath::IsNearlyEqual(Access::Lines(Context.Group)[0].Progress, 0.5f, 0.001f));
	Binding.LineLength = 42.0f;
	Context.Group->RebuildConnectionLines();
	TestEqual(TEXT("Explicit length still overrides automatic measurement"), Access::Lines(Context.Group)[0].Length, 42.0f);
	Binding.SourceNodeWidgetName = TEXT("MissingNode");
	AddExpectedErrorPlain(TEXT("[Upgrade.Line] Invalid image/texture/parent mapping: Line0"), EAutomationExpectedErrorFlags::Contains, 1);
	Context.Group->RebuildConnectionLines();
	TestEqual(TEXT("Missing node widget is rejected"), Access::Lines(Context.Group).Num(), 3);
	return true;
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUpgradeLineRestoreTest, "Outlier.UI.Upgrade.Lines.RestoreAndCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUpgradeLineRestoreTest::RunTest(const FString& Parameters)
{
	using Access = FUpgradeConnectionLineTestAccess;
	UpgradeConnectionLineTests::FFixture Context;
	if (!TestTrue(TEXT("Line fixture is configured"), Context.Initialize())) return false;
	Context.State->RestoreCheckpointProgress(0, EOutlierUpgradeRole::Shooter, { TEXT("A") });
	TestEqual(TEXT("Restored activation is immediately full"), Access::Lines(Context.Group)[0].Progress, 1.0f);
	TestFalse(TEXT("Restore does not animate"), Access::HasTicker(Context.Group));
	Context.State->FlushActivatedUpgradeNodes(0);
	TestEqual(TEXT("Reset clears purchased connections"), Access::Lines(Context.Group)[0].Progress, 0.0f);
	Context.State->AddActivatedUpgradeNode(EOutlierUpgradeRole::Shooter, TEXT("A"));
	Access::Advance(Context.Group, 0.2f);
	Context.State->RestoreCheckpointProgress(0, EOutlierUpgradeRole::Shooter, { TEXT("A") });
	TestEqual(TEXT("Same-list restore finishes an in-flight animation"), Access::Lines(Context.Group)[0].Progress, 1.0f);
	TestFalse(TEXT("Same-list restore stops the ticker"), Access::HasTicker(Context.Group));
	Context.State->AddActivatedUpgradeNode(EOutlierUpgradeRole::Shooter, TEXT("B"));
	TestTrue(TEXT("Next purchase starts its own ticker"), Access::HasTicker(Context.Group));
	Access::Hide(Context.Group);
	TestFalse(TEXT("Hidden screen stops animation work"), Access::HasTicker(Context.Group));
	TestEqual(TEXT("Hidden screen settles to authoritative state"), Access::Lines(Context.Group)[2].Progress, 1.0f);
	Context.Group->SetVisibility(ESlateVisibility::Visible);
	Context.State->FlushActivatedUpgradeNodes(0);
	Context.State->AddActivatedUpgradeNode(EOutlierUpgradeRole::Shooter, TEXT("B"));
	Context.Group->InjectUpgradeContext(nullptr, nullptr, nullptr, nullptr);
	TestFalse(TEXT("Context replacement cancels the ticker"), Access::HasTicker(Context.Group));
	TestEqual(TEXT("Context replacement drops line runtime"), Access::Lines(Context.Group).Num(), 0);
	Context.State->AddActivatedUpgradeNode(EOutlierUpgradeRole::Shooter, TEXT("EndA"));
	TestEqual(TEXT("Old PS cannot update replaced context"), Access::Lines(Context.Group).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUpgradeLineRepNotifyTest, "Outlier.UI.Upgrade.Lines.ReplicationOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUpgradeLineRepNotifyTest::RunTest(const FString& Parameters)
{
	using Access = FUpgradeConnectionLineTestAccess;
	UpgradeConnectionLineTests::FFixture Context;
	if (!TestTrue(TEXT("Line fixture is configured"), Context.Initialize())) return false;
	Access::ReceiveNodes(Context.State, { TEXT("A") });
	TestEqual(TEXT("Intermediate RepNotify cannot start presentation"), Access::Lines(Context.Group)[0].Progress, 0.0f);
	Access::ReceiveRestore(Context.State);
	Context.State->PostRepNotifies();
	TestEqual(TEXT("Nodes-before-revision restore snaps to full"), Access::Lines(Context.Group)[0].Progress, 1.0f);
	TestFalse(TEXT("Restored client has no ticker"), Access::HasTicker(Context.Group));
	Access::ReceiveNodes(Context.State, {});
	Context.State->PostRepNotifies();
	Access::ReceiveRestore(Context.State);
	Access::ReceiveNodes(Context.State, { TEXT("B") });
	Context.State->PostRepNotifies();
	TestEqual(TEXT("Revision-before-nodes restore also snaps to full"), Access::Lines(Context.Group)[2].Progress, 1.0f);
	Access::ReceiveNodes(Context.State, { TEXT("A"), TEXT("B") });
	Context.State->PostRepNotifies();
	TestTrue(TEXT("Ordinary replicated purchase animates"), Access::Lines(Context.Group)[0].bAnimating);
	Context.State->PostRepNotifies();
	Access::Advance(Context.Group, 0.3f);
	Access::ReceiveNodes(Context.State, { TEXT("A"), TEXT("B") });
	Context.State->PostRepNotifies();
	TestEqual(TEXT("Repeated replication preserves progress"), Access::Lines(Context.Group)[0].Progress, 0.3f);
	UImage* Image = Access::Lines(Context.Group)[0].Image;
	Access::Destruct(Context.Group);
	TestFalse(TEXT("Destruction stops active work"), Access::HasTicker(Context.Group));
	TestTrue(TEXT("Destruction unbinds presentation"), !Context.State->OnUpgradePresentationChanged.IsBound());
	TestTrue(TEXT("Destruction restores the original brush"),
		Image->GetBrush().GetResourceObject() == Context.Group->ConnectionLines[0].LineTexture);
	TestEqual(TEXT("Destruction restores the original opacity"), Image->GetRenderOpacity(), 1.0f);
	return true;
}

#endif
