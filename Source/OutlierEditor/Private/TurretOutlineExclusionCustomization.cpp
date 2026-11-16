#include "TurretOutlineExclusionCustomization.h"

#include "Components/SkeletalMeshComponent.h"
#include "DetailWidgetRow.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/EngineTypes.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameFramework/Actor.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "PropertyHandle.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<IPropertyTypeCustomization> FTurretOutlineExclusionCustomization::MakeInstance()
{
	return MakeShared<FTurretOutlineExclusionCustomization>();
}

void FTurretOutlineExclusionCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& Utils)
{
	HeaderRow.NameContent()[PropertyHandle->CreatePropertyNameWidget()];
}

void FTurretOutlineExclusionCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& Utils)
{
	MeshHandle = PropertyHandle->GetChildHandle(TEXT("MeshComponent"));
	if (MeshHandle.IsValid())
	{
		ChildBuilder.AddProperty(MeshHandle.ToSharedRef()).CustomWidget()
		.NameContent()[MeshHandle->CreatePropertyNameWidget()]
		.ValueContent().MinDesiredWidth(220.0f)
		[
			SNew(SComboButton)
			.IsEnabled_Lambda([Handle = MeshHandle]() { return !Handle->IsEditConst(); })
			.OnGetMenuContent(this, &FTurretOutlineExclusionCustomization::BuildMeshMenu)
			.ButtonContent()[SNew(STextBlock).Text(this, &FTurretOutlineExclusionCustomization::GetMeshText)]
		];
	}
	if (TSharedPtr<IPropertyHandle> Slots = PropertyHandle->GetChildHandle(TEXT("MaterialSlotNames")))
	{
		ChildBuilder.AddProperty(Slots.ToSharedRef());
	}
}

TSharedRef<SWidget> FTurretOutlineExclusionCustomization::BuildMeshMenu()
{
	TArray<UObject*> Objects;
	MeshHandle->GetOuterObjects(Objects);
	TSet<FName> CommonNames;
	bool bFirst = true;
	for (UObject* Object : Objects)
	{
		const AActor* Actor = Cast<AActor>(Object);
		if (!Actor) Actor = Object ? Object->GetTypedOuter<AActor>() : nullptr;
		TSet<FName> Names;
		if (Actor)
		{
			// CDO에는 BP 추가 컴포넌트 인스턴스가 없다. 기본 프로퍼티와 상속된 SCS 노드를
			// 함께 읽고 변수명만 저장해, 생성된 터렛에서도 기존 GetComponent 경로를 사용한다.
			for (TFieldIterator<FObjectPropertyBase> It(Actor->GetClass()); It; ++It)
			{
				if (It->PropertyClass->IsChildOf(USkeletalMeshComponent::StaticClass())) Names.Add(It->GetFName());
			}
			for (const UClass* Class = Actor->GetClass(); Class; Class = Class->GetSuperClass())
			{
				const auto* BlueprintClass = Cast<UBlueprintGeneratedClass>(Class);
				if (!BlueprintClass || !BlueprintClass->SimpleConstructionScript) continue;
				for (const USCS_Node* Node : BlueprintClass->SimpleConstructionScript->GetAllNodes())
				{
					if (Node && Cast<USkeletalMeshComponent>(Node->ComponentTemplate)) Names.Add(Node->GetVariableName());
				}
			}
		}
		// 다중 편집은 모든 대상에서 해석할 수 있는 변수만 제공한다.
		CommonNames = bFirst ? MoveTemp(Names) : CommonNames.Intersect(Names);
		bFirst = false;
	}
	TArray<FName> SortedNames = CommonNames.Array();
	SortedNames.Sort(FNameLexicalLess());
	FMenuBuilder Menu(true, nullptr);
	Menu.AddMenuEntry(FText::FromString(TEXT("None")), FText::GetEmpty(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &FTurretOutlineExclusionCustomization::SelectMesh, FName(NAME_None))));
	for (FName Name : SortedNames)
	{
		Menu.AddMenuEntry(FText::FromName(Name), FText::GetEmpty(), FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &FTurretOutlineExclusionCustomization::SelectMesh, Name)));
	}
	return Menu.MakeWidget();
}

FText FTurretOutlineExclusionCustomization::GetMeshText() const
{
	TArray<void*> Values;
	MeshHandle->AccessRawData(Values);
	const FComponentReference* First = nullptr;
	for (const void* Value : Values)
	{
		if (!Value) continue;
		const auto* Reference = static_cast<const FComponentReference*>(Value);
		if (First && !(*First == *Reference)) return FText::FromString(TEXT("Multiple Values"));
		First = Reference;
	}
	if (!First) return FText::GetEmpty();
	if (!First->ComponentProperty.IsNone()) return FText::FromName(First->ComponentProperty);
	return First->PathToComponent.IsEmpty() ? FText::FromString(TEXT("None")) : FText::FromString(First->PathToComponent);
}

void FTurretOutlineExclusionCustomization::SelectMesh(FName ComponentName)
{
	FComponentReference Reference;
	Reference.ComponentProperty = ComponentName;
	FString Value;
	FComponentReference::StaticStruct()->ExportText(Value, &Reference, nullptr, nullptr, PPF_None, nullptr);
	// 프로퍼티 핸들로 갱신해 Undo/Redo·BP 기본값 저장과 인스턴스 override를 유지한다.
	MeshHandle->SetValueFromFormattedString(Value);
}
