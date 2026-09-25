IFDEF RAX ; # 64-bit

    EXTERNDEF LoadAPI:PROC

    API_EXPORT_ORIG MACRO API_NAME:REQ
        .CONST
            S_&API_NAME DB '&API_NAME', 0
        .CODE
        &API_NAME PROC FRAME
            ; Windows x64: 32-byte shadow space, four GP arguments, four XMM
            ; arguments, and 8-byte alignment padding. Stack arguments remain
            ; untouched when we restore RSP and tail-call the real export.
            sub rsp, 88h
            .ALLOCSTACK 88h
            .ENDPROLOG
            mov [rsp+20h], rcx
            mov [rsp+28h], rdx
            mov [rsp+30h], r8
            mov [rsp+38h], r9
            movdqu [rsp+40h], xmm0
            movdqu [rsp+50h], xmm1
            movdqu [rsp+60h], xmm2
            movdqu [rsp+70h], xmm3
            mov rcx, OFFSET S_&API_NAME
            call LoadAPI
            mov r11, rax
            mov rcx, [rsp+20h]
            mov rdx, [rsp+28h]
            mov r8, [rsp+30h]
            mov r9, [rsp+38h]
            movdqu xmm0, [rsp+40h]
            movdqu xmm1, [rsp+50h]
            movdqu xmm2, [rsp+60h]
            movdqu xmm3, [rsp+70h]
            add rsp, 88h
            jmp r11
        &API_NAME ENDP
    ENDM

 API_EXPORT_ORIG ClearReportsBetween_ExportThunk
    API_EXPORT_ORIG CrashForException_ExportThunk
    API_EXPORT_ORIG DisableHook
    API_EXPORT_ORIG DrainLog
    API_EXPORT_ORIG DumpHungProcessWithPtype_ExportThunk
    API_EXPORT_ORIG DumpProcessWithoutCrash
    API_EXPORT_ORIG GetApplyHookResult
    API_EXPORT_ORIG GetBlockedModulesCount
    API_EXPORT_ORIG GetCrashReports_ExportThunk
    API_EXPORT_ORIG GetCrashpadDatabasePath_ExportThunk
    API_EXPORT_ORIG GetHandleVerifier
    API_EXPORT_ORIG GetInstallDetailsPayload
    API_EXPORT_ORIG GetProductInfo_ExportThunk
    API_EXPORT_ORIG GetUniqueBlockedModulesCount
    API_EXPORT_ORIG GetUploadConsent_ExportThunk
    API_EXPORT_ORIG GetUserDataDirectoryThunk
    API_EXPORT_ORIG InjectDumpForHungInput_ExportThunk
    API_EXPORT_ORIG IsBrowserProcess
    API_EXPORT_ORIG IsCrashReportingEnabledImpl
    API_EXPORT_ORIG IsExtensionPointDisableSet
    API_EXPORT_ORIG IsTemporaryUserDataDirectoryCreatedForHeadless
    API_EXPORT_ORIG IsThirdPartyInitialized
    API_EXPORT_ORIG RegisterLogNotification
    API_EXPORT_ORIG RequestSingleCrashUpload_ExportThunk
    API_EXPORT_ORIG SetCrashKeyValueImpl
    API_EXPORT_ORIG SetMetricsClientId
    API_EXPORT_ORIG SetUploadConsent_ExportThunk
    API_EXPORT_ORIG SignalChromeElf
    API_EXPORT_ORIG SignalInitializeCrashReporting

ENDIF

END
