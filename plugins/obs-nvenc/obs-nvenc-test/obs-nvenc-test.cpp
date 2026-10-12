#include <string_view>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <future>
#include <cstring>
#include <cstdio>
#include <cstdarg>

/* Keep diagnostics in stdout as INI comments so the parent can retain them,
 * including partial output if the probe times out or crashes. */
static void probe_log(const char *format, ...)
{
	char message[1024];
	va_list args;
	va_start(args, format);
	vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	for (char *p = message; *p; p++) {
		if (*p == '\n' || *p == '\r')
			*p = ' ';
	}
	printf("# %s\n", message);
}

#define FFNV_LOG_FUNC(logctx, ...) probe_log(__VA_ARGS__)
#define FFNV_DEBUG_LOG_FUNC(logctx, ...)

#include <ffnvcodec/nvEncodeAPI.h>
#include <ffnvcodec/dynlink_loader.h>

/*
 * Utility to check for NVENC support and capabilities.
 * Will check all GPUs and return INI-formatted results based on highest capability of all devices.
 */

using namespace std;
using namespace std::chrono_literals;

static CudaFunctions *cu = nullptr;
static NvencFunctions *nvenc = nullptr;

NV_ENCODE_API_FUNCTION_LIST nv = {NV_ENCODE_API_FUNCTION_LIST_VER};
static constexpr uint32_t NVENC_CONFIGURED_VERSION = (NVENCAPI_MAJOR_VERSION << 4) | NVENCAPI_MINOR_VERSION;

static bool check_cuda(CUresult result, const char *call, int device = -1)
{
	if (result == CUDA_SUCCESS)
		return true;
	const char *name = nullptr;
	if (cu && cu->cuGetErrorName)
		cu->cuGetErrorName(result, &name);
	probe_log("CUDA device=%d call=%s status=%d (%s)", device, call, (int)result, name ? name : "unknown");
	return false;
}

static bool check_nvenc(NVENCSTATUS result, const char *call, int device = -1)
{
	if (result == NV_ENC_SUCCESS)
		return true;
	probe_log("NVENC device=%d call=%s status=%d", device, call, (int)result);
	return false;
}

/* NVML stuff */
#define NVML_SUCCESS 0
#define NVML_DEVICE_UUID_V2_BUFFER_SIZE 96
#define NVML_DEVICE_NAME_V2_BUFFER_SIZE 96
#define NVML_SYSTEM_DRIVER_VERSION_BUFFER_SIZE 80

typedef int nvmlReturn_t;
typedef struct nvmlDevice *nvmlDevice_t;

typedef enum nvmlEncoderType {
	NVML_ENCODER_QUERY_H264,
	NVML_ENCODER_QUERY_HEVC,
	NVML_ENCODER_QUERY_AV1,
	NVML_ENCODER_QUERY_UNKNOWN
} nvmlEncoderType_t;

typedef nvmlReturn_t (*NVML_GET_DRIVER_VER_FUNC)(char *, unsigned int);
typedef nvmlReturn_t (*NVML_INIT_V2)();
typedef nvmlReturn_t (*NVML_SHUTDOWN)();
typedef nvmlReturn_t (*NVML_GET_HANDLE_BY_BUS_ID)(const char *, nvmlDevice_t *);
typedef nvmlReturn_t (*NVML_GET_DEVICE_UUID)(nvmlDevice_t, char *, unsigned);
typedef nvmlReturn_t (*NVML_GET_DEVICE_NAME)(nvmlDevice_t, char *, unsigned);
typedef nvmlReturn_t (*NVML_GET_DEVICE_PCIE_GEN)(nvmlDevice_t, unsigned *);
typedef nvmlReturn_t (*NVML_GET_DEVICE_PCIE_WIDTH)(nvmlDevice_t, unsigned *);
typedef nvmlReturn_t (*NVML_GET_DEVICE_NAME)(nvmlDevice_t, char *, unsigned);
typedef nvmlReturn_t (*NVML_GET_DEVICE_ARCHITECTURE)(nvmlDevice_t, unsigned *);
typedef nvmlReturn_t (*NVML_GET_ENCODER_SESSIONS)(nvmlDevice_t, unsigned *, void *);
typedef nvmlReturn_t (*NVML_GET_ENCODER_CAPACITY)(nvmlDevice_t, nvmlEncoderType, unsigned *);
typedef nvmlReturn_t (*NVML_GET_ENCODER_UTILISATION)(nvmlDevice_t, unsigned *, unsigned *);

