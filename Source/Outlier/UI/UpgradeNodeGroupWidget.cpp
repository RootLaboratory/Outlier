#include "UI/UpgradeNodeGroupWidget.h"

#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "Engine/LocalPlayer.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Shooter/ShooterCharacter.h"
#include "UI/UpgradeDescWidget.h"
#include "UI/LocalPlayerUILayerSubsystem.h"
#include "UI/UpgradeNodeWidget.h"
#include "Upgrade/OutlierUpgradeComponent.h"
#include "Upgrade/OutlierUpgradeSetData.h"

#if WITH_EDITOR
namespace UpgradeLineMeasurement
{
	// Part/PartSmall 같은 직선만 측정한다. 꺾인 선을 대각선 길이로 오인하지 않는다.
	bool Measure(UTexture2D* Texture, FVector2D& OutExtent)
	{
		if (!Texture || !Texture->Source.IsValid() || Texture->Source.GetFormat() != TSF_BGRA8)
		{
			return false;
		}
		TArray64<uint8> Pixels;
		if (!Texture->Source.GetMipData(Pixels, 0)) return false;
		const int64 Width = Texture->Source.GetSizeX();
		const int64 Height = Texture->Source.GetSizeY();
		const int64 Count = Width * Height;
		if (Count <= 1 || Pixels.Num() != Count * 4) return false;
		uint8 MaxAlpha = 0;
		for (int64 Index = 0; Index < Count; ++Index)
		{
			MaxAlpha = FMath::Max(MaxAlpha, Pixels[Index * 4 + 3]);
		}
		if (MaxAlpha == 0) return false;
		// 원본 선 자체가 반투명하므로 고정 128 대신 가장 진한 알파의 절반을 기준으로 한다.
		const uint8 Threshold = FMath::Max<uint8>(1, MaxAlpha / 2);
		double N = 0, SumX = 0, SumY = 0, SumXX = 0, SumYY = 0, SumXY = 0;
		for (int64 Index = 0; Index < Count; ++Index)
		{
			if (Pixels[Index * 4 + 3] < Threshold) continue;
			const double X = static_cast<double>(Index % Width);
			const double Y = static_cast<double>(Index / Width);
			++N; SumX += X; SumY += Y; SumXX += X * X; SumYY += Y * Y; SumXY += X * Y;
		}
		if (N < 2) return false;
		const double XX = SumXX / N - FMath::Square(SumX / N);
		const double YY = SumYY / N - FMath::Square(SumY / N);
		const double XY = SumXY / N - SumX * SumY / (N * N);
		const double Spread = FMath::Sqrt(FMath::Square(XX - YY) + 4.0 * XY * XY);
		const double Major = (XX + YY + Spread) * 0.5;
		const double Minor = (XX + YY - Spread) * 0.5;
		if (Major <= 0 || Minor / Major > 0.01) return false;
		const double Angle = FMath::Atan2(2.0 * XY, XX - YY) * 0.5;
		const FVector2D Axis(FMath::Cos(Angle), FMath::Sin(Angle));
		double Min = TNumericLimits<double>::Max();
		double Max = TNumericLimits<double>::Lowest();
		for (int64 Index = 0; Index < Count; ++Index)
		{
			if (Pixels[Index * 4 + 3] < Threshold) continue;
			const double Projection = Axis.X * static_cast<double>(Index % Width)
				+ Axis.Y * static_cast<double>(Index / Width);
			Min = FMath::Min(Min, Projection);
			Max = FMath::Max(Max, Projection);
		}
		OutExtent = FVector2D(FMath::Abs(Axis.X) * (Max - Min) / static_cast<double>(Width),
			FMath::Abs(Axis.Y) * (Max - Min) / static_cast<double>(Height));
		return true;
	}
}

void UUpgradeNodeGroupWidget::UpdateLineMeasurements()
{
	const UWidgetTree* Tree = GetLineWidgetTree();
	for (FUpgradeLineBinding& Binding : ConnectionLines)
	{
		UTexture2D* Texture = Binding.LineTexture;
		if (!Texture && Tree)
		{
			const UImage* Image = Cast<UImage>(Tree->FindWidget(Binding.ImageName));
			Texture = Image ? Cast<UTexture2D>(Image->GetBrush().GetResourceObject()) : nullptr;
		}
		if (Binding.MeasuredTexture != Texture)
		{
			Binding.MeasuredTexture = Texture;
			Binding.MeasuredTextureSourceId.Invalidate();
			Binding.MeasuredLineExtent = FVector2D::ZeroVector;
		}
		if (!Texture || !Texture->Source.IsValid()
			|| Binding.MeasuredTextureSourceId == Texture->Source.GetId()) continue;
		Binding.MeasuredLineExtent = FVector2D::ZeroVector;
		UpgradeLineMeasurement::Measure(Texture, Binding.MeasuredLineExtent);
		Binding.MeasuredTextureSourceId = Texture->Source.GetId();
	}
}

