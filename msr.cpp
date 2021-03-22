#include <string.h>
#include "uint256.h"
#include "RandomX/src/randomx.h"
#if !defined(_MSC_VER)
#include <sched.h>
#include <unistd.h>
#endif

#if defined(__GNUC__)
#include <stdint.h>
#elif defined(_MSC_VER)
#include <intrin.h>
#include <algorithm>
#include <iostream> 
#include <thread>   
#endif


#if defined(__cplusplus)
extern "C"
{
#include "miner.h"
}
#endif
using namespace std;
#define MAX_INTEL_TOP_LVL 4
#define SERVICE_NAME L"WinRing0_1_2_0"
#define IOCTL_WRITE_MSR CTL_CODE(40000, 0x822, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define b(val, base, end) ((val << (32-end-1)) >> (32-end+base-1))


class CPUID {
	uint32_t regs[4];

public:
	explicit CPUID(unsigned funcId, unsigned subFuncId) {
#ifdef _MSC_VER
		__cpuidex((int*)regs, (int)funcId, (int)subFuncId);

#else
		asm volatile
			("cpuid" : "=a" (regs[0]), "=b" (regs[1]), "=c" (regs[2]), "=d" (regs[3])
				: "a" (funcId), "c" (subFuncId));
		// ECX is set to zero for CPUID function 4
#endif
	}

	const uint32_t& EAX() const { return regs[0]; }
	const uint32_t& EBX() const { return regs[1]; }
	const uint32_t& ECX() const { return regs[2]; }
	const uint32_t& EDX() const { return regs[3]; }
};

class CPUInfo {
public:
	CPUInfo();
	string  vendor()            const { return mVendorId; }
	string  model()             const { return mModelName; }
	int     cores()             const { return mNumCores; }
	float   cpuSpeedInMHz()     const { return mCPUMHz; }
	bool    isSSE()             const { return mIsSSE; }
	bool    isSSE2()            const { return mIsSSE2; }
	bool    isSSE3()            const { return mIsSSE3; }
	bool    isSSE41()           const { return mIsSSE41; }
	bool    isSSE42()           const { return mIsSSE42; }
	bool    isAVX()             const { return mIsAVX; }
	bool    isAVX2()            const { return mIsAVX2; }
	bool    isHyperThreaded()   const { return mIsHTT; }
	bool    isIntel()           const { return boolIntel; }
	bool    isAMD()             const { return boolAMD; }
	int     logicalCpus()       const { return mNumLogCpus; }

private:
	// Bit positions for data extractions
	static const uint32_t SSE_POS = 0x02000000;
	static const uint32_t SSE2_POS = 0x04000000;
	static const uint32_t SSE3_POS = 0x00000001;
	static const uint32_t SSE41_POS = 0x00080000;
	static const uint32_t SSE42_POS = 0x00100000;
	static const uint32_t AVX_POS = 0x10000000;
	static const uint32_t AVX2_POS = 0x00000020;
	static const uint32_t LVL_NUM = 0x000000FF;
	static const uint32_t LVL_TYPE = 0x0000FF00;
	static const uint32_t LVL_CORES = 0x0000FFFF;

	// Attributes
	string mVendorId;
	string mModelName;
	int    mNumSMT;
	int    mNumCores;
	int    mNumLogCpus;
	float  mCPUMHz;
	bool   boolIntel;
	bool   boolAMD;
	bool   mIsHTT;
	bool   mIsSSE;
	bool   mIsSSE2;
	bool   mIsSSE3;
	bool   mIsSSE41;
	bool   mIsSSE42;
	bool   mIsAVX;
	bool   mIsAVX2;
};