/* Only Kepler is defined in NVIDIA's documentation, but it's also the main one we care about. */
constexpr uint32_t NVML_DEVICE_ARCH_KEPLER = 2;

const unordered_map<uint32_t, const string_view> arch_to_name = {
	{NVML_DEVICE_ARCH_KEPLER, "Kepler"},
	{3, "Kepler"},
	{4, "Maxwell"},
	{5, "Volta"},
	{6, "Turing"},
	{7, "Ampere"},
	{8, "Ada"},
	{9, "Hopper"},
	{10, "Blackwell"},
};

/* List of capabilities to be queried per codec */
static const vector<pair<NV_ENC_CAPS, string>> capabilities = {
	{NV_ENC_CAPS_NUM_MAX_BFRAMES, "bframes"},
	{NV_ENC_CAPS_SUPPORT_LOSSLESS_ENCODE, "lossless"},
	{NV_ENC_CAPS_SUPPORT_LOOKAHEAD, "lookahead"},
	{NV_ENC_CAPS_SUPPORT_TEMPORAL_AQ, "temporal_aq"},
	{NV_ENC_CAPS_SUPPORT_DYN_BITRATE_CHANGE, "dynamic_bitrate"},
	{NV_ENC_CAPS_SUPPORT_10BIT_ENCODE, "10bit"},
	{NV_ENC_CAPS_SUPPORT_BFRAME_REF_MODE, "bref"},
	{NV_ENC_CAPS_NUM_ENCODER_ENGINES, "engines"},
	{NV_ENC_CAPS_SUPPORT_YUV444_ENCODE, "yuv_444"},
	{NV_ENC_CAPS_WIDTH_MAX, "max_width"},
	{NV_ENC_CAPS_HEIGHT_MAX, "max_height"},
#if NVENCAPI_MAJOR_VERSION > 12 || NVENCAPI_MINOR_VERSION >= 2
	/* SDK 12.2+ features */
	{NV_ENC_CAPS_SUPPORT_TEMPORAL_FILTER, "temporal_filter"},
	{NV_ENC_CAPS_SUPPORT_LOOKAHEAD_LEVEL, "lookahead_level"},
	{NV_ENC_CAPS_SUPPORT_UNIDIRECTIONAL_B, "unidirectional_b"},
#endif
#if NVENCAPI_MAJOR_VERSION >= 13
	/* SDK 13.0+ features */
	{NV_ENC_CAPS_SUPPORT_YUV422_ENCODE, "yuv_422"},
#endif
};

static const vector<pair<string_view, GUID>> codecs = {{"h264", NV_ENC_CODEC_H264_GUID},
						       {"hevc", NV_ENC_CODEC_HEVC_GUID},
						       {"av1", NV_ENC_CODEC_AV1_GUID}};

typedef unordered_map<string, unordered_map<string, int>> codec_caps_map;

struct device_info {
	string pci_id;
	string nvml_uuid;
	string cuda_uuid;
	string name;

	uint32_t architecture;
	uint32_t pcie_gen;
	uint32_t pcie_width;

	uint32_t encoder_sessions;
	uint32_t utilisation;
	uint32_t sample_period;
	uint32_t capacity_h264;
	uint32_t capacity_hevc;
	uint32_t capacity_av1;

