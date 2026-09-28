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
