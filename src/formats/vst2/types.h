/**
 * VST2 flat type declarations for REverb
 *
 * SPDX-License-Identifier: MIT
 *
 * Independent, unofficial interoperability declarations for the VST2 binary
 * interface (structure layouts, constants and class layouts as observed in
 * compiled plugins). Written by hand from binary evidence and from facts
 * (layouts, values) found in public reimplementations, including, but not
 * limited to:
 *
 * https://github.com/Xaymar/vst2sdk
 * https://git.iem.at/zmoelnig/FST/blob/main/fst/fst.h
 * https://github.com/mod-audio/mda-lv2/blob/master/lvz/audioeffectx.h
 * https://github.com/ELBIXishere/ELC4L
 * https://github.com/ossia/score/blob/master/src/plugins/score-plugin-vst/Vst/vst-compat.hpp
 * https://github.com/cgestes/multifxvst
 *
 * No text, comments or documentation from Steinberg's VST SDK were copied.
 *
 * Not affiliated with, authorised or endorsed by Steinberg Media Technologies
 * GmbH. "VST" is a trademark of Steinberg Media Technologies GmbH. Type and
 * field names are used only to identify and interoperate with existing
 * binaries.
 *
 * Provided as is, without warranty. Published in good faith; if you are a
 * rights holder with a concern, contact the repository maintainer and it will
 * be reviewed promptly.
 */

//
typedef struct AEffect AEffect;
typedef struct ERect ERect;
typedef struct VstEvent VstEvent;
typedef struct VstEvents VstEvents;
typedef struct VstMidiEvent VstMidiEvent;
typedef struct VstMidiSysexEvent VstMidiSysexEvent;
typedef struct VstTimeInfo VstTimeInfo;
typedef struct VstVariableIo VstVariableIo;
typedef struct VstParameterProperties VstParameterProperties;
typedef struct VstPinProperties VstPinProperties;
typedef struct MidiProgramName MidiProgramName;
typedef struct MidiProgramCategory MidiProgramCategory;
typedef struct MidiKeyName MidiKeyName;
typedef struct VstSpeakerProperties VstSpeakerProperties;
typedef struct VstSpeakerArrangement VstSpeakerArrangement;
typedef struct VstOfflineTask VstOfflineTask;
typedef struct VstAudioFile VstAudioFile;
typedef struct VstAudioFileMarker VstAudioFileMarker;
typedef struct VstWindow VstWindow;
typedef struct VstKeyCode VstKeyCode;
typedef struct VstFileType VstFileType;
typedef struct VstFileSelect VstFileSelect;
typedef struct VstPatchChunkInfo VstPatchChunkInfo;
typedef struct fxProgram fxProgram;
typedef struct fxBank fxBank;
typedef struct AudioEffect AudioEffect;
typedef struct AudioEffectX AudioEffectX;
typedef struct AEffEditor AEffEditor;
typedef struct AudioEffect_vtbl AudioEffect_vtbl;
typedef struct AudioEffectX_vtbl AudioEffectX_vtbl;
typedef struct AEffEditor_vtbl AEffEditor_vtbl;

typedef short VstInt16;

typedef int VstInt32;

typedef long long VstInt64;

typedef intptr_t VstIntPtr; // pointer-sized

typedef VstIntPtr (*audioMasterCallback)(AEffect* effect, VstInt32 opcode,
	VstInt32 index, VstIntPtr value, void* ptr, float opt);

typedef VstIntPtr (*AEffectDispatcherProc)(AEffect* effect, VstInt32 opcode,
	VstInt32 index, VstIntPtr value, void* ptr, float opt);

typedef void (*AEffectProcessProc)(
	AEffect* effect, float** inputs, float** outputs, VstInt32 sampleFrames);

typedef void (*AEffectProcessDoubleProc)(
	AEffect* effect, double** inputs, double** outputs, VstInt32 sampleFrames);

typedef void (*AEffectSetParameterProc)(
	AEffect* effect, VstInt32 index, float parameter);

typedef float (*AEffectGetParameterProc)(AEffect* effect, VstInt32 index);

struct AEffect {
	VstInt32 magic;
	AEffectDispatcherProc dispatcher;
	AEffectProcessProc process;
	AEffectSetParameterProc setParameter;
	AEffectGetParameterProc getParameter;
	VstInt32 numPrograms;
	VstInt32 numParams;
	VstInt32 numInputs;
	VstInt32 numOutputs;
	VstInt32 flags;
	VstIntPtr resvd1;
	VstIntPtr resvd2;
	VstInt32 initialDelay;
	VstInt32 realQualities;
	VstInt32 offQualities;
	float ioRatio;
	void* object;
	void* user;
	VstInt32 uniqueID;
	VstInt32 version;
	AEffectProcessProc processReplacing;
	AEffectProcessDoubleProc processDoubleReplacing;
	char future[56];
};

typedef enum VstAEffectFlags {
	effFlagsHasEditor = 1,
	effFlagsCanReplacing = 16,
	effFlagsProgramChunks = 32,
	effFlagsIsSynth = 256,
	effFlagsNoSoundInStop = 512,
	effFlagsCanDoubleReplacing = 4096,
	effFlagsHasClip = 2,
	effFlagsHasVu = 4,
	effFlagsCanMono = 8,
	effFlagsExtIsAsync = 1024,
	effFlagsExtHasBuffer = 2048,
} VstAEffectFlags;

typedef enum VstOpcode {
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
	effGetNumMidiOutputChannels = 79,
} VstOpcode;

typedef enum AudioMasterOpcode {
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
	audioMasterGetOutputSpeakerArrangement = 31,
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
	audioMasterGetInputSpeakerArrangement = 49,
} AudioMasterOpcode;

typedef enum VstStringConstants {
	kVstMaxProgNameLen = 24,
	kVstMaxParamStrLen = 8,
	kVstMaxVendorStrLen = 64,
	kVstMaxProductStrLen = 64,
	kVstMaxEffectNameLen = 32,
} VstStringConstants;

struct ERect {
	VstInt16 top;
	VstInt16 left;
	VstInt16 bottom;
	VstInt16 right;
};

typedef enum Vst2StringConstants {
	kVstMaxNameLen = 64,
	kVstMaxLabelLen = 64,
	kVstMaxShortLabelLen = 8,
	kVstMaxCategLabelLen = 24,
	kVstMaxFileNameLen = 100,
} Vst2StringConstants;

struct VstEvent {
	VstInt32 type;
	VstInt32 byteSize;
	VstInt32 deltaFrames;
	VstInt32 flags;
	char data[16];
};

