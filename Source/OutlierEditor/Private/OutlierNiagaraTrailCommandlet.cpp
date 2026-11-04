#include "OutlierNiagaraTrailCommandlet.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Algo/Find.h"
#include "FileHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "NiagaraDataInterfaceArrayFloat.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraEmitter.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraGraph.h"
#include "NiagaraScript.h"
#include "NiagaraSystem.h"
#include "UObject/StructOnScope.h"
#include "UObject/UObjectHash.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "ViewModels/Stack/NiagaraStackFunctionInput.h"
#include "ViewModels/Stack/NiagaraStackModuleItem.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"

DEFINE_LOG_CATEGORY_STATIC(LogOutlierNiagaraTrail, Log, All);

namespace OutlierNiagaraTrail
{
	const TCHAR* SystemPaths[] = {
		TEXT("/Game/OutlierGunTrail/NS_Trail_Player_Original.NS_Trail_Player_Original"),
		TEXT("/Game/OutlierGunTrail/NS_Trail_Drone_Original.NS_Trail_Drone_Original"),
		TEXT("/Game/OutlierGunTrail/NS_Trail_Turret_Original.NS_Trail_Turret_Original"),
	};

	FString DescribePin(const UEdGraphPin* Pin)
	{
		FString Links;
		for (const UEdGraphPin* Linked : Pin->LinkedTo)
		{
			Links += FString::Printf(TEXT(" -> %s.%s"), *Linked->GetOwningNode()->GetName(), *Linked->PinName.ToString());
		}
		const UObject* SubObject = Pin->PinType.PinSubCategoryObject.Get();
		return FString::Printf(TEXT("%s %s [%s/%s] Default='%s'%s"),
			Pin->Direction == EGPD_Input ? TEXT("in ") : TEXT("out"),
			*Pin->PinName.ToString(), *Pin->PinType.PinCategory.ToString(), *GetNameSafe(SubObject),
			*Pin->DefaultValue, *Links);
	}

	FString DescribeNodeExtra(const UEdGraphNode* Node)
	{
		// 노드 클래스 대부분이 export 되지 않아 리플렉션으로 읽는다.
		FString Extra;
		if (const FObjectProperty* ScriptProp = FindFProperty<FObjectProperty>(Node->GetClass(), TEXT("FunctionScript")))
		{
			Extra += FString::Printf(TEXT(" FunctionScript=%s"), *GetPathNameSafe(ScriptProp->GetObjectPropertyValue_InContainer(Node)));
		}
		if (const FNameProperty* NameProp = FindFProperty<FNameProperty>(Node->GetClass(), TEXT("FunctionDisplayName")))
		{
			Extra += FString::Printf(TEXT(" Display=%s"), *NameProp->GetPropertyValue_InContainer(Node).ToString());
		}
		return Extra;
	}