	/* A failed query must be distinguishable from a reported zero. Even a
	 * successful zero session count can be incomplete on some Windows drivers. */
	nvmlReturn_t nvml_device_status = -1;
	nvmlReturn_t encoder_sessions_status = -1;
	nvmlReturn_t utilisation_status = -1;

	codec_caps_map caps;
};

/* RAII wrappers to make my life a little easier. */
struct NVML {
	NVML_INIT_V2 init;
	NVML_SHUTDOWN shutdown;
	NVML_GET_DRIVER_VER_FUNC getDriverVersion;
	NVML_GET_HANDLE_BY_BUS_ID getDeviceHandleByPCIBusId;
	NVML_GET_DEVICE_UUID getDeviceUUID;
	NVML_GET_DEVICE_NAME getDeviceName;
	NVML_GET_DEVICE_PCIE_GEN getDevicePCIeGen;
	NVML_GET_DEVICE_PCIE_WIDTH getDevicePCIeWidth;
	NVML_GET_DEVICE_ARCHITECTURE getDeviceArchitecture;
	NVML_GET_ENCODER_SESSIONS getEncoderSessions;
	NVML_GET_ENCODER_CAPACITY getEncoderCapacity;
	NVML_GET_ENCODER_UTILISATION getEncoderUtilisation;

	NVML() = default;

	~NVML()
	{
		if (initialised && shutdown)
			shutdown();
	}

	bool Init()
	{
		probe_log("stage=NVML initialization");
		if (!load_nvml_lib()) {
			printf("reason=nvml_lib\n");
			return false;
		}

		init = (NVML_INIT_V2)load_nvml_func("nvmlInit_v2");
		shutdown = (NVML_SHUTDOWN)load_nvml_func("nvmlShutdown");
		getDriverVersion = (NVML_GET_DRIVER_VER_FUNC)load_nvml_func("nvmlSystemGetDriverVersion");
		getDeviceHandleByPCIBusId =
			(NVML_GET_HANDLE_BY_BUS_ID)load_nvml_func("nvmlDeviceGetHandleByPciBusId_v2");
		getDeviceUUID = (NVML_GET_DEVICE_UUID)load_nvml_func("nvmlDeviceGetUUID");
		getDeviceName = (NVML_GET_DEVICE_NAME)load_nvml_func("nvmlDeviceGetName");
		getDevicePCIeGen = (NVML_GET_DEVICE_PCIE_GEN)load_nvml_func("nvmlDeviceGetCurrPcieLinkGeneration");
		getDevicePCIeWidth = (NVML_GET_DEVICE_PCIE_WIDTH)load_nvml_func("nvmlDeviceGetCurrPcieLinkWidth");
		getDeviceArchitecture = (NVML_GET_DEVICE_ARCHITECTURE)load_nvml_func("nvmlDeviceGetArchitecture");
		getEncoderSessions = (NVML_GET_ENCODER_SESSIONS)load_nvml_func("nvmlDeviceGetEncoderSessions");
		getEncoderCapacity = (NVML_GET_ENCODER_CAPACITY)load_nvml_func("nvmlDeviceGetEncoderCapacity");
		getEncoderUtilisation = (NVML_GET_ENCODER_UTILISATION)load_nvml_func("nvmlDeviceGetEncoderUtilization");

		if (!init || !shutdown || !getDriverVersion || !getDeviceHandleByPCIBusId || !getDeviceUUID ||
		    !getDeviceName || !getDevicePCIeGen || !getDevicePCIeWidth || !getEncoderSessions ||
		    !getEncoderCapacity || !getEncoderUtilisation || !getDeviceArchitecture) {
			printf("reason=nvml_symbol\n");
			return false;
		}

		nvmlReturn_t res = init();
		if (res != 0) {
			printf("reason=nvml_init_%d\n", res);
			return false;
		}

		initialised = true;
		return true;
	}

private:
	bool initialised = false;
	static inline void *nvml_lib = nullptr;