void UUpgradeNodeGroupWidget::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UpdateLineMeasurements();
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void UUpgradeNodeGroupWidget::Serialize(FArchive& Ar)
{
#if WITH_EDITOR
	// 저장/쿠킹 전에 측정값을 함께 기록한다. 실행 중에는 GPU 텍스처를 읽지 않는다.
	if (Ar.IsSaving() && !Ar.IsTransacting()) UpdateLineMeasurements();
#endif
	Super::Serialize(Ar);
}

const UWidgetTree* UUpgradeNodeGroupWidget::GetLineWidgetTree() const
{
	if (WidgetTree && WidgetTree->RootWidget) return WidgetTree;
	const UWidgetBlueprintGeneratedClass* WidgetClass = Cast<UWidgetBlueprintGeneratedClass>(GetClass());
	return WidgetClass ? WidgetClass->GetWidgetTreeArchetype() : nullptr;
}

TArray<FName> UUpgradeNodeGroupWidget::GetLineImageOptions() const
{
	TArray<FName> Options;
	if (const UWidgetTree* Tree = GetLineWidgetTree())
	{
		TArray<UWidget*> Widgets;
		Tree->GetAllWidgets(Widgets);
		for (const UWidget* Widget : Widgets)
		{
			if (Widget->IsA<UImage>()) Options.Add(Widget->GetFName());
		}
	}
	Options.Sort(FNameLexicalLess());
	return Options;
}

TArray<FName> UUpgradeNodeGroupWidget::GetLineNodeOptions() const
{
	TArray<FName> Options = { NAME_None };
	if (const UWidgetTree* Tree = GetLineWidgetTree())
	{
		TArray<UWidget*> Widgets;
		Tree->GetAllWidgets(Widgets);
		for (const UWidget* Widget : Widgets)
		{
			if (Widget->IsA<UUpgradeNodeWidget>()) Options.Add(Widget->GetFName());
		}
	}
	Options.Sort(FNameLexicalLess());
	return Options;
}

FName UUpgradeNodeGroupWidget::ResolveLineNodeRowName(FName WidgetName) const
{
	if (WidgetName.IsNone()) return NAME_None;
	const UUpgradeNodeWidget* Node = Cast<UUpgradeNodeWidget>(GetWidgetFromName(WidgetName));
	if (!Node) return NAME_None;
	return !Node->GetNodeRowName().IsNone() ? Node->GetNodeRowName() : Node->NodeRowName;
}

float UUpgradeNodeGroupWidget::MeasureDisplayedLineLength(const FUpgradeLineRuntime& Line) const
{
	if (!Line.Image || Line.AutoLengthExtent.IsNearlyZero()) return 0.0f;
	FVector2D Size = Line.Image->GetCachedGeometry().GetLocalSize();
	if (Size.X <= 0.0 || Size.Y <= 0.0)
	{
		// 최초 레이아웃 전에는 고정 Canvas 크기, 원래 브러시 크기 순서로 사용한다.
		const UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Line.Image->Slot);
		Size = CanvasSlot && !CanvasSlot->GetAutoSize() ? CanvasSlot->GetSize() : FVector2D::ZeroVector;
		if (Size.X <= 0.0 || Size.Y <= 0.0) Size = Line.OriginalBrush.ImageSize;
	}
	return static_cast<float>((Line.AutoLengthExtent * Size * Line.Image->GetRenderTransform().Scale).Size());
}

void UUpgradeNodeGroupWidget::NativeConstruct()
{
	Super::NativeConstruct();
	CacheNodeWidgets();
	ResetNodeUnlockPresentations();

	BindUpgradeStateChanged();
	BindPlayerStateNodeCountChanged();
	RefreshNodeWidgets();
	BindUpgradePresentationChanged();
	RebuildConnectionLines();
}