typedef enum VstEventTypes {
	kVstMidiType = 1,
	kVstAudioType = 2,
	kVstVideoType = 3,
	kVstParameterType = 4,
	kVstTriggerType = 5,
	kVstSysExType = 6,
} VstEventTypes;

struct VstEvents {
	VstInt32 numEvents;
	VstIntPtr reserved;
	VstEvent* events[2];
};

struct VstMidiEvent {
	VstInt32 type;
	VstInt32 byteSize;
	VstInt32 deltaFrames;
	VstInt32 flags;
	VstInt32 noteLength;
	VstInt32 noteOffset;
	char midiData[4];
	char detune;
	char noteOffVelocity;
	char reserved1;
	char reserved2;
};

typedef enum VstMidiEventFlags {
	kVstMidiEventIsRealtime = 1,
} VstMidiEventFlags;

struct VstMidiSysexEvent {
	VstInt32 type;
	VstInt32 byteSize;
	VstInt32 deltaFrames;
	VstInt32 flags;
	VstInt32 dumpBytes;
	VstIntPtr resvd1;
	char* sysexDump;
	VstIntPtr resvd2;
};

struct VstTimeInfo {
	double samplePos;
	double sampleRate;
	double nanoSeconds;
	double ppqPos;
	double tempo;
	double barStartPos;
	double cycleStartPos;
	double cycleEndPos;
	VstInt32 timeSigNumerator;
	VstInt32 timeSigDenominator;
	VstInt32 smpteOffset;
	VstInt32 smpteFrameRate;
	VstInt32 samplesToNextClock;
	VstInt32 flags;
};

typedef enum VstTimeInfoFlags {
	kVstTransportChanged = 1,
	kVstTransportPlaying = 2,
	kVstTransportCycleActive = 4,
	kVstTransportRecording = 8,
	kVstAutomationWriting = 64,
	kVstAutomationReading = 128,
	kVstNanosValid = 256,
	kVstPpqPosValid = 512,
	kVstTempoValid = 1024,
	kVstBarsValid = 2048,
	kVstCyclePosValid = 4096,
	kVstTimeSigValid = 8192,
	kVstSmpteValid = 16384,
	kVstClockValid = 32768,
} VstTimeInfoFlags;

typedef enum VstSmpteFrameRate {
	kVstSmpte24fps = 0,
	kVstSmpte25fps = 1,
	kVstSmpte2997fps = 2,
	kVstSmpte30fps = 3,
	kVstSmpte2997dfps = 4,
	kVstSmpte30dfps = 5,
	kVstSmpteFilm16mm = 6,
	kVstSmpteFilm35mm = 7,
	kVstSmpte239fps = 10,
	kVstSmpte249fps = 11,
	kVstSmpte599fps = 12,
	kVstSmpte60fps = 13,
} VstSmpteFrameRate;

struct VstVariableIo {
	float** inputs;
	float** outputs;
	VstInt32 numSamplesInput;
	VstInt32 numSamplesOutput;
	VstInt32* numSamplesInputProcessed;
	VstInt32* numSamplesOutputProcessed;
};

typedef enum VstHostLanguage {
	kVstLangEnglish = 1,
	kVstLangGerman = 2,
	kVstLangFrench = 3,
	kVstLangItalian = 4,
	kVstLangSpanish = 5,
	kVstLangJapanese = 6,
} VstHostLanguage;

typedef enum VstProcessPrecision {
	kVstProcessPrecision32 = 0,
	kVstProcessPrecision64 = 1,
} VstProcessPrecision;

struct VstParameterProperties {
	float stepFloat;
	float smallStepFloat;
	float largeStepFloat;
	char label[64];
	VstInt32 flags;
	VstInt32 minInteger;
	VstInt32 maxInteger;
	VstInt32 stepInteger;
	VstInt32 largeStepInteger;
	char shortLabel[8];
	VstInt16 displayIndex;
	VstInt16 category;
	VstInt16 numParametersInCategory;
	VstInt16 reserved;
	char categoryLabel[24];
	char future[16];
};

typedef enum VstParameterFlags {
	kVstParameterIsSwitch = 1,
	kVstParameterUsesIntegerMinMax = 2,
	kVstParameterUsesFloatStep = 4,
	kVstParameterUsesIntStep = 8,
	kVstParameterSupportsDisplayIndex = 16,
	kVstParameterSupportsDisplayCategory = 32,
	kVstParameterCanRamp = 64,
} VstParameterFlags;

struct VstPinProperties {
	char label[64];
	VstInt32 flags;
	VstInt32 arrangementType;
	char shortLabel[8];
	char future[48];
};

typedef enum VstPinPropertiesFlags {
	kVstPinIsActive = 1,
	kVstPinIsStereo = 2,
	kVstPinUseSpeaker = 4,
} VstPinPropertiesFlags;

typedef enum VstPlugCategory {
	kPlugCategUnknown = 0,
	kPlugCategEffect = 1,
	kPlugCategSynth = 2,
	kPlugCategAnalysis = 3,
	kPlugCategMastering = 4,
	kPlugCategSpacializer = 5,
	kPlugCategRoomFx = 6,
	kPlugSurroundFx = 7,
	kPlugCategRestoration = 8,
	kPlugCategOfflineProcess = 9,
	kPlugCategShell = 10,
	kPlugCategGenerator = 11,
	kPlugCategMaxCount = 12,
} VstPlugCategory;

struct MidiProgramName {
	VstInt32 thisProgramIndex;
	char name[64];
	char midiProgram;
	char midiBankMsb;
	char midiBankLsb;
	char reserved;
	VstInt32 parentCategoryIndex;
	VstInt32 flags;
};

typedef enum VstMidiProgramNameFlags {
	kMidiIsOmni = 1,
} VstMidiProgramNameFlags;

struct MidiProgramCategory {
	VstInt32 thisCategoryIndex;
	char name[64];
	VstInt32 parentCategoryIndex;
	VstInt32 flags;
};

struct MidiKeyName {
	VstInt32 thisProgramIndex;
	VstInt32 thisKeyNumber;
	char keyName[64];
	VstInt32 reserved;
	VstInt32 flags;
};

struct VstSpeakerProperties {
	float azimuth;
	float elevation;
	float radius;
	float reserved;
	char name[64];
	VstInt32 type;
	char future[28];
};

struct VstSpeakerArrangement {
	VstInt32 type;
	VstInt32 numChannels;
	VstSpeakerProperties speakers[8];
};