	bool load_nvml_lib()
	{
#ifdef _WIN32
		nvml_lib = LoadLibraryA("nvml.dll");
		if (!nvml_lib)
			probe_log("LoadLibraryA(nvml.dll) failed: win32_error=%lu", GetLastError());
#else
		nvml_lib = dlopen("libnvidia-ml.so.1", RTLD_LAZY);
		if (!nvml_lib)
			probe_log("dlopen(libnvidia-ml.so.1) failed: %s", dlerror());
#endif
		return nvml_lib != nullptr;
	}

	static void *load_nvml_func(const char *func)
	{
#ifdef _WIN32
		void *func_ptr = (void *)GetProcAddress((HMODULE)nvml_lib, func);
#else
		void *func_ptr = dlsym(nvml_lib, func);
#endif
		if (!func_ptr)
			probe_log("NVML missing function: %s", func);
		return func_ptr;
	}
};

struct CUDACtx {
	CUcontext ctx = nullptr;

	CUDACtx() = default;

	~CUDACtx()
	{
		if (ctx)
			check_cuda(cu->cuCtxDestroy(ctx), "cuCtxDestroy");
	}

	bool Init(int adapter_idx)
	{
		CUdevice dev;
		probe_log("device=%d stage=cuDeviceGet", adapter_idx);
		if (!check_cuda(cu->cuDeviceGet(&dev, adapter_idx), "cuDeviceGet", adapter_idx))
			return false;

		probe_log("device=%d stage=cuCtxCreate", adapter_idx);
		return check_cuda(cu->cuCtxCreate(&ctx, 0, dev), "cuCtxCreate", adapter_idx);
	}

	string GetPCIBusId()
	{
		CUdevice dev;
		string bus_id;
		bus_id.resize(16);

		if (!check_cuda(cu->cuCtxGetDevice(&dev), "cuCtxGetDevice") ||
		    !check_cuda(cu->cuDeviceGetPCIBusId(bus_id.data(), (int)bus_id.size(), dev), "cuDeviceGetPCIBusId"))
			return {};
		return bus_id;
	}

	string GetUUID()
	{
		CUdevice dev;
		CUuuid uuid;
		string uuid_str;

		if (!check_cuda(cu->cuCtxGetDevice(&dev), "cuCtxGetDevice") ||
		    !check_cuda(cu->cuDeviceGetUuid_v2(&uuid, dev), "cuDeviceGetUuid_v2"))
			return {};

		uuid_str.resize(32);
		for (size_t idx = 0; idx < 16; idx++) {
			sprintf(uuid_str.data() + idx * 2, "%02x", uuid.bytes[idx] & 0xFF);
		}

		return uuid_str;
	}
};

struct NVSession {
	void *ptr = nullptr;

	NVSession() = default;

	~NVSession()
	{
		if (ptr)
			check_nvenc(nv.nvEncDestroyEncoder(ptr), "nvEncDestroyEncoder");
	}

	NVENCSTATUS OpenSession(const CUDACtx &ctx)
	{
		NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS params = {};
		params.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
		params.apiVersion = NVENCAPI_VERSION;
		params.device = ctx.ctx;
		params.deviceType = NV_ENC_DEVICE_TYPE_CUDA;

		return nv.nvEncOpenEncodeSessionEx(&params, &ptr);
	}
};

static bool init_nvenc()
{
	probe_log("stage=NVENC initialization");
	if (nvenc_load_functions(&nvenc, nullptr)) {
		printf("reason=nvenc_lib\n");
		return false;
	}

	NVENCSTATUS res = nvenc->NvEncodeAPICreateInstance(&nv);
	if (!check_nvenc(res, "NvEncodeAPICreateInstance")) {
		printf("reason=nvenc_init_%d\n", res);
		return false;
	}

	return true;
}

static bool init_cuda()
{
	probe_log("stage=CUDA initialization");
	if (cuda_load_functions(&cu, nullptr)) {
		printf("reason=cuda_lib\n");
		return false;
	}

	CUresult res = cu->cuInit(0);
	if (!check_cuda(res, "cuInit")) {
		printf("reason=cuda_init_%d\n", res);
		return false;
	}

	return true;
}