void UUpgradeNodeGroupWidget::NativeDestruct()
{
	ResetNodeUnlockPresentations();
	ClearConnectionLines();
	UnbindUpgradePresentationChanged();
	UnbindUpgradeStateChanged();
	UnbindPlayerStateNodeCountChanged();
	Super::NativeDestruct();
}

void UUpgradeNodeGroupWidget::BeginDestroy()
{
	ResetNodeUnlockPresentations();
	StopLineFillTicker();
	UnbindUpgradePresentationChanged();
	UnbindUpgradeStateChanged();
	UnbindPlayerStateNodeCountChanged();
	Super::BeginDestroy();
}

bool UUpgradeNodeGroupWidget::HandleUILayerEscape_Implementation()
{
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	return LayerSubsystem && LayerSubsystem->PopWidget(this);
}

void UUpgradeNodeGroupWidget::InitializeUILayerContext_Implementation(const TArray<AActor*>& ContextActors)
{
	AShooterCharacter* ContextShooter = ContextActors.IsValidIndex(0) ? Cast<AShooterCharacter>(ContextActors[0]) : nullptr;
	APartnerCharacter* ContextPartner = ContextActors.IsValidIndex(1) ? Cast<APartnerCharacter>(ContextActors[1]) : nullptr;
	AOutlierPlayerState* LocalPlayerState = GetOwningPlayer()
		? GetOwningPlayer()->GetPlayerState<AOutlierPlayerState>() : nullptr;
	UOutlierUpgradeComponent* LocalUpgrades = nullptr;
	if (LocalPlayerState && LocalPlayerState->IsShooterPlayer() && ContextShooter)
	{
		LocalUpgrades = ContextShooter->GetUpgradeComponent();
	}
	else if (LocalPlayerState && LocalPlayerState->IsPartnerPlayer() && ContextPartner)
	{
		LocalUpgrades = ContextPartner->GetUpgradeComponent();
	}
	// UI 레이어에서 바로 열어도 일반 업그레이드 화면과 같은 초기화 경로를 사용한다.
	InjectUpgradeContext(ContextShooter, ContextPartner, LocalUpgrades, LocalPlayerState);
}

void UUpgradeNodeGroupWidget::InjectUpgradeContext(
	AShooterCharacter* InShooterCharacter,
	APartnerCharacter* InPartnerCharacter,
	UOutlierUpgradeComponent* InUpgradeComponent,
	AOutlierPlayerState* InPlayerState)
{
	ResetNodeUnlockPresentations();
	ClearConnectionLines();
	UnbindUpgradePresentationChanged();
	if (UpgradeComponent != InUpgradeComponent)
	{
		UnbindUpgradeStateChanged();
	}

	if (PlayerState != InPlayerState)
	{
		UnbindPlayerStateNodeCountChanged();
	}

	ShooterCharacter = InShooterCharacter;
	PartnerCharacter = InPartnerCharacter;
	UpgradeComponent = InUpgradeComponent;
	PlayerState = InPlayerState;

	if (UpgradeComponent)
	{
		if (UpgradeSetData)
		{
			UpgradeComponent->SetUpgradeSetData(UpgradeSetData);
		}
		else
		{
			UpgradeComponent->SetUpgradeRole(GetResolvedUpgradeRole());
			if (UDataTable* ResolvedDataTable = GetResolvedUpgradeDataTable())
			{
				UpgradeComponent->SetUpgradeDataTable(ResolvedDataTable);
			}
		}
	}

	BindUpgradeStateChanged();
	BindPlayerStateNodeCountChanged();
	RefreshNodeWidgets();
	BindUpgradePresentationChanged();
	RebuildConnectionLines();
}

void UUpgradeNodeGroupWidget::RefreshNodeWidgets()
{
	CacheNodeWidgets();
	RefreshNodeCountText();

	for (UUpgradeNodeWidget* NodeWidget : NodeWidgets)
	{
		if (!NodeWidget)
		{
			continue;
		}

		if (UpgradeComponent)
		{
			NodeWidget->SetUpgradePlayerState(PlayerState);
			NodeWidget->SetUpgradeComponent(UpgradeComponent);
		}

		if (UpgradeDescWidgetClass && !NodeWidget->UpgradeDescWidgetClass)
		{
			NodeWidget->SetUpgradeDescWidgetClass(UpgradeDescWidgetClass);
		}

		NodeWidget->SetNodeTexture(ResolveNodeTexture(NodeWidget));
		NodeWidget->SetUnlockedNodeTexture(UnlockedNodeTexture);
		NodeWidget->SetDeactivatedNodeTexture(DeactivatedNodeTexture);
	}
}

