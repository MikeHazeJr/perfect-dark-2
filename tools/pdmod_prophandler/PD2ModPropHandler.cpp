/**
 * PD2ModPropHandler.cpp -- Priority M / B-238 / M-2.1
 *
 * Windows Shell property handler for `.pdmod` archives. When Explorer (or
 * any IShellPropSheetExt consumer) asks for properties on a `.pdmod`, this
 * handler reads `mod.json` from the archive in memory and emits the
 * standard System.* property values per design Section 4.5.
 *
 * Architecture:
 *
 *   Explorer
 *      |
 *      | IInitializeWithStream::Initialize(IStream*)
 *      v
 *   PD2ModPropHandler
 *      |
 *      | (read entire IStream into a buffer)
 *      v
 *   pdmod_zip_inflate.cpp  (parse central dir + inflate mod.json)
 *      |
 *      v
 *   pdmod_json_min.cpp     (extract headline string fields)
 *      |
 *      v
 *   IPropertyStore::GetCount / GetAt / GetValue  (emit PROPVARIANT)
 *
 * The handler is read-only -- IPropertyStore::SetValue / Commit return
 * E_NOTIMPL. A future revision can add System.Mod.* custom properties
 * once the .propdesc schema infrastructure ships; Phase 1 is the
 * standard-property surface.
 *
 * CLSID: {6F4D7064-D2BB-4F38-8B95-7A2B9C4BD61F}  (PD2-mod random GUID)
 */

#define INITGUID
#include <windows.h>
#include <unknwn.h>
#include <objidl.h>
#include <propsys.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shlwapi.h>
#include <objbase.h>
#include <new>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pdmod_zip_inflate.h"
#include "pdmod_json_min.h"

/* ------------------------------------------------------------------ CLSID */

// {6F4D7064-D2BB-4F38-8B95-7A2B9C4BD61F}
DEFINE_GUID(CLSID_PD2ModPropHandler,
	0x6F4D7064, 0xD2BB, 0x4F38, 0x8B, 0x95, 0x7A, 0x2B, 0x9C, 0x4B, 0xD6, 0x1F);

/* MinGW's propkey.h ships an incomplete subset of the Windows shell
 * property keys. Define the ones we need locally with their canonical
 * FMTID + PID. Source: Microsoft's propkey.h. */
#ifndef PKEY_Software_ProductVersion
DEFINE_PROPERTYKEY(PKEY_Software_ProductVersion,
	0x0CEF7D53, 0xFA64, 0x11D1, 0xA2, 0x03, 0x00, 0x00, 0xF8, 0x1F, 0xED, 0xEE, 8);
#endif

static LONG g_DllRefCount = 0;

/* -------------------------------------------------------------- Helpers */

static HRESULT readEntireStream(IStream *stream, std::nothrow_t, BYTE *&outBuf, DWORD &outSize)
{
	outBuf = nullptr;
	outSize = 0;
	if (!stream) return E_POINTER;

	STATSTG stat = {};
	HRESULT hr = stream->Stat(&stat, STATFLAG_NONAME);
	if (FAILED(hr)) return hr;

	if (stat.cbSize.HighPart != 0 || stat.cbSize.LowPart > 256u * 1024u * 1024u) {
		/* Refuse archives over 256 MiB -- the property handler should not
		 * memory-balloon Explorer. */
		return E_OUTOFMEMORY;
	}
	DWORD sz = stat.cbSize.LowPart;
	if (sz < 22) return E_FAIL;  /* min EOCD */

	BYTE *buf = (BYTE *)CoTaskMemAlloc(sz);
	if (!buf) return E_OUTOFMEMORY;

	LARGE_INTEGER zero = {};
	stream->Seek(zero, STREAM_SEEK_SET, nullptr);

	ULONG read = 0;
	hr = stream->Read(buf, sz, &read);
	if (FAILED(hr) || read != sz) {
		CoTaskMemFree(buf);
		return FAILED(hr) ? hr : E_FAIL;
	}

	outBuf = buf;
	outSize = sz;
	return S_OK;
}

static HRESULT setPropString(PROPVARIANT *out, const char *utf8, size_t len)
{
	if (!out) return E_POINTER;
	PropVariantInit(out);
	if (!utf8 || len == 0) return S_OK;

	int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, (int)len, nullptr, 0);
	if (wlen <= 0) return E_FAIL;
	wchar_t *wbuf = (wchar_t *)CoTaskMemAlloc((size_t)(wlen + 1) * sizeof(wchar_t));
	if (!wbuf) return E_OUTOFMEMORY;
	MultiByteToWideChar(CP_UTF8, 0, utf8, (int)len, wbuf, wlen);
	wbuf[wlen] = 0;

	out->vt = VT_LPWSTR;
	out->pwszVal = wbuf;
	return S_OK;
}