static bool get_adapter_caps(int adapter_idx, codec_caps_map &caps, device_info &device_info, NVML &nvml,
			     bool &session_limit)
{
	CUDACtx cudaCtx;
	NVSession nvSession;

	if (!cudaCtx.Init(adapter_idx))
		return false;

	device_info.pci_id = cudaCtx.GetPCIBusId();
	device_info.cuda_uuid = cudaCtx.GetUUID();

	nvmlDevice_t dev;
	probe_log("device=%d stage=NVML device queries", adapter_idx);
	device_info.nvml_device_status = nvml.getDeviceHandleByPCIBusId(device_info.pci_id.data(), &dev);
	if (device_info.nvml_device_status == NVML_SUCCESS) {
		char uuid[NVML_DEVICE_UUID_V2_BUFFER_SIZE] = {};
		if (nvml.getDeviceUUID(dev, uuid, sizeof(uuid)) == NVML_SUCCESS)
			device_info.nvml_uuid = uuid;

		char name[NVML_DEVICE_NAME_V2_BUFFER_SIZE] = {};
		if (nvml.getDeviceName(dev, name, sizeof(name)) == NVML_SUCCESS)
			device_info.name = name;

		nvml.getDevicePCIeGen(dev, &device_info.pcie_gen);
		nvml.getDevicePCIeWidth(dev, &device_info.pcie_width);
		device_info.encoder_sessions_status =
			nvml.getEncoderSessions(dev, &device_info.encoder_sessions, nullptr);
		nvml.getDeviceArchitecture(dev, &device_info.architecture);
		device_info.utilisation_status =
			nvml.getEncoderUtilisation(dev, &device_info.utilisation, &device_info.sample_period);
		nvml.getEncoderCapacity(dev, NVML_ENCODER_QUERY_H264, &device_info.capacity_h264);
		nvml.getEncoderCapacity(dev, NVML_ENCODER_QUERY_HEVC, &device_info.capacity_hevc);
		nvml.getEncoderCapacity(dev, NVML_ENCODER_QUERY_AV1, &device_info.capacity_av1);
	}
	probe_log("device=%d NVML handle_status=%d sessions_status=%d sessions=%u utilisation_status=%d utilisation=%u",
		  adapter_idx, device_info.nvml_device_status, device_info.encoder_sessions_status,
		  device_info.encoder_sessions, device_info.utilisation_status, device_info.utilisation);

	probe_log("device=%d stage=nvEncOpenEncodeSessionEx", adapter_idx);
	auto res = nvSession.OpenSession(cudaCtx);
	session_limit = session_limit || res == NV_ENC_ERR_INCOMPATIBLE_CLIENT_KEY;
	if (!check_nvenc(res, "nvEncOpenEncodeSessionEx", adapter_idx))
		return false;

	uint32_t guid_count = 0;
	probe_log("device=%d stage=nvEncGetEncodeGUIDCount", adapter_idx);
	if (!check_nvenc(nv.nvEncGetEncodeGUIDCount(nvSession.ptr, &guid_count), "nvEncGetEncodeGUIDCount",
			 adapter_idx))
		return false;

	vector<GUID> guids;
	guids.resize(guid_count);
	probe_log("device=%d stage=nvEncGetEncodeGUIDs count=%u", adapter_idx, guid_count);
	NVENCSTATUS stat = nv.nvEncGetEncodeGUIDs(nvSession.ptr, guids.data(), guid_count, &guid_count);
	if (!check_nvenc(stat, "nvEncGetEncodeGUIDs", adapter_idx))
		return false;

	NV_ENC_CAPS_PARAM param = {NV_ENC_CAPS_PARAM_VER};
	probe_log("device=%d stage=codec capability queries", adapter_idx);

	for (uint32_t i = 0; i < guid_count; i++) {
		GUID *guid = &guids[i];

		std::string codec_name = "unknown";
		for (const auto &[name, codec_guid] : codecs) {
			if (memcmp(&codec_guid, guid, sizeof(GUID)) == 0) {
				codec_name = name;
				break;
			}
		}

		caps[codec_name]["codec_supported"] = 1;
		device_info.caps[codec_name]["codec_supported"] = 1;

		for (const auto &[cap, name] : capabilities) {
			int v;
			param.capsToQuery = cap;
			if (nv.nvEncGetEncodeCaps(nvSession.ptr, *guid, &param, &v) != NV_ENC_SUCCESS)
				continue;

			device_info.caps[codec_name][name] = v;
			caps[codec_name][name] = std::max(v, caps[codec_name][name]);
		}

#if NVENCAPI_MAJOR_VERSION > 12 || NVENCAPI_MINOR_VERSION >= 2
		/* Explicitly check if UHQ tuning is supported since temporal filtering query is true for all codecs. */
		NV_ENC_PRESET_CONFIG preset_config = {};
		preset_config.version = NV_ENC_PRESET_CONFIG_VER;
		preset_config.presetCfg.version = NV_ENC_CONFIG_VER;

		NVENCSTATUS res = nv.nvEncGetEncodePresetConfigEx(nvSession.ptr, *guid, NV_ENC_PRESET_P7_GUID,
								  NV_ENC_TUNING_INFO_ULTRA_HIGH_QUALITY,
								  &preset_config);

		device_info.caps[codec_name]["uhq"] = res == NV_ENC_SUCCESS ? 1 : 0;
		caps[codec_name]["uhq"] = std::max(device_info.caps[codec_name]["uhq"], caps[codec_name]["uhq"]);
#endif
	}

	return true;
}