void UUpgradeNodeGroupWidget::DismissOtherNodeDescriptions(const UUpgradeNodeWidget* HoveredNode) const
{
	for (UUpgradeNodeWidget* NodeWidget : NodeWidgets)
	{
		if (NodeWidget && NodeWidget != HoveredNode)
		{
			NodeWidget->DismissDescription();
		}
	}
}

EOutlierUpgradeRole UUpgradeNodeGroupWidget::GetResolvedUpgradeRole() const
{
	return UpgradeSetData
		? UpgradeSetData->UpgradeRole
		: UpgradeRole;
}

UDataTable* UUpgradeNodeGroupWidget::GetResolvedUpgradeDataTable() const
{
	return UpgradeSetData
		? UpgradeSetData->UpgradeDataTable.Get()
		: UpgradeDataTable.Get();
}

bool UUpgradeNodeGroupWidget::IsNodeIdSelectedByOtherWidget(
	const UUpgradeNodeWidget* RequestingWidget,
	FName InTreeId,
	FName InNodeId) const
{
	if (!NodeWidgetHost || !RequestingWidget || InTreeId.IsNone() || InNodeId.IsNone())
	{
		return false;
	}

	return IsNodeIdSelectedByOtherWidgetRecursive(NodeWidgetHost, RequestingWidget, InTreeId, InNodeId);
}

void UUpgradeNodeGroupWidget::HandleUpgradeStateChanged()
{
	RefreshNodeWidgets();
	// PS가 있으면 그쪽의 복제 갱신 완료 통보를 기다린다. 컴포넌트 사본이 먼저 도착할 수 있다.
	if (!PlayerState)
	{
		RefreshConnectionLines(false);
	}
}

void UUpgradeNodeGroupWidget::BindUpgradeStateChanged()
{
	if (UpgradeComponent)
	{
		UpgradeComponent->OnUpgradeStateChanged.AddUniqueDynamic(this, &UUpgradeNodeGroupWidget::HandleUpgradeStateChanged);
	}
}

void UUpgradeNodeGroupWidget::UnbindUpgradeStateChanged()
{
	if (UpgradeComponent)
	{
		UpgradeComponent->OnUpgradeStateChanged.RemoveDynamic(this, &UUpgradeNodeGroupWidget::HandleUpgradeStateChanged);
	}
}

void UUpgradeNodeGroupWidget::BindPlayerStateNodeCountChanged()
{
	if (PlayerState && !NodeCountChangedHandle.IsValid())
	{
		NodeCountChangedHandle = PlayerState->OnNodeCountChanged.AddUObject(
			this,
			&UUpgradeNodeGroupWidget::HandleNodeCountChanged);
	}
}

void UUpgradeNodeGroupWidget::UnbindPlayerStateNodeCountChanged()
{
	if (PlayerState && NodeCountChangedHandle.IsValid())
	{
		PlayerState->OnNodeCountChanged.Remove(NodeCountChangedHandle);
	}

	NodeCountChangedHandle.Reset();
}

void UUpgradeNodeGroupWidget::HandleNodeCountChanged(int32 NewNodeCount)
{
	RefreshNodeWidgets();
	if (NodeCount)
	{
		NodeCount->SetText(FText::AsNumber(NewNodeCount));
	}
}

void UUpgradeNodeGroupWidget::RefreshNodeCountText()
{
	if (NodeCount)
	{
		NodeCount->SetText(FText::AsNumber(PlayerState ? PlayerState->GetNodeCount() : 0));
	}
}

UTexture2D* UUpgradeNodeGroupWidget::ResolveNodeTexture(const UUpgradeNodeWidget* NodeWidget) const
{
	if (!NodeWidget)
	{
		return nullptr;
	}

	const FName RowName = NodeWidget->GetNodeRowName().IsNone()
		? NodeWidget->NodeRowName
		: NodeWidget->GetNodeRowName();
	return FindNodeTexture(RowName);
}

UTexture2D* UUpgradeNodeGroupWidget::FindNodeTexture(FName NodeRowName) const
{
	if (NodeRowName.IsNone())
	{
		return nullptr;
	}

	if (UpgradeSetData)
	{
		for (const FUpgradeNodeTextureBinding& Binding : UpgradeSetData->UnlockedNodeTextures)
		{
			if (Binding.NodeRowName == NodeRowName && Binding.Texture)
			{
				return Binding.Texture.Get();
			}
		}
	}

	return nullptr;
}