/* --------------------------------------------------- PD2ModPropHandler */

class PD2ModPropHandler : public IInitializeWithStream, public IPropertyStore
{
public:
	PD2ModPropHandler() : m_ref(1), m_initialized(false), m_archive(nullptr), m_modJson(nullptr), m_modJsonLen(0) {
		InterlockedIncrement(&g_DllRefCount);
	}

	virtual ~PD2ModPropHandler() {
		releaseAll();
		InterlockedDecrement(&g_DllRefCount);
	}

	/* IUnknown */
	IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv) override {
		if (!ppv) return E_POINTER;
		if (riid == IID_IUnknown ||
		    riid == IID_IInitializeWithStream) {
			*ppv = static_cast<IInitializeWithStream*>(this);
		} else if (riid == IID_IPropertyStore) {
			*ppv = static_cast<IPropertyStore*>(this);
		} else {
			*ppv = nullptr;
			return E_NOINTERFACE;
		}
		AddRef();
		return S_OK;
	}
	IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_ref); }
	IFACEMETHODIMP_(ULONG) Release() override {
		LONG r = InterlockedDecrement(&m_ref);
		if (r == 0) delete this;
		return r;
	}

	/* IInitializeWithStream */
	IFACEMETHODIMP Initialize(IStream *stream, DWORD /*grfMode*/) override {
		if (m_initialized) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);

		BYTE *buf = nullptr;
		DWORD sz = 0;
		HRESULT hr = readEntireStream(stream, std::nothrow, buf, sz);
		if (FAILED(hr)) return hr;

		m_archive = pdmodZipOpenMemory(buf, sz);
		CoTaskMemFree(buf);
		if (!m_archive) return E_FAIL;

		void *mfst = nullptr;
		uint32_t mfstLen = 0;
		if (!pdmodZipExtractToMalloc(m_archive, "mod.json", &mfst, &mfstLen)) {
			pdmodZipClose(m_archive);
			m_archive = nullptr;
			return E_FAIL;
		}

		m_modJson = (char *)mfst;
		m_modJsonLen = mfstLen;
		m_initialized = true;
		return S_OK;
	}

	/* IPropertyStore */
	IFACEMETHODIMP GetCount(DWORD *cProps) override {
		if (!cProps) return E_POINTER;
		*cProps = m_initialized ? PROP_COUNT : 0;
		return S_OK;
	}
	IFACEMETHODIMP GetAt(DWORD iProp, PROPERTYKEY *pkey) override {
		if (!pkey) return E_POINTER;
		if (!m_initialized || iProp >= PROP_COUNT) return E_INVALIDARG;
		*pkey = s_PropOrder[iProp];
		return S_OK;
	}
	IFACEMETHODIMP GetValue(REFPROPERTYKEY key, PROPVARIANT *pv) override {
		if (!pv) return E_POINTER;
		PropVariantInit(pv);
		if (!m_initialized) return S_OK;

		const char *value = nullptr;
		size_t len = 0;
		if (IsEqualPropertyKey(key, PKEY_Title)) {
			value = pdmodJsonFindString(m_modJson, m_modJsonLen, "name", &len);
		} else if (IsEqualPropertyKey(key, PKEY_Author)) {
			/* Try "creator" first per design Section 4.5.1, fall back to "author". */
			value = pdmodJsonFindString(m_modJson, m_modJsonLen, "creator", &len);
			if (!value) value = pdmodJsonFindString(m_modJson, m_modJsonLen, "author", &len);
			if (value && len > 0) {
				/* PKEY_Author is multi-string (VT_VECTOR | VT_LPWSTR). Use a
				 * single-element vector so Explorer renders a single author. */
				int wlen = MultiByteToWideChar(CP_UTF8, 0, value, (int)len, nullptr, 0);
				if (wlen <= 0) return S_OK;
				wchar_t *wbuf = (wchar_t *)CoTaskMemAlloc((size_t)(wlen + 1) * sizeof(wchar_t));
				if (!wbuf) return E_OUTOFMEMORY;
				MultiByteToWideChar(CP_UTF8, 0, value, (int)len, wbuf, wlen);
				wbuf[wlen] = 0;
				wchar_t **arr = (wchar_t **)CoTaskMemAlloc(sizeof(wchar_t *));
				if (!arr) { CoTaskMemFree(wbuf); return E_OUTOFMEMORY; }
				arr[0] = wbuf;
				pv->vt = VT_LPWSTR | VT_VECTOR;
				pv->calpwstr.cElems = 1;
				pv->calpwstr.pElems = arr;
				return S_OK;
			}
			return S_OK;
		} else if (IsEqualPropertyKey(key, PKEY_Comment)) {
			value = pdmodJsonFindString(m_modJson, m_modJsonLen, "description", &len);
		} else if (IsEqualPropertyKey(key, PKEY_Software_ProductVersion)) {
			value = pdmodJsonFindString(m_modJson, m_modJsonLen, "version", &len);
		} else if (IsEqualPropertyKey(key, PKEY_Keywords)) {
			/* Joined comma-separated tags. We do not parse the tags array
			 * deeply here; the comment mirror or the loader's manifest UI
			 * give richer access. Phase 1 surface: just emit the
			 * mod.json `id` so users can see something distinguishing in
			 * the Keywords column even before the full tags pipeline lands. */
			value = pdmodJsonFindString(m_modJson, m_modJsonLen, "id", &len);
		}

		if (value && len > 0) {
			return setPropString(pv, value, len);
		}
		return S_OK;
	}
	IFACEMETHODIMP SetValue(REFPROPERTYKEY /*key*/, REFPROPVARIANT /*pv*/) override {
		/* Read-only handler. */
		return STG_E_ACCESSDENIED;
	}
	IFACEMETHODIMP Commit() override { return S_OK; }