bool nvenc_checks(codec_caps_map &caps, vector<device_info> &device_infos)
{
	/* NVENC API init */
	if (!init_nvenc())
		return false;

	/* CUDA init */
	if (!init_cuda())
		return false;

	NVML nvml;
	if (!nvml.Init())
		return false;

	/* --------------------------------------------------------- */
	/* obtain adapter compatibility information                  */

	uint32_t nvenc_ver;
	int cuda_driver_ver;
	int cuda_devices = 0;
	int nvenc_devices = 0;
	char driver_ver[NVML_SYSTEM_DRIVER_VERSION_BUFFER_SIZE];
	bool session_limit = false;

	/* NVIDIA driver version */
	probe_log("stage=driver and device enumeration");
	nvmlReturn_t driver_status = nvml.getDriverVersion(driver_ver, sizeof(driver_ver));
	printf("driver_ver_status=%d\n", driver_status);
	if (driver_status == NVML_SUCCESS) {
		printf("driver_ver=%s\n", driver_ver);
	} else {
		// Treat this as a non-fatal failure
		printf("driver_ver=0.0\n");
	}

	/* CUDA driver version and devices */
	if (check_cuda(cu->cuDriverGetVersion(&cuda_driver_ver), "cuDriverGetVersion")) {
		printf("cuda_ver=%d.%d\n", cuda_driver_ver / 1000, cuda_driver_ver % 1000);
	} else {
		printf("reason=no_cuda_version\n");
		return false;
	}

	if (check_cuda(cu->cuDeviceGetCount(&cuda_devices), "cuDeviceGetCount") && cuda_devices) {
		printf("cuda_devices=%d\n", cuda_devices);
	} else {
		printf("reason=no_devices\n");
		return false;
	}

	/* NVENC API version */
	if (check_nvenc(nvenc->NvEncodeAPIGetMaxSupportedVersion(&nvenc_ver), "NvEncodeAPIGetMaxSupportedVersion")) {
		printf("nvenc_ver=%d.%d\n", nvenc_ver >> 4, nvenc_ver & 0xf);
	} else {
		printf("reason=no_nvenc_version\n");
		return false;
	}

	device_infos.resize(cuda_devices);
	for (int idx = 0; idx < cuda_devices; idx++) {
		if (get_adapter_caps(idx, caps, device_infos[idx], nvml, session_limit))
			nvenc_devices++;
	}

	if (session_limit) {
		printf("reason=session_limit\n");
		return false;
	}

	if (nvenc_ver < NVENC_CONFIGURED_VERSION) {
		printf("reason=outdated_driver\n");
		return false;
	}

	printf("nvenc_devices=%d\n", nvenc_devices);
	if (!nvenc_devices) {
		printf("reason=no_supported_devices\n");
		return false;
	}

	uint32_t latest_architecture = 0;
	string_view architecture = "Unknown";

	for (auto &info : device_infos)
		latest_architecture = std::max(info.architecture, latest_architecture);

	if (arch_to_name.count(latest_architecture))
		architecture = arch_to_name.at(latest_architecture);

	printf("latest_architecture=%u\n"
	       "latest_architecture_name=%s\n",
	       latest_architecture, architecture.data());

	return true;
}

