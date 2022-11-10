// Copyright (c) .NET Foundation and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "ProcDumpProfiler.h"
#include "corhlpr.h"
#include "CComPtr.h"
#include "profiler_pal.h"
#include <string>
#include <stdio.h>

INITIALIZE_EASYLOGGINGPP

//------------------------------------------------------------------------------------------------------------------------------------------------------
// CorProfiler::CorProfiler
//------------------------------------------------------------------------------------------------------------------------------------------------------
CorProfiler::CorProfiler() : refCount(0), corProfilerInfo8(nullptr), corProfilerInfo(nullptr)
{
    el::Loggers::reconfigureAllLoggers(el::ConfigurationType::Filename, LOG_FILE);
    el::Loggers::reconfigureAllLoggers(el::ConfigurationType::MaxLogFileSize, MAX_LOG_FILE_SIZE);

}

//------------------------------------------------------------------------------------------------------------------------------------------------------
// CorProfiler::~CorProfiler
//------------------------------------------------------------------------------------------------------------------------------------------------------
CorProfiler::~CorProfiler()
{
    Shutdown();
}

//------------------------------------------------------------------------------------------------------------------------------------------------------
// CorProfiler::InitializeForAttach
//------------------------------------------------------------------------------------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE CorProfiler::InitializeForAttach(IUnknown *pCorProfilerInfoUnk, void *pvClientData, UINT cbClientData)
{
    LOG(TRACE) << "Enter: CorProfiler::InitializeForAttach";

    char* filter = (char*) pvClientData;
    filter[cbClientData]='\0';

    // Convert to WCHAR
    WCHAR* fW = (WCHAR*) malloc((strlen(filter)+1)*sizeof(uint16_t));
    for(int i=0; i<(strlen(filter)+1); i++)
    {
        fW[i] = (uint16_t) filter[i];
    }

    HRESULT queryInterfaceResult = pCorProfilerInfoUnk->QueryInterface(__uuidof(ICorProfilerInfo8), reinterpret_cast<void **>(&this->corProfilerInfo8));
    if (FAILED(queryInterfaceResult))
    {
        return E_FAIL;
    }

    queryInterfaceResult = pCorProfilerInfoUnk->QueryInterface(__uuidof(ICorProfilerInfo), reinterpret_cast<void **>(&this->corProfilerInfo));
    if (FAILED(queryInterfaceResult))
    {
        return E_FAIL;
    }

    DWORD eventMask = COR_PRF_MONITOR_EXCEPTIONS;
    HRESULT hr = this->corProfilerInfo8->SetEventMask(eventMask);
    if(FAILED(hr))
    {
        return E_FAIL;
    }

    ParseExceptionList(fW);

    LOG(TRACE) << "Exit: CorProfiler::InitializeForAttach";
    return S_OK;
}

//------------------------------------------------------------------------------------------------------------------------------------------------------
// CorProfiler::ParseExceptionList
//------------------------------------------------------------------------------------------------------------------------------------------------------
bool CorProfiler::ParseExceptionList(WCHAR* filter)
{
    String filterW(filter);
    std::wstringstream exceptionFilter(filterW.ToCStr());

    std::wstring segment;
    std::vector<std::wstring> exclist;

    while(std::getline(exceptionFilter, segment, L';'))
    {
        exclist.push_back(segment);
    }

    std::vector<std::wstring> exceptionList;
    for(std::wstring exception : exclist)
    {
        std::wstring segment2;
        std::wstringstream stream(exception);
        ExceptionMonitorEntry entry;
        std::getline(stream, segment2, L':');

        entry.exception = (WCHAR*) malloc(segment2.length()*sizeof(uint16_t)+1);
        wchar_t* t = (wchar_t*) segment2.c_str();

        for(int i=0; i<segment2.length(); i++)
        {
            entry.exception[i] = segment2[i];
        }
        entry.exception[segment2.length()] = '\0';

        std::getline(stream, segment2, L':');
        entry.dumpsToCollect = std::stoi(segment2);

        exceptionMonitorList.push_back(entry);
    }

    for (auto & element : exceptionMonitorList)
    {
        String str(element.exception);
        LOG(TRACE) << "Exception filter " << str.ToCStr() << " with dump count set to " << std::to_wstring(element.dumpsToCollect);
    }

    return true;
}

