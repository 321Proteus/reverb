struct AEffect {
	int32_t magic;
	intptr_t (*dispatcher)(AEffect* effect, int32_t opcode, int32_t index,
		intptr_t value, void* ptr, float opt);
	void (*process)(AEffect* effect, float** inputs, float** outputs,
		int32_t sampleFrames); // deprecated, accumulating mode
	void (*setParameter)(AEffect* effect, int32_t index, float value);
	float (*getParameter)(AEffect* effect, int32_t index);
	int32_t numPrograms;
	int32_t numParams;
	int32_t numInputs;
	int32_t numOutputs;
	int32_t flags;
	intptr_t resvd1;
	intptr_t resvd2;
	int32_t initialDelay;
	int32_t realQualities; // deprecated
	int32_t offQualities;  // deprecated
	float ioRatio;		   // deprecated
	void* object;
	void* user;
	int32_t uniqueID;
	int32_t version;
	void (*processReplacing)(
		AEffect* effect, float** inputs, float** outputs, int32_t sampleFrames);
	void (*processDoubleReplacing)(AEffect* effect, double** inputs,
		double** outputs, int32_t sampleFrames);
	char future[56];
};

typedef intptr_t (*audioMasterCallback)(AEffect* effect, int32_t opcode,
	int32_t index, intptr_t value, void* ptr, float opt);

/*
	Sources:
	https://github.com/cgestes/multifxvst/blob/master/aeffectx.h
	https://github.com/LMMS/lmms/blob/master/include/aeffectx.h
*/
enum VstOpcode {
	effOpen = 0,
	effClose = 1,
	effSetProgram = 2,
	effGetProgram = 3,
	effSetProgramName = 4,
	effGetProgramName = 5,
	effGetParamLabel = 6,
	effGetParamDisplay = 7,
	effGetParamName = 8,
	effGetVu = 9,
	effSetSampleRate = 10,
	effSetBlockSize = 11,
	effMainsChanged = 12,
	effEditGetRect = 13,
	effEditOpen = 14,
	effEditClose = 15,
	effEditDraw = 16,
	effEditMouse = 17,
	effEditKey = 18,
	effEditIdle = 19,
	effEditTop = 20,
	effEditSleep = 21,
	effIdentify = 22,
	effGetChunk = 23,
	effSetChunk = 24,
	effProcessEvents = 25,
	effCanBeAutomated = 26,
	effString2Parameter = 27,
	effGetNumProgramCategories = 28,
	effGetProgramNameIndexed = 29,
	effCopyProgram = 30,
	effConnectInput = 31,
	effConnectOutput = 32,
	effGetInputProperties = 33,
	effGetOutputProperties = 34,
	effGetPlugCategory = 35,
	effGetCurrentPosition = 36,
	effGetDestinationBuffer = 37,
	effOfflineNotify = 38,
	effOfflinePrepare = 39,
	effOfflineRun = 40,
	effProcessVarIo = 41,
	effSetSpeakerArrangement = 42,
	effSetBlockSizeAndSampleRate = 43,
	effSetBypass = 44,
	effGetEffectName = 45,
	effGetErrorText = 46,
	effGetVendorString = 47,
	effGetProductString = 48,
	effGetVendorVersion = 49,
	effVendorSpecific = 50,
	effCanDo = 51,
	effGetTailSize = 52,
	effIdle = 53,
	effGetIcon = 54,
	effSetViewPosition = 55,
	effGetParameterProperties = 56,
	effKeysRequired = 57,
	effGetVstVersion = 58,
	effEditKeyDown = 59,
	effEditKeyUp = 60,
	effSetEditKnobMode = 61,
	effGetMidiProgramName = 62,
	effGetCurrentMidiProgram = 63,
	effGetMidiProgramCategory = 64,
	effHasMidiProgramsChanged = 65,
	effGetMidiKeyName = 66,
	effBeginSetProgram = 67,
	effEndSetProgram = 68,
	effGetSpeakerArrangement = 69,
	effShellGetNextPlugin = 70,
	effStartProcess = 71,
	effStopProcess = 72,
	effSetTotalSampleToProcess = 73,
	effSetPanLaw = 74,
	effBeginLoadBank = 75,
	effBeginLoadProgram = 76,
	effSetProcessPrecision = 77,
	effGetNumMidiInputChannels = 78,
	effGetNumMidiOutputChannels = 79
};

enum AudioMasterOpcode {
	audioMasterAutomate = 0,
	audioMasterVersion = 1,
	audioMasterCurrentId = 2,
	audioMasterIdle = 3,
	audioMasterPinConnected = 4,
	audioMasterWantMidi = 6,
	audioMasterGetTime = 7,
	audioMasterProcessEvents = 8,
	audioMasterSetTime = 9,
	audioMasterTempoAt = 10,
	audioMasterGetNumAutomatableParameters = 11,
	audioMasterGetParameterQuantization = 12,
	audioMasterIOChanged = 13,
	audioMasterNeedIdle = 14,
	audioMasterSizeWindow = 15,
	audioMasterGetSampleRate = 16,
	audioMasterGetBlockSize = 17,
	audioMasterGetInputLatency = 18,
	audioMasterGetOutputLatency = 19,
	audioMasterGetPreviousPlug = 20,
	audioMasterGetNextPlug = 21,
	audioMasterWillReplaceOrAccumulate = 22,
	audioMasterGetCurrentProcessLevel = 23,
	audioMasterGetAutomationState = 24,
	audioMasterOfflineStart = 25,
	audioMasterOfflineRead = 26,
	audioMasterOfflineWrite = 27,
	audioMasterOfflineGetCurrentPass = 28,
	audioMasterOfflineGetCurrentMetaPass = 29,
	audioMasterSetOutputSampleRate = 30,
	audioMasterGetSpeakerArrangement = 31,
	audioMasterGetVendorString = 32,
	audioMasterGetProductString = 33,
	audioMasterGetVendorVersion = 34,
	audioMasterVendorSpecific = 35,
	audioMasterSetIcon = 36,
	audioMasterCanDo = 37,
	audioMasterGetLanguage = 38,
	audioMasterOpenWindow = 39,
	audioMasterCloseWindow = 40,
	audioMasterGetDirectory = 41,
	audioMasterUpdateDisplay = 42,
	audioMasterBeginEdit = 43,
	audioMasterEndEdit = 44,
	audioMasterOpenFileSelector = 45,
	audioMasterCloseFileSelector = 46,
	audioMasterEditFile = 47,
	audioMasterGetChunkFile = 48,
	audioMasterGetInputSpeakerArrangement = 49
};