int check_thread()
{
	int ret = 0;
	codec_caps_map caps;
	vector<device_info> device_infos;

	caps["h264"]["codec_supported"] = 0;
	caps["hevc"]["codec_supported"] = 0;
	caps["av1"]["codec_supported"] = 0;

	printf("[general]\ncompiled_nvenc_ver=%d.%d\n", NVENCAPI_MAJOR_VERSION, NVENCAPI_MINOR_VERSION);

	if (nvenc_checks(caps, device_infos)) {
		printf("nvenc_supported=true\n");
	} else {
		printf("nvenc_supported=false\n");
		ret = 1;
	}

	/* Global capabilities, based on highest supported across all devices */
	for (const auto &[codec, codec_caps] : caps) {
		printf("\n[%s]\n", codec.c_str());

		for (const auto &[name, value] : codec_caps) {
			printf("%s=%d\n", name.c_str(), value);
		}
	}

	/* Per-device info (mostly for debugging) */
	for (size_t idx = 0; idx < device_infos.size(); idx++) {
		const auto &info = device_infos[idx];
		string_view architecture = "Unknown";
		if (arch_to_name.count(info.architecture))
			architecture = arch_to_name.at(info.architecture);

		printf("\n[device.%zu]\n"
		       "pci_id=%s\n"
		       "nvml_uuid=%s\n"
		       "cuda_uuid=%s\n"
		       "name=%s\n"
		       "architecture=%u\n"
		       "architecture_name=%s\n"
		       "pcie_link_width=%d\n"
		       "pcie_link_gen=%d\n"
		       "encoder_sessions=%u\n"
		       "utilisation=%u\n"
		       "sample_period=%u\n"
		       "capacity_h264=%u\n"
		       "capacity_hevc=%u\n"
		       "capacity_av1=%u\n",
		       idx, info.pci_id.c_str(), info.nvml_uuid.c_str(), info.cuda_uuid.c_str(), info.name.c_str(),
		       info.architecture, architecture.data(), info.pcie_width, info.pcie_gen, info.encoder_sessions,
		       info.utilisation, info.sample_period, info.capacity_h264, info.capacity_hevc, info.capacity_av1);
		printf("nvml_device_status=%d\nencoder_sessions_status=%d\nutilisation_status=%d\n",
		       info.nvml_device_status, info.encoder_sessions_status, info.utilisation_status);

		for (const auto &[codec, codec_caps] : info.caps) {
			printf("\n[device.%zu.%s]\n", idx, codec.c_str());

			for (const auto &[name, value] : codec_caps) {
				printf("%s=%d\n", name.c_str(), value);
			}
		}
	}

	return ret;
}

int main(int, char **)
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	future<int> f = async(launch::async, check_thread);
	future_status status = f.wait_for(2.5s);

	if (status == future_status::timeout) {
		probe_log("timeout after 2500 ms; last stage above may not have completed");
		exit(2);
	}

	return f.get();
}