private:
	void releaseAll() {
		if (m_archive) {
			pdmodZipClose(m_archive);
			m_archive = nullptr;
		}
		if (m_modJson) {
			free(m_modJson);
			m_modJson = nullptr;
		}
		m_modJsonLen = 0;
		m_initialized = false;
	}

	LONG                m_ref;
	bool                m_initialized;
	pdmod_zip_t        *m_archive;
	char               *m_modJson;
	uint32_t            m_modJsonLen;

	static const DWORD PROP_COUNT = 5;
	static const PROPERTYKEY s_PropOrder[PROP_COUNT];
};

const PROPERTYKEY PD2ModPropHandler::s_PropOrder[PROP_COUNT] = {
	PKEY_Title,
	PKEY_Author,
	PKEY_Comment,
	PKEY_Software_ProductVersion,
	PKEY_Keywords,
};

/* ----------------------------------------------------- Class factory + DLL */

class ClassFactory : public IClassFactory
{
public:
	ClassFactory() : m_ref(1) { InterlockedIncrement(&g_DllRefCount); }
	virtual ~ClassFactory() { InterlockedDecrement(&g_DllRefCount); }

	IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv) override {
		if (!ppv) return E_POINTER;
		if (riid == IID_IUnknown || riid == IID_IClassFactory) {
			*ppv = static_cast<IClassFactory*>(this);
			AddRef();
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}
	IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_ref); }
	IFACEMETHODIMP_(ULONG) Release() override {
		LONG r = InterlockedDecrement(&m_ref);
		if (r == 0) delete this;
		return r;
	}
	IFACEMETHODIMP CreateInstance(IUnknown *outer, REFIID riid, void **ppv) override {
		if (outer) return CLASS_E_NOAGGREGATION;
		PD2ModPropHandler *h = new (std::nothrow) PD2ModPropHandler();
		if (!h) return E_OUTOFMEMORY;
		HRESULT hr = h->QueryInterface(riid, ppv);
		h->Release();
		return hr;
	}
	IFACEMETHODIMP LockServer(BOOL fLock) override {
		if (fLock) InterlockedIncrement(&g_DllRefCount);
		else       InterlockedDecrement(&g_DllRefCount);
		return S_OK;
	}

private:
	LONG m_ref;
};

extern "C" __declspec(dllexport) HRESULT __stdcall DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv)
{
	if (!ppv) return E_POINTER;
	if (rclsid != CLSID_PD2ModPropHandler) {
		*ppv = nullptr;
		return CLASS_E_CLASSNOTAVAILABLE;
	}
	ClassFactory *cf = new (std::nothrow) ClassFactory();
	if (!cf) return E_OUTOFMEMORY;
	HRESULT hr = cf->QueryInterface(riid, ppv);
	cf->Release();
	return hr;
}

extern "C" __declspec(dllexport) HRESULT __stdcall DllCanUnloadNow(void)
{
	return (g_DllRefCount == 0) ? S_OK : S_FALSE;
}

/* DllRegisterServer / DllUnregisterServer are stubbed in this build because
 * the registry edits are richer than a single CLSID -- the operator-facing
 * register.ps1 / unregister.ps1 scripts under tools/pdmod_prophandler/install/
 * cover the full picture per design Section 4.5.2. Stubs returning S_OK keep
 * regsvr32 happy if anyone runs it manually. */
extern "C" __declspec(dllexport) HRESULT __stdcall DllRegisterServer(void)
{
	return S_OK;
}
extern "C" __declspec(dllexport) HRESULT __stdcall DllUnregisterServer(void)
{
	return S_OK;
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID /*reserved*/)
{
	if (reason == DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls(hInst);
	}
	return TRUE;
}