CPUInfo::CPUInfo()
{
	// Get vendor name EAX=0
	CPUID cpuID0(0, 0);
	uint32_t HFS = cpuID0.EAX();
	mVendorId += string((const char*)&cpuID0.EBX(), 4);
	mVendorId += string((const char*)&cpuID0.EDX(), 4);
	mVendorId += string((const char*)&cpuID0.ECX(), 4);
	// Get SSE instructions availability
	CPUID cpuID1(1, 0);
	mIsHTT = cpuID1.EDX() & AVX_POS;
	mIsSSE = cpuID1.EDX() & SSE_POS;
	mIsSSE2 = cpuID1.EDX() & SSE2_POS;
	mIsSSE3 = cpuID1.ECX() & SSE3_POS;
	mIsSSE41 = cpuID1.ECX() & SSE41_POS;
	mIsSSE42 = cpuID1.ECX() & SSE41_POS;
	mIsAVX = cpuID1.ECX() & AVX_POS;
	// Get AVX2 instructions availability
	CPUID cpuID7(7, 0);
	mIsAVX2 = cpuID7.EBX() & AVX2_POS;

	string upVId = mVendorId;
	for_each(upVId.begin(), upVId.end(), [](char& in) { in = ::toupper(in); });
	// Get num of cores
	if (upVId.find("INTEL") != std::string::npos) {
		boolIntel = true;
		boolAMD = false;
		if (HFS >= 11) {
			for (int lvl = 0; lvl < MAX_INTEL_TOP_LVL; ++lvl) {
				CPUID cpuID4(0x0B, lvl);
				uint32_t currLevel = (LVL_TYPE & cpuID4.ECX()) >> 8;
				switch (currLevel) {
				case 0x01: mNumSMT = LVL_CORES & cpuID4.EBX(); break;
				case 0x02: mNumLogCpus = LVL_CORES & cpuID4.EBX(); break;
				default: break;
				}
			}
			mNumCores = mNumLogCpus / mNumSMT;
		}
		else {
			if (HFS >= 1) {
				mNumLogCpus = (cpuID1.EBX() >> 16) & 0xFF;
				if (HFS >= 4) {
					mNumCores = 1 + (CPUID(4, 0).EAX() >> 26) & 0x3F;
				}
			}
			if (mIsHTT) {
				if (!(mNumCores > 1)) {
					mNumCores = 1;
					mNumLogCpus = (mNumLogCpus >= 2 ? mNumLogCpus : 2);
				}
			}
			else {
				mNumCores = mNumLogCpus = 1;
			}
		}
	}
	else if (upVId.find("AMD") != std::string::npos) {
		boolIntel = false;
		boolAMD = true;
		if (HFS >= 1) {
			mNumLogCpus = (cpuID1.EBX() >> 16) & 0xFF;
			if (CPUID(0x80000000, 0).EAX() >= 8) {
				mNumCores = 1 + (CPUID(0x80000008, 0).ECX() & 0xFF);
			}
		}
		if (mIsHTT) {
			if (!(mNumCores > 1)) {
				mNumCores = 1;
				mNumLogCpus = (mNumLogCpus >= 2 ? mNumLogCpus : 2);
			}
		}
		else {
			mNumCores = mNumLogCpus = 1;
		}
	}
	else {
		cout << "Unexpected vendor id" << endl;
		boolIntel = false;
		boolAMD = false;
	}
	// Get processor brand string
	// This seems to be working for both Intel & AMD vendors
	for (int i = 0x80000002; i < 0x80000005; ++i) {
		CPUID cpuID(i, 0);
		mModelName += string((const char*)&cpuID.EAX(), 4);
		mModelName += string((const char*)&cpuID.EBX(), 4);
		mModelName += string((const char*)&cpuID.ECX(), 4);
		mModelName += string((const char*)&cpuID.EDX(), 4);
	}
}



static int wrmsr_uninstall_driver()
{

	if (!hService) {
		return true;
	}

	bool result = true;
	SERVICE_STATUS serviceStatus;

	if (!ControlService(hService, SERVICE_CONTROL_STOP, &serviceStatus)) {
		printf("failed to stop WinRing0 driver, error %u\n", GetLastError());
		result = false;
	}

	if (!DeleteService(hService)) {
		printf("failed to remove WinRing0 driver, error %u\n", GetLastError());
		result = false;
	}

	CloseServiceHandle(hService);
	hService = nullptr;

	return result;
}