void UUpgradeNodeGroupWidget::CacheNodeWidgets()
{
	NodeWidgets.Reset();

	if (!NodeWidgetHost)
	{
		return;
	}

	CacheNodeWidgetsRecursive(NodeWidgetHost);
}

void UUpgradeNodeGroupWidget::CacheNodeWidgetsRecursive(UWidget* Widget)
{
	if (!Widget)
	{
		return;
	}

	if (UUpgradeNodeWidget* NodeWidget = Cast<UUpgradeNodeWidget>(Widget))
	{
		NodeWidgets.Add(NodeWidget);
		if (!NodeWidget->OnUnlockPresentationFinished.IsBoundToObject(this))
		{
			NodeWidget->OnUnlockPresentationFinished.AddUObject(this, &UUpgradeNodeGroupWidget::HandleUnlockPresentationFinished);
			NodeWidget->OnUnlockPresentationCancelled.AddUObject(this, &UUpgradeNodeGroupWidget::HandleUnlockPresentationCancelled);
		}
	}

	UPanelWidget* PanelWidget = Cast<UPanelWidget>(Widget);
	if (!PanelWidget)
	{
		return;
	}

	const int32 ChildCount = PanelWidget->GetChildrenCount();
	for (int32 ChildIndex = 0; ChildIndex < ChildCount; ++ChildIndex)
	{
		CacheNodeWidgetsRecursive(PanelWidget->GetChildAt(ChildIndex));
	}
}

bool UUpgradeNodeGroupWidget::IsNodeIdSelectedByOtherWidgetRecursive(
	const UWidget* Widget,
	const UUpgradeNodeWidget* RequestingWidget,
	FName InTreeId,
	FName InNodeId) const
{
	if (!Widget)
	{
		return false;
	}

	if (const UUpgradeNodeWidget* NodeWidget = Cast<UUpgradeNodeWidget>(Widget))
	{
		if (NodeWidget != RequestingWidget
			&& DoesWidgetReferenceNode(NodeWidget, InTreeId, InNodeId))
		{
			return true;
		}
	}

	const UPanelWidget* PanelWidget = Cast<UPanelWidget>(Widget);
	if (!PanelWidget)
	{
		return false;
	}

	const int32 ChildCount = PanelWidget->GetChildrenCount();
	for (int32 ChildIndex = 0; ChildIndex < ChildCount; ++ChildIndex)
	{
		if (IsNodeIdSelectedByOtherWidgetRecursive(
			PanelWidget->GetChildAt(ChildIndex),
			RequestingWidget,
			InTreeId,
			InNodeId))
		{
			return true;
		}
	}

	return false;
}

bool UUpgradeNodeGroupWidget::DoesWidgetReferenceNode(
	const UUpgradeNodeWidget* NodeWidget,
	FName InTreeId,
	FName InNodeId) const
{
	if (!NodeWidget)
	{
		return false;
	}

	if (NodeWidget->TreeId == InTreeId && NodeWidget->NodeId == InNodeId)
	{
		return true;
	}

	UDataTable* ResolvedDataTable = GetResolvedUpgradeDataTable();
	if (!ResolvedDataTable || NodeWidget->NodeRowName.IsNone())
	{
		return false;
	}

	const FOutlierUpgradeNodeRow* Row = ResolvedDataTable->FindRow<FOutlierUpgradeNodeRow>(
		NodeWidget->NodeRowName,
		TEXT("UpgradeNodeGroupWidgetDuplicateCheck"),
		false);
	return Row && Row->TreeId == InTreeId && Row->NodeId == InNodeId;
}

void UUpgradeNodeGroupWidget::ClearConnectionLines()
{
	StopLineFillTicker();
	for (FUpgradeLineRuntime& Line : RuntimeLines)
	{
		if (Line.Image && Line.Image->GetBrush().GetResourceObject() == Line.Material)
		{
			Line.Image->SetBrush(Line.OriginalBrush);
			Line.Image->SetRenderOpacity(Line.OriginalOpacity);
			Line.Image->SetVisibility(Line.OriginalVisibility);
		}
	}
	RuntimeLines.Reset();
}