typedef enum VstSpeakerType {
	kSpeakerUndefined = 2147483647,
	kSpeakerM = 0,
	kSpeakerL = 1,
	kSpeakerR = 2,
	kSpeakerC = 3,
	kSpeakerLfe = 4,
	kSpeakerLs = 5,
	kSpeakerRs = 6,
	kSpeakerLc = 7,
	kSpeakerRc = 8,
	kSpeakerS = 9,
	kSpeakerCs = 9,
	kSpeakerSl = 10,
	kSpeakerSr = 11,
	kSpeakerTm = 12,
	kSpeakerTfl = 13,
	kSpeakerTfc = 14,
	kSpeakerTfr = 15,
	kSpeakerTrl = 16,
	kSpeakerTrc = 17,
	kSpeakerTrr = 18,
	kSpeakerLfe2 = 19,
} VstSpeakerType;

typedef enum VstUserSpeakerType {
	kSpeakerU32 = -32,
	kSpeakerU31 = -31,
	kSpeakerU30 = -30,
	kSpeakerU29 = -29,
	kSpeakerU28 = -28,
	kSpeakerU27 = -27,
	kSpeakerU26 = -26,
	kSpeakerU25 = -25,
	kSpeakerU24 = -24,
	kSpeakerU23 = -23,
	kSpeakerU22 = -22,
	kSpeakerU21 = -21,
	kSpeakerU20 = -20,
	kSpeakerU19 = -19,
	kSpeakerU18 = -18,
	kSpeakerU17 = -17,
	kSpeakerU16 = -16,
	kSpeakerU15 = -15,
	kSpeakerU14 = -14,
	kSpeakerU13 = -13,
	kSpeakerU12 = -12,
	kSpeakerU11 = -11,
	kSpeakerU10 = -10,
	kSpeakerU9 = -9,
	kSpeakerU8 = -8,
	kSpeakerU7 = -7,
	kSpeakerU6 = -6,
	kSpeakerU5 = -5,
	kSpeakerU4 = -4,
	kSpeakerU3 = -3,
	kSpeakerU2 = -2,
	kSpeakerU1 = -1,
} VstUserSpeakerType;

typedef enum VstSpeakerArrangementType {
	kSpeakerArrUserDefined = -2,
	kSpeakerArrEmpty = -1,
	kSpeakerArrMono = 0,
	kSpeakerArrStereo = 1,
	kSpeakerArrStereoSurround = 2,
	kSpeakerArrStereoCenter = 3,
	kSpeakerArrStereoSide = 4,
	kSpeakerArrStereoCLfe = 5,
	kSpeakerArr30Cine = 6,
	kSpeakerArr30Music = 7,
	kSpeakerArr31Cine = 8,
	kSpeakerArr31Music = 9,
	kSpeakerArr40Cine = 10,
	kSpeakerArr40Music = 11,
	kSpeakerArr41Cine = 12,
	kSpeakerArr41Music = 13,
	kSpeakerArr50 = 14,
	kSpeakerArr51 = 15,
	kSpeakerArr60Cine = 16,
	kSpeakerArr60Music = 17,
	kSpeakerArr61Cine = 18,
	kSpeakerArr61Music = 19,
	kSpeakerArr70Cine = 20,
	kSpeakerArr70Music = 21,
	kSpeakerArr71Cine = 22,
	kSpeakerArr71Music = 23,
	kSpeakerArr80Cine = 24,
	kSpeakerArr80Music = 25,
	kSpeakerArr81Cine = 26,
	kSpeakerArr81Music = 27,
	kSpeakerArr102 = 28,
	kNumSpeakerArr = 29,
} VstSpeakerArrangementType;

struct VstOfflineTask {
	char processName[96];
	double readPosition;
	double writePosition;
	VstInt32 readCount;
	VstInt32 writeCount;
	VstInt32 sizeInputBuffer;
	VstInt32 sizeOutputBuffer;
	void* inputBuffer;
	void* outputBuffer;
	double positionToProcessFrom;
	double numFramesToProcess;
	double maxFramesToWrite;
	void* extraBuffer;
	VstInt32 value;
	VstInt32 index;
	double numFramesInSourceFile;
	double sourceSampleRate;
	double destinationSampleRate;
	VstInt32 numSourceChannels;
	VstInt32 numDestinationChannels;
	VstInt32 sourceFormat;
	VstInt32 destinationFormat;
	char outputText[512];
	double progress;
	VstInt32 progressMode;
	char progressText[100];
	VstInt32 flags;
	VstInt32 returnValue;
	void* hostOwned;
	void* plugOwned;
	char future[1024];
};

typedef enum VstOfflineTaskFlags {
	kVstOfflineUnvalidParameter = 1,
	kVstOfflineNewFile = 2,
	kVstOfflinePlugError = 1024,
	kVstOfflineInterleavedAudio = 2048,
	kVstOfflineTempOutputFile = 4096,
	kVstOfflineFloatOutputFile = 8192,
	kVstOfflineRandomWrite = 16384,
	kVstOfflineStretch = 32768,
	kVstOfflineNoThread = 65536,
} VstOfflineTaskFlags;

typedef enum VstOfflineOption {
	kVstOfflineAudio = 0,
	kVstOfflinePeaks = 1,
	kVstOfflineParameter = 2,
	kVstOfflineMarker = 3,
	kVstOfflineCursor = 4,
	kVstOfflineSelection = 5,
	kVstOfflineQueryFiles = 6,
} VstOfflineOption;

struct VstAudioFile {
	VstInt32 flags;
	void* hostOwned;
	void* plugOwned;
	char name[100];
	VstInt32 uniqueId;
	double sampleRate;
	VstInt32 numChannels;
	double numFrames;
	VstInt32 format;
	double editCursorPosition;
	double selectionStart;
	double selectionSize;
	VstInt32 selectedChannelsMask;
	VstInt32 numMarkers;
	VstInt32 timeRulerUnit;
	double timeRulerOffset;
	double tempo;
	VstInt32 timeSigNumerator;
	VstInt32 timeSigDenominator;
	VstInt32 ticksPerBlackNote;
	VstInt32 smpteFrameRate;
	char future[64];
};

typedef enum VstAudioFileFlags {
	kVstOfflineReadOnly = 1,
	kVstOfflineNoRateConversion = 2,
	kVstOfflineNoChannelChange = 4,
	kVstOfflineCanProcessSelection = 1024,
	kVstOfflineNoCrossfade = 2048,
	kVstOfflineWantRead = 4096,
	kVstOfflineWantWrite = 8192,
	kVstOfflineWantWriteMarker = 16384,
	kVstOfflineWantMoveCursor = 32768,
	kVstOfflineWantSelect = 65536,
} VstAudioFileFlags;

