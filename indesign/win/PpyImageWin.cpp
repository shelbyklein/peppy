// Windows: the rendered page as a picture an agent can look at, scaled to at
// most maxSide px on its long side, on white, as a JPEG. WIC does the work.

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincodec.h>

#include "../PpyImage.h"

#include <algorithm>
#include <string>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace {

std::wstring Wide(const std::string& s)
{
	int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
	std::wstring w(n > 0 ? n - 1 : 0, L'\0');
	if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
	return w;
}

std::string Base64(const unsigned char* in, size_t size)
{
	static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	out.reserve((size + 2) / 3 * 4);
	size_t i = 0;
	for (; i + 2 < size; i += 3) {
		unsigned v = in[i] << 16 | in[i + 1] << 8 | in[i + 2];
		out += t[v >> 18]; out += t[(v >> 12) & 63]; out += t[(v >> 6) & 63]; out += t[v & 63];
	}
	if (i < size) {
		unsigned v = in[i] << 16 | (i + 1 < size ? in[i + 1] << 8 : 0);
		out += t[v >> 18]; out += t[(v >> 12) & 63];
		out += i + 1 < size ? t[(v >> 6) & 63] : '=';
		out += '=';
	}
	return out;
}

template <class T> struct Com {
	T* p = nullptr;
	~Com() { if (p) p->Release(); }
	T** operator&() { return &p; }
	T* operator->() { return p; }
	operator T*() { return p; }
};

void Check(HRESULT hr, const char* what)
{
	if (FAILED(hr)) throw peppy::Error(peppy::kErrHost, std::string("couldn't ") + what + " (WIC error " + std::to_string((long) hr) + ")");
}

} // namespace

peppy::Json PpyInlineImage(const std::string& path, int maxSide)
{
	HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);   // InDesign usually has COM up already
	struct Uninit { bool on; ~Uninit() { if (on) CoUninitialize(); } } uninit{SUCCEEDED(init)};

	Com<IWICImagingFactory> wic;
	Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)), "start Windows Imaging");
	Com<IWICBitmapDecoder> decoder;
	Check(wic->CreateDecoderFromFilename(Wide(path).c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder), "read the rendered page");
	Com<IWICBitmapFrameDecode> frame;
	Check(decoder->GetFrame(0, &frame), "read the rendered page");
	UINT w = 0, h = 0;
	frame->GetSize(&w, &h);
	double k = std::min(1.0, (double) maxSide / (double) std::max(w, h));
	UINT tw = std::max<UINT>(1, (UINT) (w * k + 0.5)), th = std::max<UINT>(1, (UINT) (h * k + 0.5));

	Com<IWICBitmapScaler> scaler;
	Check(wic->CreateBitmapScaler(&scaler), "scale the picture");
	Check(scaler->Initialize(frame, tw, th, WICBitmapInterpolationModeHighQualityCubic), "scale the picture");
	// Premultiplied BGRA, then flattened onto white by hand (JPEG has no alpha).
	Com<IWICFormatConverter> bgra;
	Check(wic->CreateFormatConverter(&bgra), "convert the picture");
	Check(bgra->Initialize(scaler, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom), "convert the picture");
	std::string pixels((size_t) tw * th * 4, '\0');
	Check(bgra->CopyPixels(nullptr, tw * 4, (UINT) pixels.size(), (BYTE*) &pixels[0]), "convert the picture");
	std::string rgb((size_t) tw * th * 3, '\0');
	for (size_t i = 0, j = 0; i < pixels.size(); i += 4, j += 3) {
		unsigned a = (unsigned char) pixels[i + 3], white = 255 - a;
		rgb[j] = (char) ((unsigned char) pixels[i] + white);         // B
		rgb[j + 1] = (char) ((unsigned char) pixels[i + 1] + white); // G
		rgb[j + 2] = (char) ((unsigned char) pixels[i + 2] + white); // R
	}

	Com<IStream> stream;
	Check(CreateStreamOnHGlobal(nullptr, TRUE, &stream), "encode the picture");
	Com<IWICBitmapEncoder> encoder;
	Check(wic->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder), "encode the picture");
	Check(encoder->Initialize(stream, WICBitmapEncoderNoCache), "encode the picture");
	Com<IWICBitmapFrameEncode> out;
	Com<IPropertyBag2> props;
	Check(encoder->CreateNewFrame(&out, &props), "encode the picture");
	PROPBAG2 option = {};
	option.pstrName = (LPOLESTR) L"ImageQuality";
	VARIANT quality;
	VariantInit(&quality);
	quality.vt = VT_R4;
	quality.fltVal = 0.85f;
	props->Write(1, &option, &quality);
	Check(out->Initialize(props), "encode the picture");
	Check(out->SetSize(tw, th), "encode the picture");
	WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
	Check(out->SetPixelFormat(&format), "encode the picture");
	Check(out->WritePixels(th, tw * 3, (UINT) rgb.size(), (BYTE*) &rgb[0]), "encode the picture");
	Check(out->Commit(), "encode the picture");
	Check(encoder->Commit(), "encode the picture");

	HGLOBAL memory = nullptr;
	Check(GetHGlobalFromStream(stream, &memory), "encode the picture");
	STATSTG stat = {};
	stream->Stat(&stat, STATFLAG_NONAME);
	const unsigned char* bytes = (const unsigned char*) GlobalLock(memory);
	std::string data = Base64(bytes, (size_t) stat.cbSize.QuadPart);
	GlobalUnlock(memory);
	return {{"mimeType", "image/jpeg"}, {"data", data}, {"width", tw}, {"height", th}};
}
