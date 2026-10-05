class FUnknown {
	virtual int queryInterface(const char iid[16], void** obj);
	virtual unsigned int addRef();
	virtual unsigned int release();
};

class IPluginFactory : public FUnknown {
	virtual int getFactoryInfo(void* info);
	virtual int countClasses();
	virtual int getClassInfo(int index, void* info);
	virtual int createInstance(
		const char cid[16], const char iid[16], void** obj);
};

struct PFactoryInfo {
	char vendor[64];
	char url[256];
	char email[128];
	int32_t flags;
};

struct PClassInfo {
	char cid[16];
	int32_t cardinality;
	char category[32];
	char name[64];
};

struct PClassInfo2 {
	char cid[16];
	int32_t cardinality;
	char category[32];
	char name[64];
	uint32_t classFlags;
	char subCategories[128];
	char vendor[64];
	char version[64];
	char sdkVersion[64];
};