struct VstAudioFileMarker {
	double position;
	char name[32];
	VstInt32 type;
	VstInt32 id;
	VstInt32 reserved;
};

struct VstWindow {
	char title[128];
	VstInt16 xPos;
	VstInt16 yPos;
	VstInt16 width;
	VstInt16 height;
	VstInt32 style;
	void* parent;
	void* userHandle;
	void* winHandle;
	char future[104];
};

struct VstKeyCode {
	VstInt32 character;
	unsigned char virt;
	unsigned char modifier;
};

typedef enum VstVirtualKey {
	VKEY_BACK = 1,
	VKEY_TAB = 2,
	VKEY_CLEAR = 3,
	VKEY_RETURN = 4,
	VKEY_PAUSE = 5,
	VKEY_ESCAPE = 6,
	VKEY_SPACE = 7,
	VKEY_NEXT = 8,
	VKEY_END = 9,
	VKEY_HOME = 10,
	VKEY_LEFT = 11,
	VKEY_UP = 12,
	VKEY_RIGHT = 13,
	VKEY_DOWN = 14,
	VKEY_PAGEUP = 15,
	VKEY_PAGEDOWN = 16,
	VKEY_SELECT = 17,
	VKEY_PRINT = 18,
	VKEY_ENTER = 19,
	VKEY_SNAPSHOT = 20,
	VKEY_INSERT = 21,
	VKEY_DELETE = 22,
	VKEY_HELP = 23,
	VKEY_NUMPAD0 = 24,
	VKEY_NUMPAD1 = 25,
	VKEY_NUMPAD2 = 26,
	VKEY_NUMPAD3 = 27,
	VKEY_NUMPAD4 = 28,
	VKEY_NUMPAD5 = 29,
	VKEY_NUMPAD6 = 30,
	VKEY_NUMPAD7 = 31,
	VKEY_NUMPAD8 = 32,
	VKEY_NUMPAD9 = 33,
	VKEY_MULTIPLY = 34,
	VKEY_ADD = 35,
	VKEY_SEPARATOR = 36,
	VKEY_SUBTRACT = 37,
	VKEY_DECIMAL = 38,
	VKEY_DIVIDE = 39,
	VKEY_F1 = 40,
	VKEY_F2 = 41,
	VKEY_F3 = 42,
	VKEY_F4 = 43,
	VKEY_F5 = 44,
	VKEY_F6 = 45,
	VKEY_F7 = 46,
	VKEY_F8 = 47,
	VKEY_F9 = 48,
	VKEY_F10 = 49,
	VKEY_F11 = 50,
	VKEY_F12 = 51,
	VKEY_NUMLOCK = 52,
	VKEY_SCROLL = 53,
	VKEY_SHIFT = 54,
	VKEY_CONTROL = 55,
	VKEY_ALT = 56,
	VKEY_EQUALS = 57,
} VstVirtualKey;

typedef enum VstModifierKey {
	MODIFIER_SHIFT = 1,
	MODIFIER_ALTERNATE = 2,
	MODIFIER_COMMAND = 4,
	MODIFIER_CONTROL = 8,
} VstModifierKey;

struct VstFileType {
	char name[128];
	char macType[8];
	char dosType[8];
	char unixType[8];
	char mimeType1[128];
	char mimeType2[128];
};

struct VstFileSelect {
	VstInt32 command;
	VstInt32 type;
	VstInt32 macCreator;
	VstInt32 nbFileTypes;
	VstFileType* fileTypes;
	char title[1024];
	char* initialPath;
	char* returnPath;
	VstInt32 sizeReturnPath;
	char** returnMultiplePaths;
	VstInt32 nbReturnPath;
	VstIntPtr reserved;
	char future[116];
};

typedef enum VstFileSelectCommand {
	kVstFileLoad = 0,
	kVstFileSave = 1,
	kVstMultipleFilesLoad = 2,
	kVstDirectorySelect = 3,
} VstFileSelectCommand;

typedef enum VstFileSelectType {
	kVstFileType = 0,
} VstFileSelectType;

struct VstPatchChunkInfo {
	VstInt32 version;
	VstInt32 pluginUniqueID;
	VstInt32 pluginVersion;
	VstInt32 numElements;
	char future[48];
};

typedef enum VstPanLawType {
	kLinearPanLaw = 0,
	kEqualPowerPanLaw = 1,
} VstPanLawType;

typedef enum VstProcessLevels {
	kVstProcessLevelUnknown = 0,
	kVstProcessLevelUser = 1,
	kVstProcessLevelRealtime = 2,
	kVstProcessLevelPrefetch = 3,
	kVstProcessLevelOffline = 4,
} VstProcessLevels;

typedef enum VstAutomationStates {
	kVstAutomationUnsupported = 0,
	kVstAutomationOff = 1,
	kVstAutomationRead = 2,
	kVstAutomationWrite = 3,
	kVstAutomationReadWrite = 4,
} VstAutomationStates;

struct fxProgram {
	VstInt32 chunkMagic;
	VstInt32 byteSize;
	VstInt32 fxMagic;
	VstInt32 version;
	VstInt32 fxID;
	VstInt32 fxVersion;
	VstInt32 numParams;
	char prgName[28];
	union {
		float params[1];
		struct {
			VstInt32 size;
			char chunk[1];
		} data;
	} content;
};

struct fxBank {
	VstInt32 chunkMagic;
	VstInt32 byteSize;
	VstInt32 fxMagic;
	VstInt32 version;
	VstInt32 fxID;
	VstInt32 fxVersion;
	VstInt32 numPrograms;
	VstInt32 currentProgram;
	char future[124];
	union {
		fxProgram programs[1];
		struct {
			VstInt32 size;
			char chunk[1];
		} data;
	} content;
};

