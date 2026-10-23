#include "Interaction/JournalInteraction.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "FirstPerson/FirstPersonCharacter.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "Interaction/InteractableComponent.h"
#include "Net/UnrealNetwork.h"
#include "Shooter/ShooterCharacter.h"
#include "UI/JournalWidget.h"
#include "UI/UILayerGameplayTags.h"

AJournalInteraction::AJournalInteraction()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;
	JournalMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("JournalMesh"));
	JournalMesh->SetupAttachment(SceneRoot);
	JournalMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	JournalMesh->SetCollisionObjectType(ECC_WorldDynamic);
	JournalMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	JournalMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	JournalMesh->SetGenerateOverlapEvents(false);

	InteractableComponent = CreateDefaultSubobject<UInteractableComponent>(TEXT("InteractableComponent"));
	InteractableComponent->InteractableTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Interact.Target.Item")));
	InteractableComponent->InteractableTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("Interact.Use.Multiple")));
}

void AJournalInteraction::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AJournalInteraction, JournalEntry);
}

UInteractableComponent* AJournalInteraction::GetInteractableComponent() const
{
	return InteractableComponent;
}

bool AJournalInteraction::Interact(AFirstPersonCharacter* Interactor)
{
	if (!HasAuthority() || !IsValid(Interactor)
		|| (!Interactor->IsA<AShooterCharacter>() && !Interactor->IsA<APartnerCharacter>())
		|| !InteractableComponent || !InteractableComponent->CanInteract(Interactor->GetOwnedGameplayTagsForQuery()))
	{
		return false;
	}

	AFirstPersonPlayerController* Controller = Cast<AFirstPersonPlayerController>(Interactor->GetController());
	const UDataTable* Table = JournalEntry.DataTable;
	if (!Controller || !JournalWidgetClass || JournalWidgetClass->HasAnyClassFlags(CLASS_Abstract)
		|| !Table || !Table->GetRowStruct() || !Table->GetRowStruct()->IsChildOf(FJournalRow::StaticStruct())
		|| JournalEntry.RowName.IsNone()
		|| !Table->FindRow<FJournalRow>(JournalEntry.RowName, TEXT("JournalInteraction"), false))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Journal] Interaction rejected: invalid controller, widget class or entry. Actor=%s Table=%s Row=%s"),
			*GetName(), *GetNameSafe(Table), *JournalEntry.RowName.ToString());
		return false;
	}

	// 거리와 시선 검사는 Character의 ServerInteract가 담당한다.
	// 이 요청은 상호작용한 Controller에만 보내며, 상대 플레이어의 UI와 입력은 변경하지 않는다.
	FUILayerPushRequest Request;
	Request.WidgetClass = JournalWidgetClass;
	Request.LayerTag = UILayerTags::GameMenu();
	Request.InputModeTag = FirstPersonInputModeTags::UI();
	Request.RequestOwner = this;
	Request.ContextActors = {this};
	Request.FocusTarget = EUILayerFocusTarget::Widget;
	Request.bShowCursor = true;
	Controller->ClientPushUILayer(Request);
	return true;
}

void AJournalInteraction::OnRep_JournalEntry()
{
	// UI 생성보다 행 복제가 늦게 도착해도, 열린 화면이 서버의 설정을 다시 읽게 한다.
	OnJournalEntryChanged.Broadcast();
}

void AJournalInteraction::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// 액터의 Mesh를 지정한 경우에만 덮어쓴다. 비어 있으면 컴포넌트에 직접 지정한
	// 인스턴스 메시를 유지해야 Construction 때 None으로 되돌아가지 않는다.
	if (JournalMeshAsset)
	{
		JournalMesh->SetStaticMesh(JournalMeshAsset);
	}
	if (JournalMesh->GetStaticMesh())
	{
		// 메시 자체의 피봇은 변경하지 않고, 하단 중앙을 액터 원점에 맞춘다.
		// 기존 위치 오프셋을 제외하고 계산해야 Construction 반복 시 위치가 누적되지 않는다.
		const FTransform MeshShapeTransform(
			JournalMesh->GetRelativeRotation(), FVector::ZeroVector, JournalMesh->GetRelativeScale3D());
		const FBoxSphereBounds MeshBounds = JournalMesh->CalcBounds(MeshShapeTransform);
		const FVector BottomCenter = MeshBounds.Origin - FVector(0.0, 0.0, MeshBounds.BoxExtent.Z);
		JournalMesh->SetRelativeLocation(-BottomCenter);
	}
	else
	{
		JournalMesh->SetRelativeLocation(FVector::ZeroVector);
	}
#if WITH_EDITOR
	RefreshJournalPreview();
#endif
}

#if WITH_EDITOR

void AJournalInteraction::PostLoad()
{
	Super::PostLoad();
	RefreshJournalPreview();
}

void AJournalInteraction::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (IsValid(this))
	{
		RefreshJournalPreview();
	}
}

void AJournalInteraction::RefreshJournalPreview()
{
	const UDataTable* Table = JournalEntry.DataTable;
	if (PreviewTable.Get() != Table || (Table && !PreviewTableChangedHandle.IsValid()))
	{
		UnbindPreviewTable();
		// RowHandle의 데이터는 읽기 전용이다. 엔진의 변경 Delegate API만 non-const이므로
		// 구독·해제에 사용할 참조에서만 const를 해제하고, 행 내용은 변경하지 않는다.
		PreviewTable = const_cast<UDataTable*>(Table);
		if (Table && !HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
		{
			// Reimport는 액터 프로퍼티를 변경하지 않는다. 참조 중인 테이블의 통지만 구독한다.
			PreviewTableChangedHandle = PreviewTable->OnDataTableChanged().AddUObject(this, &ThisClass::RefreshJournalPreview);
		}
	}

	const FJournalRow* Row = Table && Table->GetRowStruct()
		&& Table->GetRowStruct()->IsChildOf(FJournalRow::StaticStruct()) && !JournalEntry.RowName.IsNone()
		? Table->FindRow<FJournalRow>(JournalEntry.RowName, TEXT("JournalPreview"), false) : nullptr;
	PreviewTitle = Row ? Row->Title : FText::GetEmpty();
	PreviewDesc = Row ? Row->Desc : FText::GetEmpty();
}

void AJournalInteraction::UnbindPreviewTable()
{
	if (UDataTable* Table = PreviewTable.Get())
	{
		Table->OnDataTableChanged().Remove(PreviewTableChangedHandle);
	}
	PreviewTable.Reset();
	PreviewTableChangedHandle.Reset();
}

void AJournalInteraction::BeginDestroy()
{
	UnbindPreviewTable();
	Super::BeginDestroy();
}

#endif