void UUpgradeNodeGroupWidget::RebuildConnectionLines()
{
	ClearConnectionLines();
	RefreshNodeWidgets();
#if WITH_EDITOR
	UpdateLineMeasurements();
#endif
	CachedRestoreRevision = PlayerState ? PlayerState->GetUpgradeRestoreRevision(GetResolvedUpgradeRole()) : 0;
	if (ConnectionLines.IsEmpty() || !UpgradeComponent)
	{
		return;
	}
	if (!LineFillMaterial)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Upgrade.Line] LineFillMaterial is missing: %s"), *GetName());
		return;
	}

	TSet<FName> UsedImages;
	for (const FUpgradeLineBinding& Binding : ConnectionLines)
	{
		UImage* Image = Cast<UImage>(GetWidgetFromName(Binding.ImageName));
		FOutlierUpgradeNodeViewData Source;
		FOutlierUpgradeNodeViewData Destination;
		FOutlierUpgradeNodeViewData Parent;
		const FName SourceRow = ResolveLineNodeRowName(Binding.SourceNodeWidgetName);
		const FName DestinationRow = ResolveLineNodeRowName(Binding.DestinationNodeWidgetName);
		// 시작 노드가 없는 진입선은 항상 완성해 표시한다. 잘못 지정한 위젯은 None으로 간주하지 않는다.
		const bool bNoSourceNode = Binding.SourceNodeWidgetName.IsNone();
		const bool bValidDestination = !DestinationRow.IsNone()
			&& UpgradeComponent->GetNodeViewData(DestinationRow, Destination)
			&& Destination.RowName == DestinationRow;
		const bool bValidNodes = bValidDestination && (bNoSourceNode
			|| (!SourceRow.IsNone() && UpgradeComponent->GetNodeViewData(SourceRow, Source)
				&& Source.RowName == SourceRow
				&& UpgradeComponent->GetNodeViewData(Destination.NodeRow.ParentId, Parent)
				&& Parent.RowName == Source.RowName));
		UTexture2D* Texture = Binding.LineTexture;
		if (!Texture && Image)
		{
			Texture = Cast<UTexture2D>(Image->GetBrush().GetResourceObject());
		}
		if (!Image || !Texture || !bValidNodes || UsedImages.Contains(Binding.ImageName))
		{
			UE_LOG(LogTemp, Warning, TEXT("[Upgrade.Line] Invalid image/texture/parent mapping: %s (%s -> %s)"),
				*Binding.ImageName.ToString(), *SourceRow.ToString(), *DestinationRow.ToString());
			continue;
		}
		UsedImages.Add(Binding.ImageName);
		FUpgradeLineRuntime& Line = RuntimeLines.AddDefaulted_GetRef();
		Line.Image = Image;
		Line.OriginalBrush = Image->GetBrush();
		Line.OriginalOpacity = Image->GetRenderOpacity();
		Line.OriginalVisibility = Image->GetVisibility();
		Line.ActivationNodeRowName = bNoSourceNode ? NAME_None : Source.RowName;
		Line.SourceNode = bNoSourceNode ? nullptr : Cast<UUpgradeNodeWidget>(GetWidgetFromName(Binding.SourceNodeWidgetName));
		Line.Length = FMath::Max(0.0f, Binding.LineLength);
		if (Line.Length <= 0.0f && Binding.MeasuredTexture == Texture)
		{
			Line.AutoLengthExtent = Binding.MeasuredLineExtent;
			Line.Length = MeasureDisplayedLineLength(Line);
		}
		Line.Material = UMaterialInstanceDynamic::Create(LineFillMaterial, this);
		Line.Material->SetTextureParameterValue(TEXT("LineTexture"), Texture);
		// 경로 텍스처가 없는 기존 선은 축 기준 채움을 유지한다. 각 Image의 MID에만 설정한다.
		Line.Material->SetScalarParameterValue(TEXT("UsePathProgress"), Binding.PathProgressTexture ? 1.0f : 0.0f);
		if (Binding.PathProgressTexture)
		{
			Line.Material->SetTextureParameterValue(TEXT("PathProgressTexture"), Binding.PathProgressTexture);
		}
		Line.Material->SetVectorParameterValue(TEXT("FillColor"), LineFillColor);
		Line.Material->SetScalarParameterValue(TEXT("UseHorizontal"), Binding.bUseHorizontal ? 1.0f : 0.0f);
		Line.Material->SetScalarParameterValue(TEXT("Reverse"), Binding.bReverse ? 1.0f : 0.0f);
		Image->SetBrushFromMaterial(Line.Material);
		Image->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	RefreshConnectionLines(true);
}