struct AudioEffect_vtbl {
	void(__RV_THISCALL* __dtor)(AudioEffect* __hidden this);
	__RV_ITANIUM_ONLY void(__RV_THISCALL* __deleting_dtor)(
		AudioEffect* __hidden this);
	VstIntPtr(__RV_THISCALL* dispatcher)(AudioEffect* __hidden this,
		VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt);
	void(__RV_THISCALL* open)(AudioEffect* __hidden this);
	void(__RV_THISCALL* close)(AudioEffect* __hidden this);
	void(__RV_THISCALL* suspend)(AudioEffect* __hidden this);
	void(__RV_THISCALL* resume)(AudioEffect* __hidden this);
	void(__RV_THISCALL* setSampleRate)(
		AudioEffect* __hidden this, float sampleRate);
	void(__RV_THISCALL* setBlockSize)(
		AudioEffect* __hidden this, VstInt32 blockSize);
	void(__RV_THISCALL* processReplacing)(AudioEffect* __hidden this,
		float** inputs, float** outputs, VstInt32 sampleFrames);
	void(__RV_THISCALL* processDoubleReplacing)(AudioEffect* __hidden this,
		double** inputs, double** outputs, VstInt32 sampleFrames);
	void(__RV_THISCALL* setParameter)(
		AudioEffect* __hidden this, VstInt32 index, float value);
	float(__RV_THISCALL* getParameter)(
		AudioEffect* __hidden this, VstInt32 index);
	void(__RV_THISCALL* setParameterAutomated)(
		AudioEffect* __hidden this, VstInt32 index, float value);
	VstInt32(__RV_THISCALL* getProgram)(AudioEffect* __hidden this);
	void(__RV_THISCALL* setProgram)(
		AudioEffect* __hidden this, VstInt32 program);
	void(__RV_THISCALL* setProgramName)(AudioEffect* __hidden this, char* name);
	void(__RV_THISCALL* getProgramName)(AudioEffect* __hidden this, char* name);
	void(__RV_THISCALL* getParameterLabel)(
		AudioEffect* __hidden this, VstInt32 index, char* label);
	void(__RV_THISCALL* getParameterDisplay)(
		AudioEffect* __hidden this, VstInt32 index, char* text);
	void(__RV_THISCALL* getParameterName)(
		AudioEffect* __hidden this, VstInt32 index, char* text);
	VstInt32(__RV_THISCALL* getChunk)(
		AudioEffect* __hidden this, void** data, bool isPreset);
	VstInt32(__RV_THISCALL* setChunk)(AudioEffect* __hidden this, void* data,
		VstInt32 byteSize, bool isPreset);
	void(__RV_THISCALL* setUniqueID)(AudioEffect* __hidden this, VstInt32 iD);
	void(__RV_THISCALL* setNumInputs)(
		AudioEffect* __hidden this, VstInt32 inputs);
	void(__RV_THISCALL* setNumOutputs)(
		AudioEffect* __hidden this, VstInt32 outputs);
	void(__RV_THISCALL* canProcessReplacing)(
		AudioEffect* __hidden this, bool state);
	void(__RV_THISCALL* canDoubleReplacing)(
		AudioEffect* __hidden this, bool state);
	void(__RV_THISCALL* programsAreChunks)(
		AudioEffect* __hidden this, bool state);
	void(__RV_THISCALL* setInitialDelay)(
		AudioEffect* __hidden this, VstInt32 delay);
	AEffEditor*(__RV_THISCALL* getEditor)(AudioEffect* __hidden this);
	AEffect*(__RV_THISCALL* getAeffect)(AudioEffect* __hidden this);
	float(__RV_THISCALL* getSampleRate)(AudioEffect* __hidden this);
	VstInt32(__RV_THISCALL* getBlockSize)(AudioEffect* __hidden this);
	VstInt32(__RV_THISCALL* getMasterVersion)(AudioEffect* __hidden this);
	VstInt32(__RV_THISCALL* getCurrentUniqueId)(AudioEffect* __hidden this);
	void(__RV_THISCALL* masterIdle)(AudioEffect* __hidden this);
	void(__RV_THISCALL* dB2string)(
		AudioEffect* __hidden this, float value, char* text, VstInt32 maxLen);
	void(__RV_THISCALL* Hz2string)(
		AudioEffect* __hidden this, float samples, char* text, VstInt32 maxLen);
	void(__RV_THISCALL* ms2string)(
		AudioEffect* __hidden this, float samples, char* text, VstInt32 maxLen);
	void(__RV_THISCALL* float2string)(
		AudioEffect* __hidden this, float value, char* text, VstInt32 maxLen);
	void(__RV_THISCALL* int2string)(AudioEffect* __hidden this, VstInt32 value,
		char* text, VstInt32 maxLen);
	void(__RV_THISCALL* process)(AudioEffect* __hidden this, float** inputs,
		float** outputs, VstInt32 sampleFrames);
	float(__RV_THISCALL* getVu)(AudioEffect* __hidden this);
	void(__RV_THISCALL* hasVu)(AudioEffect* __hidden this, bool state);
	void(__RV_THISCALL* hasClip)(AudioEffect* __hidden this, bool state);
	void(__RV_THISCALL* canMono)(AudioEffect* __hidden this, bool state);
	void(__RV_THISCALL* setRealtimeQualities)(
		AudioEffect* __hidden this, VstInt32 qualities);
	void(__RV_THISCALL* setOfflineQualities)(
		AudioEffect* __hidden this, VstInt32 qualities);
	bool(__RV_THISCALL* isInputConnected)(
		AudioEffect* __hidden this, VstInt32 input);
	bool(__RV_THISCALL* isOutputConnected)(
		AudioEffect* __hidden this, VstInt32 output);
};

struct AudioEffect {
	AudioEffect_vtbl* __vftable;
	audioMasterCallback audioMaster;
	AEffEditor* editor;
	float sampleRate;
	VstInt32 blockSize;
	VstInt32 numPrograms;
	VstInt32 numParams;
	VstInt32 curProgram;
	AEffect cEffect;
};