	void DumpGraph(const UEdGraph* Graph)
	{
		UE_LOG(LogOutlierNiagaraTrail, Display, TEXT("GRAPH %s"), *Graph->GetPathName());
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node)
			{
				continue;
			}
			UE_LOG(LogOutlierNiagaraTrail, Display, TEXT("  NODE %s (%s) '%s'%s"), *Node->GetName(), *Node->GetClass()->GetName(),
				*Node->GetNodeTitle(ENodeTitleType::ListView).ToString(), *DescribeNodeExtra(Node));
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				UE_LOG(LogOutlierNiagaraTrail, Display, TEXT("    PIN %s"), *DescribePin(Pin));
			}
		}
	}

	void DumpSystem(UNiagaraSystem* System)
	{
		UE_LOG(LogOutlierNiagaraTrail, Display, TEXT("==== SYSTEM %s"), *System->GetPathName());

		TArray<FNiagaraVariable> UserParameters;
		System->GetExposedParameters().GetUserParameters(UserParameters);
		for (const FNiagaraVariable& Variable : UserParameters)
		{
			UE_LOG(LogOutlierNiagaraTrail, Display, TEXT("USERPARAM %s : %s"), *Variable.GetName().ToString(), *Variable.GetType().GetName());
		}

		// 스택에서 로컬 값으로 넣은 모듈 입력은 래피드 이터레이션 파라미터( Constants.* )에 들어 있다.
		TArray<UNiagaraScript*> Scripts = { System->GetSystemSpawnScript(), System->GetSystemUpdateScript() };
		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			if (FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData())
			{
				Data->GetScripts(Scripts, false);
				UE_LOG(LogOutlierNiagaraTrail, Display, TEXT("EMITTER %s LocalSpace=%d"), *Handle.GetName().ToString(), Data->bLocalSpace);

				// 렌더러가 쓰는 머티리얼과 그 스칼라 / 스태틱 스위치 값. 트레이서가 화면에 어떻게 보이는지는 여기서 갈린다.
				for (const UNiagaraRendererProperties* Renderer : Data->GetRenderers())
				{
					UE_LOG(LogOutlierNiagaraTrail, Display, TEXT("RENDERER %s enabled=%d"), *Renderer->GetClass()->GetName(), Renderer->GetIsEnabled());
					for (TFieldIterator<FProperty> It(Renderer->GetClass()); It; ++It)
					{
						const FString PropName = It->GetName();
						if (PropName.EndsWith(TEXT("Binding")) || PropName.Contains(TEXT("Alignment")) || PropName.Contains(TEXT("Facing"))
							|| PropName.Contains(TEXT("PivotInUVSpace")) || PropName.Contains(TEXT("SubImage")))
						{
							FString Value;
							It->ExportTextItem_Direct(Value, It->ContainerPtrToValuePtr<void>(Renderer), nullptr, nullptr, PPF_None);
							if (PropName.EndsWith(TEXT("Binding")))
							{
								// 바인딩은 텍스트가 길어 실제 바인딩된 변수 이름만 남긴다.
								int32 NameStart = Value.Find(TEXT("Name=\""));
								Value = NameStart != INDEX_NONE ? Value.Mid(NameStart, 60) : Value.Left(60);
							}
							UE_LOG(LogOutlierNiagaraTrail, Display, TEXT("  PROP %s = %s"), *PropName, *Value);
						}
					}
					TArray<UMaterialInterface*> Materials;
					Renderer->GetUsedMaterials(nullptr, Materials);
					for (const UMaterialInterface* Material : Materials)
					{
						UE_LOG(LogOutlierNiagaraTrail, Display, TEXT("MATERIAL %s %s"), *Renderer->GetClass()->GetName(), *GetPathNameSafe(Material));
						if (!Material)
						{
							continue;
						}
						TArray<FMaterialParameterInfo> Infos;
						TArray<FGuid> Ids;
						Material->GetAllScalarParameterInfo(Infos, Ids);
						for (const FMaterialParameterInfo& Info : Infos)
						{
							float Value = 0.0f;
							Material->GetScalarParameterValue(Info, Value);
							UE_LOG(LogOutlierNiagaraTrail, Display, TEXT("  SCALAR %s = %f"), *Info.Name.ToString(), Value);
						}
						Material->GetAllStaticSwitchParameterInfo(Infos, Ids);
						for (const FMaterialParameterInfo& Info : Infos)
						{
							bool bValue = false;
							FGuid Guid;
							Material->GetStaticSwitchParameterValue(Info, bValue, Guid);
							UE_LOG(LogOutlierNiagaraTrail, Display, TEXT("  SWITCH %s = %d"), *Info.Name.ToString(), bValue);
						}
					}
				}
			}
		}
		for (UNiagaraScript* Script : Scripts)
		{
			TArray<FNiagaraVariable> Constants;
			if (Script)
			{
				Script->RapidIterationParameters.GetParameters(Constants);
			}
			for (FNiagaraVariable& Constant : Constants)
			{
				const int32 Offset = Script->RapidIterationParameters.IndexOf(Constant);
				if (Offset != INDEX_NONE && Constant.GetType().GetSize() > 0 && !Constant.IsDataInterface() && !Constant.IsUObject())
				{
					Constant.SetData(Script->RapidIterationParameters.GetParameterData(Offset, Constant.GetType()));
				}
				UE_LOG(LogOutlierNiagaraTrail, Display, TEXT("CONST %s %s = %s"), *Script->GetName(), *Constant.GetName().ToString(), *Constant.ToString());
			}
		}

		TArray<UObject*> Objects;
		GetObjectsWithPackage(System->GetPackage(), Objects, true);
		for (UObject* Object : Objects)
		{
			if (const UNiagaraGraph* Graph = Cast<UNiagaraGraph>(Object))
			{
				DumpGraph(Graph);
			}
		}
	}
}