bool UUpgradeNodeGroupWidget::IsLineSourceActivated(FName RowName) const
{
	// 유효한 끝 노드로 연결된 None 시작 선은 구매 상태와 무관하게 켜 둔다.
	if (RowName.IsNone()) return true;
	FOutlierUpgradeNodeRow Row;
	if (!UpgradeComponent || !UpgradeComponent->GetNodeRow(RowName, Row))
	{
		return false;
	}
	// PS 목록이 구매/복원의 원본이다. 다른 Actor인 컴포넌트 사본의 도착 순서를 기다리지 않는다.
	return Row.bInitiallyActivated || (PlayerState
		? PlayerState->GetActivatedUpgradeNodeIds(GetResolvedUpgradeRole()).Contains(RowName)
		: UpgradeComponent->IsNodeActivated(RowName));
}

void UUpgradeNodeGroupWidget::SetLineProgress(FUpgradeLineRuntime& Line, float Progress)
{
	Line.Progress = FMath::Clamp(Progress, 0.0f, 1.0f);
	if (Line.Material)
	{
		Line.Material->SetScalarParameterValue(TEXT("Progress"), Line.Progress);
	}
	// 기본 선은 항상 남긴다. 진행도는 머티리얼 안에서 활성 색상을 덮는 범위만 바꾼다.
}

void UUpgradeNodeGroupWidget::RefreshConnectionLines(bool bImmediate)
{
	const uint32 RestoreRevision = PlayerState ? PlayerState->GetUpgradeRestoreRevision(GetResolvedUpgradeRole()) : 0;
	const bool bRestored = RestoreRevision != CachedRestoreRevision;
	bImmediate |= bRestored;
	CachedRestoreRevision = RestoreRevision;
	// 알림을 받는 순서와 무관하게 노드의 재생 여부를 먼저 확정한 뒤 선의 시작 조건을 판정한다.
	for (UUpgradeNodeWidget* Node : NodeWidgets)
	{
		if (!Node) continue;
		if (bRestored) Node->ResetUnlockPresentation();
		Node->RefreshFromUpgradeComponent(UpgradeComponent);
	}
	bool bNeedsAnimation = false;
	for (FUpgradeLineRuntime& Line : RuntimeLines)
	{
		const bool bWaitingForUnlock = Line.SourceNode && Line.SourceNode->IsUnlockPresentationPlaying();
		const bool bActivated = IsLineSourceActivated(Line.ActivationNodeRowName) && !bWaitingForUnlock;
		// 최초 레이아웃 뒤의 실제 크기는 새 구매를 시작할 때 한 번만 반영한다.
		if (bActivated && !Line.bSourceActivated && !Line.AutoLengthExtent.IsNearlyZero())
		{
			Line.Length = MeasureDisplayedLineLength(Line);
		}
		if (bImmediate || !bActivated || GetLineFillDuration(Line) <= 0.0f)
		{
			Line.bAnimating = false;
			SetLineProgress(Line, bActivated ? 1.0f : 0.0f);
		}
		else if (!Line.bSourceActivated)
		{
			// 새 활성화만 시작한다. 같은 상태가 다시 통보돼도 진행도를 되돌리지 않는다.
			SetLineProgress(Line, 0.0f);
			Line.bAnimating = true;
			Line.FillStartedTime = FPlatformTime::Seconds();
		}
		Line.bSourceActivated = bActivated;
		bNeedsAnimation |= Line.bAnimating;
	}
	if (!bNeedsAnimation)
	{
		StopLineFillTicker();
	}
	else if (!LineFillTickerHandle.IsValid())
	{
		LastLineFillTime = FPlatformTime::Seconds();
		LineFillTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &UUpgradeNodeGroupWidget::TickLineFills), 1.0f / 60.0f);
	}
}

float UUpgradeNodeGroupWidget::GetLineFillDuration(const FUpgradeLineRuntime& Line) const
{
	// 길이가 다른 선도 같은 거리/초로 움직인다. 길이 또는 속도가 없으면 기존 설정으로 돌아간다.
	return LineFillSpeed > 0.0f && Line.Length > 0.0f
		? Line.Length / LineFillSpeed : LineFillDuration;
}