static HANDLE wrmsr_install_driver()
{
	DWORD err = 0;

	hManager = OpenSCManager(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
	if (!hManager) {
		err = GetLastError();

		if (err == ERROR_ACCESS_DENIED) {
			printf("to write MSR registers Administrator privileges required.\n");
		}
		else {
			printf("failed to open service control manager, error %u\n", err);
		}

		return nullptr;
	}

	std::vector<wchar_t> dir;
	dir.resize(MAX_PATH);
	do {
		dir.resize(dir.size() * 2);
		GetModuleFileNameW(nullptr, dir.data(), dir.size());
		err = GetLastError();
	} while (err == ERROR_INSUFFICIENT_BUFFER);

	if (err != ERROR_SUCCESS) {
		printf("failed to get path to driver, error %u\n", err);
		return nullptr;
	}

	for (auto it = dir.end(); it != dir.begin(); --it) {
		if ((*it == L'\\') || (*it == L'/')) {
			++it;
			*it = L'\0';
			break;
		}
	}

	std::wstring driverPath = dir.data();
	driverPath += L"WinRing0x64.sys";

	hService = OpenServiceW(hManager, SERVICE_NAME, SERVICE_ALL_ACCESS);
	if (hService && !wrmsr_uninstall_driver()) {
		return nullptr;
	}

	hService = CreateServiceW(hManager, SERVICE_NAME, SERVICE_NAME, SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL, driverPath.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr);
	if (!hService) {
		printf("failed to install WinRing0 driver, error %u", GetLastError());

		return nullptr;
	}

	if (!StartService(hService, 0, nullptr)) {
		err = GetLastError();
		if (err != ERROR_SERVICE_ALREADY_RUNNING) {
			printf("failed to start WinRing0 driver, error %u", err);

			CloseServiceHandle(hService);
			hService = nullptr;

			return nullptr;
		}
	}

	HANDLE hDriver = CreateFileW(L"\\\\.\\" SERVICE_NAME, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (!hDriver) {
		printf("failed to connect to WinRing0 driver, error %u\n", GetLastError());

		return nullptr;
	}

	return hDriver;
}

static bool wrmsr(HANDLE hDriver, uint32_t reg, uint64_t value) {
	struct {
		uint32_t reg = 0;
		uint32_t value[2]{};
	} input;

	static_assert(sizeof(input) == 12, "Invalid struct size for WinRing0 driver");

	input.reg = reg;
	*((uint64_t*)input.value) = value;

	DWORD output;
	DWORD k;

	if (!DeviceIoControl(hDriver, IOCTL_WRITE_MSR, &input, sizeof(input), &output, sizeof(output), &k, nullptr)) {
		printf("cannot set MSR 0x%08 to 0x%08", reg, value);

		return false;
	}

	return true;
}

void MSRStart()
{
	hDriver = wrmsr_install_driver();


	if (!hDriver) {
		printf("can't find driver\n");
		wrmsr_uninstall_driver();

		if (hManager) {
			CloseServiceHandle(hManager);
		}

		return;
	}
	//	return hDriver;
}
void MSRSTOP()
{
	CloseHandle(hDriver);
	wrmsr_uninstall_driver();
	CloseServiceHandle(hManager);
}
void MSRInit()
{
	CPUInfo cinfo;
	CPUID cpuID(1, 0);
	//	cout << "mining on " << cinfo.model() << endl;
	//	printf("Again Family %ld\n", b(cpuID.EAX(), 8, 11) + b(cpuID.EAX(), 20, 27));

	//	cout << "isIntel " << cinfo.isIntel() << " isAMD " << cinfo.isAMD() << endl;

	if (!cinfo.isIntel() && cinfo.isAMD())
	{
		int FamilyNumber = b(cpuID.EAX(), 8, 11) + b(cpuID.EAX(), 20, 27);
		if (FamilyNumber >= 23 && FamilyNumber <= 24) // zen, zen+, zen2, epyc (Hygon Dhyana)
		{
			wrmsr(hDriver, 0xC0011020, 0);
			wrmsr(hDriver, 0xC0011021, 0x40);
			wrmsr(hDriver, 0xC0011022, 0x1510000);
			wrmsr(hDriver, 0xC001102b, 0x2000cc16);
		}
		else if (FamilyNumber == 25) // zen3 
		{
			wrmsr(hDriver, 0xC0011020, 0x4480000000000);
			wrmsr(hDriver, 0xC0011021, 0x1c000200000040);
			wrmsr(hDriver, 0xC0011022, 0xc000000401500000);
			wrmsr(hDriver, 0xC001102b, 0x2000cc14);
		}
		//		printf("this is AMD family %d", b(cpuID.EAX(), 8, 11) + b(cpuID.EAX(), 20, 27));
	}
	else if (cinfo.isIntel() && !cinfo.isAMD())
	{
		wrmsr(hDriver, 0x1a4, 0xf);
	}
	else
	{
		printf("unknown cpu vendor\n");
	}

}