//------------------------------------------------------------------------------------------------------------------------------------------------------
// CorProfiler::Initialize
//------------------------------------------------------------------------------------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE CorProfiler::Initialize(IUnknown *pICorProfilerInfoUnk)
{
    return S_OK;
}

//------------------------------------------------------------------------------------------------------------------------------------------------------
// CorProfiler::Shutdown
//------------------------------------------------------------------------------------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE CorProfiler::Shutdown()
{
    LOG(TRACE) << "Enter: CorProfiler::Shutdown";
    if (this->corProfilerInfo8 != nullptr)
    {
        this->corProfilerInfo8->Release();
        this->corProfilerInfo8 = nullptr;
    }

    if (this->corProfilerInfo != nullptr)
    {
        this->corProfilerInfo->Release();
        this->corProfilerInfo = nullptr;
    }

    LOG(TRACE) << "Exit: CorProfiler::Shutdown";
    return S_OK;
}

//------------------------------------------------------------------------------------------------------------------------------------------------------
// CorProfiler::ExceptionThrown
//------------------------------------------------------------------------------------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionThrown(ObjectID thrownObjectId)
{
    LOG(TRACE) << "Enter: CorProfiler::ExceptionThrown";

    String exceptionName = GetExceptionName(thrownObjectId);

    LOG(TRACE) << "Enter: CorProfiler::ExceptionThrown: " << exceptionName.ToCStr();

    // Check to see if we have any matches on exceptions
    for (auto & element : exceptionMonitorList)
    {
        String exc(element.exception);
        if(exceptionName==exc)
        {
            LOG(TRACE) << "Starting dump generation for exception " << exceptionName.ToCStr() << " with dump count set to " << std::to_string(element.dumpsToCollect);
        }
    }

    LOG(TRACE) << "Exit: CorProfiler::ExceptionThrown";
    return S_OK;
}

//------------------------------------------------------------------------------------------------------------------------------------------------------
// CorProfiler::GetExceptionName
//------------------------------------------------------------------------------------------------------------------------------------------------------
String CorProfiler::GetExceptionName(ObjectID objectId)
{
    LOG(TRACE) << "Enter: CorProfiler::GetExceptionName, objectId =" << objectId;

    String name;
    ClassID classId;
    ModuleID moduleId;
    mdTypeDef typeDefToken;
    IMetaDataImport* metadata;
    ULONG read = 0;

    HRESULT hRes = corProfilerInfo->GetClassFromObject(objectId, &classId);
    if(FAILED(hRes))
    {
        LOG(TRACE) << "Failed: CorProfiler::GetExceptionName in call to GetClassFromObject";
        return WCHAR("Failed_GetClassFromObject");
    }

    hRes = corProfilerInfo->GetClassIDInfo(classId, &moduleId, &typeDefToken);
    if(FAILED(hRes))
    {
        LOG(TRACE) << "Failed: CorProfiler::GetExceptionName in call to GetClassIDInfo";
        return WCHAR("Failed_GetClassIDInfo");
    }

    hRes = corProfilerInfo->GetModuleMetaData(moduleId, ofRead, IID_IMetaDataImport, (IUnknown**)&metadata);
    if(FAILED(hRes))
    {
        LOG(TRACE) << "Failed: CorProfiler::GetExceptionName in call to GetModuleMetaData";
        return WCHAR("Failed_GetModuleMetaData");
    }

    WCHAR funcName[1024];
    hRes = metadata->GetTypeDefProps(typeDefToken, funcName, 1024, &read, NULL, NULL);
    if(FAILED(hRes))
    {
        LOG(TRACE) << "Failed: CorProfiler::GetExceptionName in call to GetTypeDefProps";
        metadata->Release();
        return WCHAR("Failed_GetTypeDefProps");
    }

    name+=funcName;

    metadata->Release();
    return name;
}