namespace OutlierNiagaraTrail
{
	const FName StartName(TEXT("User.Start"));
	const FName EndName(TEXT("User.End"));
	const FName ImpactPositionsName(TEXT("User.ImpactPositions"));
	const FName MuzzlePositionName(TEXT("User.MuzzlePosition"));

	// 에디터 스택 UI 와 같은 경로로 수정하기 위해 스택 엔트리를 모듈 단위로 모은다.
	struct FStackInput
	{
		UNiagaraStackModuleItem* Module = nullptr;
		UNiagaraStackFunctionInput* Input = nullptr;
	};

	void CollectStack(UNiagaraStackEntry* Entry, UNiagaraStackModuleItem* CurrentModule,
		TArray<UNiagaraStackModuleItem*>& OutModules, TArray<FStackInput>& OutInputs)
	{
		if (!Entry)
		{
			return;
		}
		Entry->RefreshChildren();

		if (UNiagaraStackModuleItem* Module = Cast<UNiagaraStackModuleItem>(Entry))
		{
			CurrentModule = Module;
			OutModules.Add(Module);
		}
		else if (UNiagaraStackFunctionInput* Input = Cast<UNiagaraStackFunctionInput>(Entry))
		{
			OutInputs.Add({ CurrentModule, Input });
		}

		TArray<UNiagaraStackEntry*> Children;
		Entry->GetUnfilteredChildren(Children);
		for (UNiagaraStackEntry* Child : Children)
		{
			CollectStack(Child, CurrentModule, OutModules, OutInputs);
		}
	}

	FString ModuleName(const UNiagaraStackModuleItem* Module)
	{
		return Module ? Module->GetDisplayName().ToString() : TEXT("<none>");
	}

	const TCHAR* ValueModeName(UNiagaraStackFunctionInput::EValueMode Mode)
	{
		switch (Mode)
		{
		case UNiagaraStackFunctionInput::EValueMode::Local: return TEXT("Local");
		case UNiagaraStackFunctionInput::EValueMode::Linked: return TEXT("Linked");
		case UNiagaraStackFunctionInput::EValueMode::Dynamic: return TEXT("Dynamic");
		case UNiagaraStackFunctionInput::EValueMode::Data: return TEXT("Data");
		default: return TEXT("Other");
		}
	}

	void CollectAll(FNiagaraSystemViewModel& ViewModel, TArray<UNiagaraStackModuleItem*>& OutModules, TArray<FStackInput>& OutInputs)
	{
		OutModules.Reset();
		OutInputs.Reset();
		CollectStack(ViewModel.GetSystemStackViewModel()->GetRootEntry(), nullptr, OutModules, OutInputs);
		for (const TSharedRef<FNiagaraEmitterHandleViewModel>& Handle : ViewModel.GetEmitterHandleViewModels())
		{
			CollectStack(Handle->GetEmitterStackViewModel()->GetRootEntry(), nullptr, OutModules, OutInputs);
		}
	}

	FStackInput* FindInput(TArray<FStackInput>& Inputs, const TCHAR* ModuleSubstring, FName InputName)
	{
		return Inputs.FindByPredicate([&](const FStackInput& Item)
		{
			return ModuleName(Item.Module).Contains(ModuleSubstring)
				&& Item.Input->GetInputParameterHandle().GetName() == InputName;
		});
	}

	void ListStack(FNiagaraSystemViewModel& ViewModel)
	{
		TArray<UNiagaraStackModuleItem*> Modules;
		TArray<FStackInput> Inputs;
		CollectAll(ViewModel, Modules, Inputs);
		for (const FStackInput& Item : Inputs)
		{
			const UNiagaraStackFunctionInput::EValueMode Mode = Item.Input->GetValueMode();
			UE_LOG(LogOutlierNiagaraTrail, Display, TEXT("STACK [%s] %s = %s%s"), *ModuleName(Item.Module),
				*Item.Input->GetInputParameterHandle().GetName().ToString(), ValueModeName(Mode),
				Mode == UNiagaraStackFunctionInput::EValueMode::Linked
					? *FString::Printf(TEXT(" -> %s"), *Item.Input->GetLinkedParameterValue().GetName().ToString()) : TEXT(""));
		}
	}

