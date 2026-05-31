#include "NiagaraDataInterfaceExplosionVolume.h"

#include "NiagaraCompileHashVisitor.h"
#include "NiagaraGpuComputeDispatchInterface.h"
#include "NiagaraShaderParametersBuilder.h"
#include "RDGExplosionVolumeProvider.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"

#define LOCTEXT_NAMESPACE "UNiagaraDataInterfaceExplosionVolume"

const TCHAR* UNiagaraDataInterfaceExplosionVolume::TemplateShaderFilePath = TEXT("/Plugin/RDG/NiagaraDataInterfaceExplosionVolume.ush");
const FName UNiagaraDataInterfaceExplosionVolume::SampleExplosionVolumeUVWName(TEXT("SampleExplosionVolumeUVW"));
const FName UNiagaraDataInterfaceExplosionVolume::SampleExplosionVolumeWorldName(TEXT("SampleExplosionVolumeWorld"));
const FName UNiagaraDataInterfaceExplosionVolume::ExplosionVolumeDimensionsName(TEXT("ExplosionVolumeDimensions"));

struct FNiagaraDataInterfaceProxyExplosionVolume : public FNiagaraDataInterfaceProxy
{
	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) override
	{
		check(false);
	}

	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override
	{
		return 0;
	}
};

UNiagaraDataInterfaceExplosionVolume::UNiagaraDataInterfaceExplosionVolume()
{
	Proxy.Reset(new FNiagaraDataInterfaceProxyExplosionVolume());
}

void UNiagaraDataInterfaceExplosionVolume::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		ENiagaraTypeRegistryFlags Flags = ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter;
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), Flags);
	}
}

bool UNiagaraDataInterfaceExplosionVolume::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	return Super::CopyToInternal(Destination);
}

#if WITH_EDITORONLY_DATA
void UNiagaraDataInterfaceExplosionVolume::GetFunctionsInternal(TArray<FNiagaraFunctionSignature>& OutFunctions) const
{
	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = SampleExplosionVolumeUVWName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bSupportsCPU = false;
		Sig.bSupportsGPU = true;
		Sig.Inputs.Emplace(FNiagaraTypeDefinition(GetClass()), TEXT("Explosion Volume"));
		Sig.Inputs.Emplace(FNiagaraTypeDefinition::GetVec3Def(), TEXT("UVW"));
		Sig.Inputs.Emplace(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Mip Level"));
		Sig.Outputs.Emplace(FNiagaraTypeDefinition::GetVec4Def(), TEXT("Field"));
		Sig.SetDescription(LOCTEXT("SampleExplosionVolumeUVWDesc", "Sample the current RDG explosion velocity volume at explicit UVW coordinates."));
	}

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = SampleExplosionVolumeWorldName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bSupportsCPU = false;
		Sig.bSupportsGPU = true;
		Sig.Inputs.Emplace(FNiagaraTypeDefinition(GetClass()), TEXT("Explosion Volume"));
		Sig.Inputs.Emplace(FNiagaraTypeDefinition::GetPositionDef(), TEXT("World Position"));
		Sig.Inputs.Emplace(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Mip Level"));
		Sig.Outputs.Emplace(FNiagaraTypeDefinition::GetVec4Def(), TEXT("Field"));
		Sig.Outputs.Emplace(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Is Valid"));
		Sig.SetDescription(LOCTEXT("SampleExplosionVolumeWorldDesc", "Project a world position into the current view frustum and sample the RDG explosion velocity volume."));
	}

	{
		FNiagaraFunctionSignature& Sig = OutFunctions.AddDefaulted_GetRef();
		Sig.Name = ExplosionVolumeDimensionsName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.bSupportsCPU = false;
		Sig.bSupportsGPU = true;
		Sig.Inputs.Emplace(FNiagaraTypeDefinition(GetClass()), TEXT("Explosion Volume"));
		Sig.Outputs.Emplace(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Dimensions"));
		Sig.SetDescription(LOCTEXT("ExplosionVolumeDimensionsDesc", "Get the dimensions of the current RDG explosion velocity volume."));
	}
}

bool UNiagaraDataInterfaceExplosionVolume::AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const
{
	bool bSuccess = Super::AppendCompileHash(InVisitor);
	InVisitor->UpdateShaderFile(TemplateShaderFilePath);
	bSuccess &= InVisitor->UpdateShaderParameters<FShaderParameters>();
	return bSuccess;
}

void UNiagaraDataInterfaceExplosionVolume::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	const TMap<FString, FStringFormatArg> TemplateArgs =
	{
		{ TEXT("ParameterName"), ParamInfo.DataInterfaceHLSLSymbol },
	};
	AppendTemplateHLSL(OutHLSL, TemplateShaderFilePath, TemplateArgs);
}

bool UNiagaraDataInterfaceExplosionVolume::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	static const TSet<FName> ValidGpuFunctions =
	{
		SampleExplosionVolumeUVWName,
		SampleExplosionVolumeWorldName,
		ExplosionVolumeDimensionsName,
	};
	return ValidGpuFunctions.Contains(FunctionInfo.DefinitionName);
}
#endif

void UNiagaraDataInterfaceExplosionVolume::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc)
{
}

void UNiagaraDataInterfaceExplosionVolume::BuildShaderParameters(FNiagaraShaderParametersBuilder& ShaderParametersBuilder) const
{
	ShaderParametersBuilder.AddNestedStruct<FShaderParameters>();
}

void UNiagaraDataInterfaceExplosionVolume::SetShaderParameters(const FNiagaraDataInterfaceSetShaderParametersContext& Context) const
{
	FRDGBuilder& GraphBuilder = Context.GetGraphBuilder();
	FShaderParameters* Parameters = Context.GetParameterNestedStruct<FShaderParameters>();

	const FRDGExplosionVolumeFrameData FrameData = FRDGExplosionVolumeProvider::Get_RenderThread();
	Parameters->MaxDepth = FrameData.MaxDepth;
	Parameters->TextureSize = FVector3f(
		static_cast<float>(FrameData.TextureSize.X),
		static_cast<float>(FrameData.TextureSize.Y),
		static_cast<float>(FrameData.TextureSize.Z));
	Parameters->TextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	if (FrameData.Texture.IsValid() && Context.IsResourceBound(&Parameters->Texture))
	{
		FRDGTextureRef RDGTexture = GraphBuilder.RegisterExternalTexture(FrameData.Texture, TEXT("RDG.ExplosionVelocityVolume.Niagara"));
		//GraphBuilder.UseInternalAccessMode(RDGTexture);
		Context.GetRDGExternalAccessQueue().Add(RDGTexture);
		Parameters->Texture = RDGTexture;
	}
	else
	{
		Parameters->TextureSize = FVector3f::ZeroVector;
		Parameters->Texture = Context.GetComputeDispatchInterface().GetBlackTexture(GraphBuilder, ETextureDimension::Texture3D);
	}
}

#undef LOCTEXT_NAMESPACE