struct AudioEffectX_vtbl {
	void(__RV_THISCALL* __dtor)(AudioEffectX* __hidden this);
	__RV_ITANIUM_ONLY void(__RV_THISCALL* __deleting_dtor)(
		AudioEffectX* __hidden this);
	VstIntPtr(__RV_THISCALL* dispatcher)(AudioEffectX* __hidden this,
		VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt);
	void(__RV_THISCALL* open)(AudioEffectX* __hidden this);
	void(__RV_THISCALL* close)(AudioEffectX* __hidden this);
	void(__RV_THISCALL* suspend)(AudioEffectX* __hidden this);
	void(__RV_THISCALL* resume)(AudioEffectX* __hidden this);
	void(__RV_THISCALL* setSampleRate)(
		AudioEffectX* __hidden this, float sampleRate);
	void(__RV_THISCALL* setBlockSize)(
		AudioEffectX* __hidden this, VstInt32 blockSize);
	void(__RV_THISCALL* processReplacing)(AudioEffectX* __hidden this,
		float** inputs, float** outputs, VstInt32 sampleFrames);
	void(__RV_THISCALL* processDoubleReplacing)(AudioEffectX* __hidden this,
		double** inputs, double** outputs, VstInt32 sampleFrames);
	void(__RV_THISCALL* setParameter)(
		AudioEffectX* __hidden this, VstInt32 index, float value);
	float(__RV_THISCALL* getParameter)(
		AudioEffectX* __hidden this, VstInt32 index);
	void(__RV_THISCALL* setParameterAutomated)(
		AudioEffectX* __hidden this, VstInt32 index, float value);
	VstInt32(__RV_THISCALL* getProgram)(AudioEffectX* __hidden this);
	void(__RV_THISCALL* setProgram)(
		AudioEffectX* __hidden this, VstInt32 program);
	void(__RV_THISCALL* setProgramName)(
		AudioEffectX* __hidden this, char* name);
	void(__RV_THISCALL* getProgramName)(
		AudioEffectX* __hidden this, char* name);
	void(__RV_THISCALL* getParameterLabel)(
		AudioEffectX* __hidden this, VstInt32 index, char* label);
	void(__RV_THISCALL* getParameterDisplay)(
		AudioEffectX* __hidden this, VstInt32 index, char* text);
	void(__RV_THISCALL* getParameterName)(
		AudioEffectX* __hidden this, VstInt32 index, char* text);
	VstInt32(__RV_THISCALL* getChunk)(
		AudioEffectX* __hidden this, void** data, bool isPreset);
	VstInt32(__RV_THISCALL* setChunk)(AudioEffectX* __hidden this, void* data,
		VstInt32 byteSize, bool isPreset);
	void(__RV_THISCALL* setUniqueID)(AudioEffectX* __hidden this, VstInt32 iD);
	void(__RV_THISCALL* setNumInputs)(
		AudioEffectX* __hidden this, VstInt32 inputs);
	void(__RV_THISCALL* setNumOutputs)(
		AudioEffectX* __hidden this, VstInt32 outputs);
	void(__RV_THISCALL* canProcessReplacing)(
		AudioEffectX* __hidden this, bool state);
	void(__RV_THISCALL* canDoubleReplacing)(
		AudioEffectX* __hidden this, bool state);
	void(__RV_THISCALL* programsAreChunks)(
		AudioEffectX* __hidden this, bool state);
	void(__RV_THISCALL* setInitialDelay)(
		AudioEffectX* __hidden this, VstInt32 delay);
	AEffEditor*(__RV_THISCALL* getEditor)(AudioEffectX* __hidden this);
	AEffect*(__RV_THISCALL* getAeffect)(AudioEffectX* __hidden this);
	float(__RV_THISCALL* getSampleRate)(AudioEffectX* __hidden this);
	VstInt32(__RV_THISCALL* getBlockSize)(AudioEffectX* __hidden this);
	VstInt32(__RV_THISCALL* getMasterVersion)(AudioEffectX* __hidden this);
	VstInt32(__RV_THISCALL* getCurrentUniqueId)(AudioEffectX* __hidden this);
	void(__RV_THISCALL* masterIdle)(AudioEffectX* __hidden this);
	void(__RV_THISCALL* dB2string)(
		AudioEffectX* __hidden this, float value, char* text, VstInt32 maxLen);
	void(__RV_THISCALL* Hz2string)(AudioEffectX* __hidden this, float samples,
		char* text, VstInt32 maxLen);
	void(__RV_THISCALL* ms2string)(AudioEffectX* __hidden this, float samples,
		char* text, VstInt32 maxLen);
	void(__RV_THISCALL* float2string)(
		AudioEffectX* __hidden this, float value, char* text, VstInt32 maxLen);
	void(__RV_THISCALL* int2string)(AudioEffectX* __hidden this, VstInt32 value,
		char* text, VstInt32 maxLen);
	void(__RV_THISCALL* process)(AudioEffectX* __hidden this, float** inputs,
		float** outputs, VstInt32 sampleFrames);
	float(__RV_THISCALL* getVu)(AudioEffectX* __hidden this);
	void(__RV_THISCALL* hasVu)(AudioEffectX* __hidden this, bool state);
	void(__RV_THISCALL* hasClip)(AudioEffectX* __hidden this, bool state);
	void(__RV_THISCALL* canMono)(AudioEffectX* __hidden this, bool state);
	void(__RV_THISCALL* setRealtimeQualities)(
		AudioEffectX* __hidden this, VstInt32 qualities);
	void(__RV_THISCALL* setOfflineQualities)(
		AudioEffectX* __hidden this, VstInt32 qualities);
	bool(__RV_THISCALL* isInputConnected)(
		AudioEffectX* __hidden this, VstInt32 input);
	bool(__RV_THISCALL* isOutputConnected)(
		AudioEffectX* __hidden this, VstInt32 output);
	bool(__RV_THISCALL* canParameterBeAutomated)(
		AudioEffectX* __hidden this, VstInt32 index);
	bool(__RV_THISCALL* string2parameter)(
		AudioEffectX* __hidden this, VstInt32 index, char* text);
	bool(__RV_THISCALL* getParameterProperties)(
		AudioEffectX* __hidden this, VstInt32 index, VstParameterProperties* p);
	bool(__RV_THISCALL* beginEdit)(AudioEffectX* __hidden this, VstInt32 index);
	bool(__RV_THISCALL* endEdit)(AudioEffectX* __hidden this, VstInt32 index);
	bool(__RV_THISCALL* getProgramNameIndexed)(AudioEffectX* __hidden this,
		VstInt32 category, VstInt32 index, char* text);
	bool(__RV_THISCALL* beginSetProgram)(AudioEffectX* __hidden this);
	bool(__RV_THISCALL* endSetProgram)(AudioEffectX* __hidden this);
	VstInt32(__RV_THISCALL* beginLoadBank)(
		AudioEffectX* __hidden this, VstPatchChunkInfo* ptr);
	VstInt32(__RV_THISCALL* beginLoadProgram)(
		AudioEffectX* __hidden this, VstPatchChunkInfo* ptr);
	bool(__RV_THISCALL* ioChanged)(AudioEffectX* __hidden this);
	double(__RV_THISCALL* updateSampleRate)(AudioEffectX* __hidden this);
	VstInt32(__RV_THISCALL* updateBlockSize)(AudioEffectX* __hidden this);
	VstInt32(__RV_THISCALL* getInputLatency)(AudioEffectX* __hidden this);
	VstInt32(__RV_THISCALL* getOutputLatency)(AudioEffectX* __hidden this);
	bool(__RV_THISCALL* getInputProperties)(AudioEffectX* __hidden this,
		VstInt32 index, VstPinProperties* properties);
	bool(__RV_THISCALL* getOutputProperties)(AudioEffectX* __hidden this,
		VstInt32 index, VstPinProperties* properties);
	bool(__RV_THISCALL* setSpeakerArrangement)(AudioEffectX* __hidden this,
		VstSpeakerArrangement* pluginInput,
		VstSpeakerArrangement* pluginOutput);
	bool(__RV_THISCALL* getSpeakerArrangement)(AudioEffectX* __hidden this,
		VstSpeakerArrangement** pluginInput,
		VstSpeakerArrangement** pluginOutput);
	bool(__RV_THISCALL* setBypass)(AudioEffectX* __hidden this, bool onOff);
	bool(__RV_THISCALL* setPanLaw)(
		AudioEffectX* __hidden this, VstInt32 type, float val);
	bool(__RV_THISCALL* setProcessPrecision)(
		AudioEffectX* __hidden this, VstInt32 precision);
	VstInt32(__RV_THISCALL* getNumMidiInputChannels)(
		AudioEffectX* __hidden this);
	VstInt32(__RV_THISCALL* getNumMidiOutputChannels)(
		AudioEffectX* __hidden this);
	VstTimeInfo*(__RV_THISCALL* getTimeInfo)(
		AudioEffectX* __hidden this, VstInt32 filter);
	VstInt32(__RV_THISCALL* getCurrentProcessLevel)(
		AudioEffectX* __hidden this);
	VstInt32(__RV_THISCALL* getAutomationState)(AudioEffectX* __hidden this);
	VstInt32(__RV_THISCALL* processEvents)(
		AudioEffectX* __hidden this, VstEvents* events);
	VstInt32(__RV_THISCALL* startProcess)(AudioEffectX* __hidden this);
	VstInt32(__RV_THISCALL* stopProcess)(AudioEffectX* __hidden this);
	bool(__RV_THISCALL* processVariableIo)(
		AudioEffectX* __hidden this, VstVariableIo* varIo);
	VstInt32(__RV_THISCALL* setTotalSampleToProcess)(
		AudioEffectX* __hidden this, VstInt32 value);
	bool(__RV_THISCALL* getHostVendorString)(
		AudioEffectX* __hidden this, char* text);
	bool(__RV_THISCALL* getHostProductString)(
		AudioEffectX* __hidden this, char* text);
	VstInt32(__RV_THISCALL* getHostVendorVersion)(AudioEffectX* __hidden this);
	VstIntPtr(__RV_THISCALL* hostVendorSpecific)(AudioEffectX* __hidden this,
		VstInt32 lArg1, VstIntPtr lArg2, void* ptrArg, float floatArg);
	VstInt32(__RV_THISCALL* canHostDo)(AudioEffectX* __hidden this, char* text);
	VstInt32(__RV_THISCALL* getHostLanguage)(AudioEffectX* __hidden this);
	void(__RV_THISCALL* isSynth)(AudioEffectX* __hidden this, bool state);
	void(__RV_THISCALL* noTail)(AudioEffectX* __hidden this, bool state);
	VstInt32(__RV_THISCALL* getGetTailSize)(AudioEffectX* __hidden this);
	void*(__RV_THISCALL* getDirectory)(AudioEffectX* __hidden this);
	bool(__RV_THISCALL* getEffectName)(AudioEffectX* __hidden this, char* name);
	bool(__RV_THISCALL* getVendorString)(
		AudioEffectX* __hidden this, char* text);
	bool(__RV_THISCALL* getProductString)(
		AudioEffectX* __hidden this, char* text);
	VstInt32(__RV_THISCALL* getVendorVersion)(AudioEffectX* __hidden this);
	VstIntPtr(__RV_THISCALL* vendorSpecific)(AudioEffectX* __hidden this,
		VstInt32 lArg, VstIntPtr lArg2, void* ptrArg, float floatArg);
	VstInt32(__RV_THISCALL* canDo)(AudioEffectX* __hidden this, char* text);
	VstInt32(__RV_THISCALL* getVstVersion)(AudioEffectX* __hidden this);
	VstPlugCategory(__RV_THISCALL* getPlugCategory)(
		AudioEffectX* __hidden this);
	VstInt32(__RV_THISCALL* getMidiProgramName)(AudioEffectX* __hidden this,
		VstInt32 channel, MidiProgramName* midiProgramName);
	VstInt32(__RV_THISCALL* getCurrentMidiProgram)(AudioEffectX* __hidden this,
		VstInt32 channel, MidiProgramName* currentProgram);
	VstInt32(__RV_THISCALL* getMidiProgramCategory)(AudioEffectX* __hidden this,
		VstInt32 channel, MidiProgramCategory* category);
	bool(__RV_THISCALL* hasMidiProgramsChanged)(
		AudioEffectX* __hidden this, VstInt32 channel);
	bool(__RV_THISCALL* getMidiKeyName)(
		AudioEffectX* __hidden this, VstInt32 channel, MidiKeyName* keyName);
	bool(__RV_THISCALL* updateDisplay)(AudioEffectX* __hidden this);
	bool(__RV_THISCALL* sizeWindow)(
		AudioEffectX* __hidden this, VstInt32 width, VstInt32 height);
	bool(__RV_THISCALL* openFileSelector)(
		AudioEffectX* __hidden this, VstFileSelect* ptr);
	bool(__RV_THISCALL* closeFileSelector)(
		AudioEffectX* __hidden this, VstFileSelect* ptr);
	VstInt32(__RV_THISCALL* getNextShellPlugin)(
		AudioEffectX* __hidden this, char* name);
	bool(__RV_THISCALL* allocateArrangement)(AudioEffectX* __hidden this,
		VstSpeakerArrangement** arrangement, VstInt32 nbChannels);
	bool(__RV_THISCALL* deallocateArrangement)(
		AudioEffectX* __hidden this, VstSpeakerArrangement** arrangement);
	bool(__RV_THISCALL* copySpeaker)(AudioEffectX* __hidden this,
		VstSpeakerProperties* to, VstSpeakerProperties* from);
	bool(__RV_THISCALL* matchArrangement)(AudioEffectX* __hidden this,
		VstSpeakerArrangement** to, VstSpeakerArrangement* from);
	bool(__RV_THISCALL* offlineRead)(AudioEffectX* __hidden this,
		VstOfflineTask* offline, VstOfflineOption option, bool readSource);
	bool(__RV_THISCALL* offlineWrite)(AudioEffectX* __hidden this,
		VstOfflineTask* offline, VstOfflineOption option);
	bool(__RV_THISCALL* offlineStart)(AudioEffectX* __hidden this,
		VstAudioFile* ptr, VstInt32 numAudioFiles, VstInt32 numNewAudioFiles);
	VstInt32(__RV_THISCALL* offlineGetCurrentPass)(AudioEffectX* __hidden this);
	VstInt32(__RV_THISCALL* offlineGetCurrentMetaPass)(
		AudioEffectX* __hidden this);
	bool(__RV_THISCALL* offlineNotify)(AudioEffectX* __hidden this,
		VstAudioFile* ptr, VstInt32 numAudioFiles, bool start);
	bool(__RV_THISCALL* offlinePrepare)(
		AudioEffectX* __hidden this, VstOfflineTask* offline, VstInt32 count);
	bool(__RV_THISCALL* offlineRun)(
		AudioEffectX* __hidden this, VstOfflineTask* offline, VstInt32 count);
	VstInt32(__RV_THISCALL* offlineGetNumPasses)(AudioEffectX* __hidden this);
	VstInt32(__RV_THISCALL* offlineGetNumMetaPasses)(
		AudioEffectX* __hidden this);
	void(__RV_THISCALL* wantEvents)(
		AudioEffectX* __hidden this, VstInt32 filter);
	VstInt32(__RV_THISCALL* tempoAt)(AudioEffectX* __hidden this, VstInt32 pos);
	VstInt32(__RV_THISCALL* getNumAutomatableParameters)(
		AudioEffectX* __hidden this);
	VstInt32(__RV_THISCALL* getParameterQuantization)(
		AudioEffectX* __hidden this);
	VstInt32(__RV_THISCALL* getNumCategories)(AudioEffectX* __hidden this);
	bool(__RV_THISCALL* copyProgram)(
		AudioEffectX* __hidden this, VstInt32 destination);
	bool(__RV_THISCALL* needIdle)(AudioEffectX* __hidden this);
	AEffect*(__RV_THISCALL* getPreviousPlug)(
		AudioEffectX* __hidden this, VstInt32 input);
	AEffect*(__RV_THISCALL* getNextPlug)(
		AudioEffectX* __hidden this, VstInt32 output);
	void(__RV_THISCALL* inputConnected)(
		AudioEffectX* __hidden this, VstInt32 index, bool state);
	void(__RV_THISCALL* outputConnected)(
		AudioEffectX* __hidden this, VstInt32 index, bool state);
	VstInt32(__RV_THISCALL* willProcessReplacing)(AudioEffectX* __hidden this);
	void(__RV_THISCALL* wantAsyncOperation)(
		AudioEffectX* __hidden this, bool state);
	void(__RV_THISCALL* hasExternalBuffer)(
		AudioEffectX* __hidden this, bool state);
	VstInt32(__RV_THISCALL* reportCurrentPosition)(AudioEffectX* __hidden this);
	float*(__RV_THISCALL* reportDestinationBuffer)(AudioEffectX* __hidden this);
	void(__RV_THISCALL* setOutputSamplerate)(
		AudioEffectX* __hidden this, float samplerate);
	VstSpeakerArrangement*(__RV_THISCALL* getInputSpeakerArrangement)(
		AudioEffectX* __hidden this);
	VstSpeakerArrangement*(__RV_THISCALL* getOutputSpeakerArrangement)(
		AudioEffectX* __hidden this);
	void*(__RV_THISCALL* openWindow)(
		AudioEffectX* __hidden this, VstWindow* a1);
	bool(__RV_THISCALL* closeWindow)(
		AudioEffectX* __hidden this, VstWindow* a1);
	void(__RV_THISCALL* setBlockSizeAndSampleRate)(
		AudioEffectX* __hidden this, VstInt32 _blockSize, float _sampleRate);
	bool(__RV_THISCALL* getErrorText)(AudioEffectX* __hidden this, char* text);
	void*(__RV_THISCALL* getIcon)(AudioEffectX* __hidden this);
	bool(__RV_THISCALL* setViewPosition)(
		AudioEffectX* __hidden this, VstInt32 x, VstInt32 y);
	VstInt32(__RV_THISCALL* fxIdle)(AudioEffectX* __hidden this);
	bool(__RV_THISCALL* keysRequired)(AudioEffectX* __hidden this);
	bool(__RV_THISCALL* getChunkFile)(
		AudioEffectX* __hidden this, void* nativePath);
};