	bool CheckCompile(UNiagaraSystem* System)
	{
		System->RequestCompile(false);
		System->WaitForCompilationComplete(false, false);

		bool bOk = true;
		TArray<UNiagaraScript*> Scripts = { System->GetSystemSpawnScript(), System->GetSystemUpdateScript() };
		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			if (FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData())
			{
				Data->GetScripts(Scripts, false);
			}
		}
		for (UNiagaraScript* Script : Scripts)
		{
			if (!Script)
			{
				continue;
			}
			// 에미터 Spawn/Update 는 시스템 스크립트에 합쳐 컴파일되어 자체 상태는 항상 Unknown 이다.
			const ENiagaraScriptCompileStatus Status = Script->GetLastCompileStatus();
			const bool bMergedIntoSystem = Script->GetUsage() == ENiagaraScriptUsage::EmitterSpawnScript
				|| Script->GetUsage() == ENiagaraScriptUsage::EmitterUpdateScript;
			UE_LOG(LogOutlierNiagaraTrail, Display, TEXT("Compile status %s = %d"), *Script->GetName(), (int32)Status);
			if (Status == ENiagaraScriptCompileStatus::NCS_Error
				|| (Status == ENiagaraScriptCompileStatus::NCS_Unknown && !bMergedIntoSystem))
			{
				UE_LOG(LogOutlierNiagaraTrail, Error, TEXT("Compile failed: %s status=%d %s"), *Script->GetPathName(), (int32)Status,
					*Script->GetVMExecutableData().ErrorMsg);
				bOk = false;
			}
		}
		return bOk;
	}

	// Lyra 트레이서를 Start/End 구조로 바꾼다.
	// - 파티클 HitPosition: SelectVectorFromArray(User.ImpactPositions) -> User.End
	// - Spawn Per Frame 의 Spawn Count: 배열 길이( System.Num ) -> 1
	// - 배열 길이를 구하던 시스템 Scratch Module 제거, ImpactPositions / MuzzlePosition 유저 파라미터 제거
	// - 시작점은 그대로 컴포넌트 위치( 총구 )를 쓴다. User.Start 는 VisualEvent 쪽 이름을 맞추기 위해 노출만 한다.
	bool ConvertSystem(UNiagaraSystem* System, FNiagaraSystemViewModel& ViewModel)
	{
		FNiagaraUserRedirectionParameterStore& UserParameters = System->GetExposedParameters();
		const FNiagaraVariable StartVar(FNiagaraTypeDefinition::GetPositionDef(), StartName);
		const FNiagaraVariable EndVar(FNiagaraTypeDefinition::GetPositionDef(), EndName);
		UserParameters.AddParameter(StartVar, true, true);
		UserParameters.AddParameter(EndVar, true, true);

		TArray<UNiagaraStackModuleItem*> Modules;
		TArray<FStackInput> Inputs;
		CollectAll(ViewModel, Modules, Inputs);

		FStackInput* HitPosition = FindInput(Inputs, TEXT("HitPosition"), TEXT("Particles.HitPosition"));
		if (!HitPosition)
		{
			UE_LOG(LogOutlierNiagaraTrail, Error, TEXT("%s: HitPosition input not found"), *System->GetName());
			return false;
		}
		HitPosition->Input->SetLinkedParameterValue(EndVar);

		FStackInput* SpawnCount = FindInput(Inputs, TEXT("Spawn Per Frame"), TEXT("Spawn Count"));
		if (!SpawnCount)
		{
			UE_LOG(LogOutlierNiagaraTrail, Error, TEXT("%s: Spawn Per Frame.Spawn Count input not found"), *System->GetName());
			return false;
		}
		SpawnCount->Input->Reset();
		if (TSharedPtr<const FStructOnScope> Current = SpawnCount->Input->GetLocalValueStruct())
		{
			TSharedRef<FStructOnScope> One = MakeShared<FStructOnScope>(Current->GetStruct());
			*reinterpret_cast<int32*>(One->GetStructMemory()) = 1;
			SpawnCount->Input->SetLocalValue(One);
		}

		UNiagaraStackModuleItem** ScratchModule = Modules.FindByPredicate([](const UNiagaraStackModuleItem* Module)
		{
			return ModuleName(Module) == TEXT("Scratch Module");
		});
		if (ScratchModule)
		{
			(*ScratchModule)->Delete();
		}

		UserParameters.RemoveParameter(FNiagaraVariableBase(FNiagaraTypeDefinition(UNiagaraDataInterfaceArrayFloat3::StaticClass()), ImpactPositionsName));
		UserParameters.RemoveParameter(FNiagaraVariableBase(FNiagaraTypeDefinition::GetPositionDef(), MuzzlePositionName));
		return true;
	}

	// 탄속이 빨라 한 프레임에 수 미터를 가므로, 첫 프레임 뒤에는 꼬리가 총구를 떠나 떨어진 곳에서 나와 보인다.
	// 빈 구간은 Speed × 프레임 시간에 비례하므로 Tracer Update 의 Speed 를 낮춰 줄인다.
	// TracerLength( 한 프레임 이동 거리 대비 꼬리 비율 )와 SpriteSize 연결( Particles.SpriteSize )은 Lyra 원본 값을 유지한다.
	struct FTracerTuning
	{
		const TCHAR* SystemName;
		float Speed;
		float TracerLengthRatio;
	};
	const FTracerTuning Tunings[] = {
		{ TEXT("NS_Trail_Player_Original"), 10000.0f, 0.45f },
		{ TEXT("NS_Trail_Drone_Original"), 20000.0f, 1.0f },
		{ TEXT("NS_Trail_Turret_Original"), 20000.0f, 1.0f },
	};
	const FName SpriteSizeName(TEXT("Particles.SpriteSize"));

	bool SetLocalFloat(UNiagaraSystem* System, FStackInput* Input, const TCHAR* Label, float Value, bool& bInOutChanged)
	{
		TSharedPtr<const FStructOnScope> Current = Input ? Input->Input->GetLocalValueStruct() : nullptr;
		if (!Input || Input->Input->GetValueMode() != UNiagaraStackFunctionInput::EValueMode::Local || !Current.IsValid())
		{
			UE_LOG(LogOutlierNiagaraTrail, Error, TEXT("%s: %s is not a local value"), *System->GetName(), Label);
			return false;
		}
		const float CurrentValue = *reinterpret_cast<const float*>(Current->GetStructMemory());
		if (!FMath::IsNearlyEqual(CurrentValue, Value))
		{
			TSharedRef<FStructOnScope> NewValue = MakeShared<FStructOnScope>(Current->GetStruct());
			*reinterpret_cast<float*>(NewValue->GetStructMemory()) = Value;
			Input->Input->SetLocalValue(NewValue);
			UE_LOG(LogOutlierNiagaraTrail, Display, TEXT("%s: %s %.3f -> %.3f"), *System->GetName(), Label, CurrentValue, Value);
			bInOutChanged = true;
		}
		return true;
	}

	bool TuneSystem(UNiagaraSystem* System, FNiagaraSystemViewModel& ViewModel, bool& bOutChanged)
	{
		bOutChanged = false;
		const FTracerTuning* Tuning = Algo::FindByPredicate(Tunings, [System](const FTracerTuning& Item)
		{
			return System->GetName() == Item.SystemName;
		});
		if (!Tuning)
		{
			UE_LOG(LogOutlierNiagaraTrail, Error, TEXT("%s: no tuning values"), *System->GetName());
			return false;
		}

		TArray<UNiagaraStackModuleItem*> Modules;
		TArray<FStackInput> Inputs;
		CollectAll(ViewModel, Modules, Inputs);

		FStackInput* SpriteSize = FindInput(Inputs, TEXT("Tracer Update"), TEXT("SpriteSize"));
		if (!SpriteSize)
		{
			UE_LOG(LogOutlierNiagaraTrail, Error, TEXT("%s: Tracer Update SpriteSize input not found"), *System->GetName());
			return false;
		}
		if (SpriteSize->Input->GetValueMode() != UNiagaraStackFunctionInput::EValueMode::Linked
			|| SpriteSize->Input->GetLinkedParameterValue().GetName() != SpriteSizeName)
		{
			SpriteSize->Input->SetLinkedParameterValue(FNiagaraVariableBase(FNiagaraTypeDefinition::GetVec2Def(), SpriteSizeName));
			UE_LOG(LogOutlierNiagaraTrail, Display, TEXT("%s: SpriteSize -> %s"), *System->GetName(), *SpriteSizeName.ToString());
			bOutChanged = true;
		}

		return SetLocalFloat(System, FindInput(Inputs, TEXT("Tracer Update"), TEXT("Speed")), TEXT("Speed"), Tuning->Speed, bOutChanged)
			&& SetLocalFloat(System, FindInput(Inputs, TEXT("Tracer Update"), TEXT("TracerLength")), TEXT("TracerLength"), Tuning->TracerLengthRatio, bOutChanged);
	}
}