// ========================================================================================================================
// ========================================================================================================================

HRESULT STDMETHODCALLTYPE CorProfiler::AppDomainCreationStarted(AppDomainID appDomainId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::AppDomainCreationFinished(AppDomainID appDomainId, HRESULT hrStatus)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::AppDomainShutdownStarted(AppDomainID appDomainId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::AppDomainShutdownFinished(AppDomainID appDomainId, HRESULT hrStatus)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::AssemblyLoadStarted(AssemblyID assemblyId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::AssemblyLoadFinished(AssemblyID assemblyId, HRESULT hrStatus)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::AssemblyUnloadStarted(AssemblyID assemblyId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::AssemblyUnloadFinished(AssemblyID assemblyId, HRESULT hrStatus)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ModuleLoadStarted(ModuleID moduleId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ModuleLoadFinished(ModuleID moduleId, HRESULT hrStatus)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ModuleUnloadStarted(ModuleID moduleId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ModuleUnloadFinished(ModuleID moduleId, HRESULT hrStatus)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ModuleAttachedToAssembly(ModuleID moduleId, AssemblyID AssemblyId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ClassLoadStarted(ClassID classId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ClassLoadFinished(ClassID classId, HRESULT hrStatus)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ClassUnloadStarted(ClassID classId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ClassUnloadFinished(ClassID classId, HRESULT hrStatus)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::FunctionUnloadStarted(FunctionID functionId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::JITCompilationStarted(FunctionID functionId, BOOL fIsSafeToBlock)
{
    return S_OK;}

HRESULT STDMETHODCALLTYPE CorProfiler::JITCompilationFinished(FunctionID functionId, HRESULT hrStatus, BOOL fIsSafeToBlock)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::JITCachedFunctionSearchStarted(FunctionID functionId, BOOL *pbUseCachedFunction)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::JITCachedFunctionSearchFinished(FunctionID functionId, COR_PRF_JIT_CACHE result)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::JITFunctionPitched(FunctionID functionId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::JITInlining(FunctionID callerId, FunctionID calleeId, BOOL *pfShouldInline)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ThreadCreated(ThreadID threadId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ThreadDestroyed(ThreadID threadId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ThreadAssignedToOSThread(ThreadID managedThreadId, DWORD osThreadId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RemotingClientInvocationStarted()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RemotingClientSendingMessage(GUID *pCookie, BOOL fIsAsync)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RemotingClientReceivingReply(GUID *pCookie, BOOL fIsAsync)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RemotingClientInvocationFinished()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RemotingServerReceivingMessage(GUID *pCookie, BOOL fIsAsync)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RemotingServerInvocationStarted()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RemotingServerInvocationReturned()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RemotingServerSendingReply(GUID *pCookie, BOOL fIsAsync)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::UnmanagedToManagedTransition(FunctionID functionId, COR_PRF_TRANSITION_REASON reason)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ManagedToUnmanagedTransition(FunctionID functionId, COR_PRF_TRANSITION_REASON reason)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RuntimeSuspendStarted(COR_PRF_SUSPEND_REASON suspendReason)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RuntimeSuspendFinished()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RuntimeSuspendAborted()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RuntimeResumeStarted()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RuntimeResumeFinished()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RuntimeThreadSuspended(ThreadID threadId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RuntimeThreadResumed(ThreadID threadId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::MovedReferences(ULONG cMovedObjectIDRanges, ObjectID oldObjectIDRangeStart[], ObjectID newObjectIDRangeStart[], ULONG cObjectIDRangeLength[])
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ObjectAllocated(ObjectID objectId, ClassID classId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ObjectsAllocatedByClass(ULONG cClassCount, ClassID classIds[], ULONG cObjects[])
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ObjectReferences(ObjectID objectId, ClassID classId, ULONG cObjectRefs, ObjectID objectRefIds[])
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RootReferences(ULONG cRootRefs, ObjectID rootRefIds[])
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionSearchFunctionEnter(FunctionID functionId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionSearchFunctionLeave()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionSearchFilterEnter(FunctionID functionId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionSearchFilterLeave()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionSearchCatcherFound(FunctionID functionId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionOSHandlerEnter(UINT_PTR __unused)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionOSHandlerLeave(UINT_PTR __unused)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionUnwindFunctionEnter(FunctionID functionId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionUnwindFunctionLeave()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionUnwindFinallyEnter(FunctionID functionId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionUnwindFinallyLeave()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionCatcherEnter(FunctionID functionId, ObjectID objectId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionCatcherLeave()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::COMClassicVTableCreated(ClassID wrappedClassId, REFGUID implementedIID, void *pVTable, ULONG cSlots)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::COMClassicVTableDestroyed(ClassID wrappedClassId, REFGUID implementedIID, void *pVTable)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionCLRCatcherFound()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ExceptionCLRCatcherExecute()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ThreadNameChanged(ThreadID threadId, ULONG cchName, WCHAR name[])
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::GarbageCollectionStarted(int cGenerations, BOOL generationCollected[], COR_PRF_GC_REASON reason)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::SurvivingReferences(ULONG cSurvivingObjectIDRanges, ObjectID objectIDRangeStart[], ULONG cObjectIDRangeLength[])
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::GarbageCollectionFinished()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::FinalizeableObjectQueued(DWORD finalizerFlags, ObjectID objectID)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::RootReferences2(ULONG cRootRefs, ObjectID rootRefIds[], COR_PRF_GC_ROOT_KIND rootKinds[], COR_PRF_GC_ROOT_FLAGS rootFlags[], UINT_PTR rootIds[])
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::HandleCreated(GCHandleID handleId, ObjectID initialObjectId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::HandleDestroyed(GCHandleID handleId)
{
    return S_OK;
}


HRESULT STDMETHODCALLTYPE CorProfiler::ProfilerAttachComplete()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ProfilerDetachSucceeded()
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ReJITCompilationStarted(FunctionID functionId, ReJITID rejitId, BOOL fIsSafeToBlock)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::GetReJITParameters(ModuleID moduleId, mdMethodDef methodId, ICorProfilerFunctionControl *pFunctionControl)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ReJITCompilationFinished(FunctionID functionId, ReJITID rejitId, HRESULT hrStatus, BOOL fIsSafeToBlock)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ReJITError(ModuleID moduleId, mdMethodDef methodId, FunctionID functionId, HRESULT hrStatus)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::MovedReferences2(ULONG cMovedObjectIDRanges, ObjectID oldObjectIDRangeStart[], ObjectID newObjectIDRangeStart[], SIZE_T cObjectIDRangeLength[])
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::SurvivingReferences2(ULONG cSurvivingObjectIDRanges, ObjectID objectIDRangeStart[], SIZE_T cObjectIDRangeLength[])
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ConditionalWeakTableElementReferences(ULONG cRootRefs, ObjectID keyRefIds[], ObjectID valueRefIds[], GCHandleID rootIds[])
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::GetAssemblyReferences(const WCHAR *wszAssemblyPath, ICorProfilerAssemblyReferenceProvider *pAsmRefProvider)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::ModuleInMemorySymbolsUpdated(ModuleID moduleId)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::DynamicMethodJITCompilationStarted(FunctionID functionId, BOOL fIsSafeToBlock, LPCBYTE ilHeader, ULONG cbILHeader)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CorProfiler::DynamicMethodJITCompilationFinished(FunctionID functionId, HRESULT hrStatus, BOOL fIsSafeToBlock)
{
    return S_OK;
}