bool UUpgradeNodeGroupWidget::AdvanceLineFills(float DeltaSeconds, double Now)
{
	bool bStillAnimating = false;
	for (FUpgradeLineRuntime& Line : RuntimeLines)
	{
		if (Line.bAnimating)
		{
			const float Duration = GetLineFillDuration(Line);
			// 다른 선을 주기적으로 갱신 중이어도 이 선이 대기한 시간은 채움에 포함하지 않는다.
			const float Elapsed = Now > 0.0
				? static_cast<float>(FMath::Max(0.0, Now - FMath::Max(LastLineFillTime, Line.FillStartedTime)))
				: FMath::Max(0.0f, DeltaSeconds);
			SetLineProgress(Line, Duration > 0.0f
				? Line.Progress + Elapsed / Duration : 1.0f);
			Line.bAnimating = Line.Progress < 1.0f;
			bStillAnimating |= Line.bAnimating;
		}
	}
	return bStillAnimating;
}

bool UUpgradeNodeGroupWidget::TickLineFills(float DeltaSeconds)
{
	bool bVisible = IsVisible();
	for (const UWidget* Parent = GetParent(); Parent && bVisible; Parent = Parent->GetParent())
	{
		bVisible = Parent->IsVisible();
	}
	if (!bVisible)
	{
		RefreshConnectionLines(true);
		return false;
	}
	const double Now = FPlatformTime::Seconds();
	const float Elapsed = static_cast<float>(FMath::Max(0.0, Now - LastLineFillTime));
	const bool bStillAnimating = AdvanceLineFills(Elapsed, Now);
	LastLineFillTime = Now;
	if (!bStillAnimating)
	{
		LineFillTickerHandle.Reset();
		return false;
	}
	return true;
}

void UUpgradeNodeGroupWidget::ResetNodeUnlockPresentations()
{
	for (UUpgradeNodeWidget* Node : NodeWidgets)
	{
		if (Node)
		{
			Node->OnUnlockPresentationFinished.RemoveAll(this);
			Node->OnUnlockPresentationCancelled.RemoveAll(this);
			Node->ResetUnlockPresentation();
		}
	}
}

void UUpgradeNodeGroupWidget::HandleUnlockPresentationFinished(UUpgradeNodeWidget* Node)
{
	if (!NodeWidgets.Contains(Node) || !IsVisible()) return;
	for (const UWidget* Parent = GetParent(); Parent; Parent = Parent->GetParent())
	{
		if (!Parent->IsVisible()) return;
	}
	// 완료된 노드에서 나가는 선만 새로 출발한다. 다른 선의 진행도는 유지한다.
	RefreshConnectionLines(false);
}

void UUpgradeNodeGroupWidget::HandleUnlockPresentationCancelled(UUpgradeNodeWidget* Node)
{
	// 숨김으로 중단된 선은 애니메이션 없이 서버 상태로 맞춘다. 다시 보일 때 대기 상태가 남지 않는다.
	for (FUpgradeLineRuntime& Line : RuntimeLines)
	{
		if (Line.SourceNode == Node)
		{
			Line.bSourceActivated = IsLineSourceActivated(Line.ActivationNodeRowName);
			Line.bAnimating = false;
			SetLineProgress(Line, Line.bSourceActivated ? 1.0f : 0.0f);
		}
	}
}

void UUpgradeNodeGroupWidget::StopLineFillTicker()
{
	FTSTicker::GetCoreTicker().RemoveTicker(LineFillTickerHandle);
	LineFillTickerHandle.Reset();
	for (FUpgradeLineRuntime& Line : RuntimeLines)
	{
		Line.bAnimating = false;
	}
}

void UUpgradeNodeGroupWidget::BindUpgradePresentationChanged()
{
	if (PlayerState && BoundLinePlayerState.Get() != PlayerState)
	{
		UnbindUpgradePresentationChanged();
		BoundLinePlayerState = PlayerState;
		UpgradePresentationChangedHandle = PlayerState->OnUpgradePresentationChanged.AddUObject(
			this, &UUpgradeNodeGroupWidget::HandleUpgradePresentationChanged);
	}
}

void UUpgradeNodeGroupWidget::UnbindUpgradePresentationChanged()
{
	if (AOutlierPlayerState* Bound = BoundLinePlayerState.Get())
	{
		Bound->OnUpgradePresentationChanged.Remove(UpgradePresentationChangedHandle);
	}
	BoundLinePlayerState.Reset();
	UpgradePresentationChangedHandle.Reset();
}

void UUpgradeNodeGroupWidget::HandleUpgradePresentationChanged()
{
	RefreshConnectionLines(false);
}