struct AudioEffectX {
	AudioEffectX_vtbl* __vftable;
	audioMasterCallback audioMaster;
	AEffEditor* editor;
	float sampleRate;
	VstInt32 blockSize;
	VstInt32 numPrograms;
	VstInt32 numParams;
	VstInt32 curProgram;
	AEffect cEffect;
};

struct AEffEditor_vtbl {
	void(__RV_THISCALL* __dtor)(AEffEditor* __hidden this);
	__RV_ITANIUM_ONLY void(__RV_THISCALL* __deleting_dtor)(
		AEffEditor* __hidden this);
	AudioEffect*(__RV_THISCALL* getEffect)(AEffEditor* __hidden this);
	bool(__RV_THISCALL* getRect)(AEffEditor* __hidden this, ERect** rect);
	bool(__RV_THISCALL* open)(AEffEditor* __hidden this, void* ptr);
	void(__RV_THISCALL* close)(AEffEditor* __hidden this);
	bool(__RV_THISCALL* isOpen)(AEffEditor* __hidden this);
	void(__RV_THISCALL* idle)(AEffEditor* __hidden this);
	__RV_MAC_ONLY void(__RV_THISCALL* draw)(
		AEffEditor* __hidden this, ERect* rect);
	__RV_MAC_ONLY VstInt32(__RV_THISCALL* mouse)(
		AEffEditor* __hidden this, VstInt32 x, VstInt32 y);
	__RV_MAC_ONLY VstInt32(__RV_THISCALL* key)(
		AEffEditor* __hidden this, VstInt32 keyCode);
	__RV_MAC_ONLY void(__RV_THISCALL* top)(AEffEditor* __hidden this);
	__RV_MAC_ONLY void(__RV_THISCALL* sleep)(AEffEditor* __hidden this);
	bool(__RV_THISCALL* onKeyDown)(
		AEffEditor* __hidden this, VstKeyCode* keyCode);
	bool(__RV_THISCALL* onKeyUp)(
		AEffEditor* __hidden this, VstKeyCode* keyCode);
	bool(__RV_THISCALL* onWheel)(AEffEditor* __hidden this, float distance);
	bool(__RV_THISCALL* setKnobMode)(AEffEditor* __hidden this, VstInt32 val);
};

struct AEffEditor {
	AEffEditor_vtbl* __vftable;
	AudioEffect* effect;
	void* systemWindow;
};
