Scriptname DynamicFeedOverhaul Hidden
{Detection header for the Dynamic Feed Overhaul SKSE plugin.
 The natives are registered by the DLL - if it is not installed the calls
 return their default values (False / 0), which is the detection signal.}

; Returns True only when the SKSE plugin DLL is loaded.
Bool Function IsInstalled() global native

; API version, bumped on capability changes. 0 when the DLL is absent.
Int Function GetVersion() global native