namespace OutlierNiagaraTrail
{
	int32 Run(bool bDump, bool bList, bool bConvert, bool bTune = false)
	{
		int32 Result = 0;
		for (const TCHAR* Path : SystemPaths)
		{
			UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, Path);
			if (!System)
			{
				UE_LOG(LogOutlierNiagaraTrail, Error, TEXT("Failed to load %s"), Path);
				return 1;
			}
			if (bDump)
			{
				DumpSystem(System);
			}
			if (!bList && !bConvert && !bTune)
			{
				continue;
			}

			// 스택 뷰모델은 입력 위젯( 커브 에디터 등 )을 만들기 때문에 Slate 가 있어야 한다. 커맨드릿에서는 쓸 수 없다.
			if (!FSlateApplication::IsInitialized())
			{
				UE_LOG(LogOutlierNiagaraTrail, Error, TEXT("-list / -convert need Slate. Run the editor with -ExecCmds=\"Outlier.NiagaraTrail convert\" instead of the commandlet."));
				return 1;
			}

			FNiagaraSystemViewModelOptions Options;
			Options.bCanAutoCompile = false;
			Options.bCanSimulate = false;
			Options.bCanModifyEmittersFromTimeline = false;
			Options.EditMode = ENiagaraSystemViewModelEditMode::SystemAsset;
			Options.MessageLogGuid = System->GetAssetGuid();
			TSharedRef<FNiagaraSystemViewModel> ViewModel = MakeShared<FNiagaraSystemViewModel>();
			ViewModel->Initialize(*System, Options);

			UE_LOG(LogOutlierNiagaraTrail, Display, TEXT("==== %s"), *System->GetName());
			if (bConvert && !System->GetExposedParameters().FindParameterVariable(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), EndName)))
			{
				if (!ConvertSystem(System, *ViewModel) || !CheckCompile(System))
				{
					Result = 1;
					continue;
				}
				System->MarkPackageDirty();
				if (!UEditorLoadingAndSavingUtils::SavePackages({ System->GetOutermost() }, false))
				{
					UE_LOG(LogOutlierNiagaraTrail, Error, TEXT("Failed to save %s"), *System->GetPathName());
					Result = 1;
					continue;
				}
				UE_LOG(LogOutlierNiagaraTrail, Display, TEXT("Converted and saved %s"), *System->GetName());
			}
			if (bTune)
			{
				bool bChanged = false;
				if (!TuneSystem(System, *ViewModel, bChanged) || (bChanged && !CheckCompile(System)))
				{
					Result = 1;
					continue;
				}
				if (bChanged)
				{
					System->MarkPackageDirty();
					if (!UEditorLoadingAndSavingUtils::SavePackages({ System->GetOutermost() }, false))
					{
						UE_LOG(LogOutlierNiagaraTrail, Error, TEXT("Failed to save %s"), *System->GetPathName());
						Result = 1;
						continue;
					}
					UE_LOG(LogOutlierNiagaraTrail, Display, TEXT("Tuned and saved %s"), *System->GetName());
				}
			}
			ListStack(*ViewModel);
		}
		UE_LOG(LogOutlierNiagaraTrail, Display, TEXT("RESULT %d"), Result);
		return Result;
	}

	FAutoConsoleCommand RunCommand(
		TEXT("Outlier.NiagaraTrail"),
		TEXT("Outlier.NiagaraTrail [dump] [list] [convert] [tune] - OutlierGunTrail 트레이서를 User.Start/End 구조로 맞추고( convert ), 총구 앞 빈 구간이 줄도록 Speed 를 조정한다( tune )."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			Run(Args.Contains(TEXT("dump")), Args.Contains(TEXT("list")), Args.Contains(TEXT("convert")), Args.Contains(TEXT("tune")));
		}));
}

UOutlierNiagaraTrailCommandlet::UOutlierNiagaraTrailCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UOutlierNiagaraTrailCommandlet::Main(const FString& Params)
{
	return OutlierNiagaraTrail::Run(FParse::Param(*Params, TEXT("dump")), FParse::Param(*Params, TEXT("list")), FParse::Param(*Params, TEXT("convert")));
}
