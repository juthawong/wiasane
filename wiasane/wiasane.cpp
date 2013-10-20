#include "wiasane.h"

#include <sti.h>
#include <math.h>
#include <winioctl.h>
#include <usbscan.h>
#include <stdlib.h>
#include <tchar.h>

#include "wiasane_opt.h"
#include "wiasane_util.h"


WIAMICRO_API HRESULT MicroEntry(LONG lCommand, _Inout_ PVAL pValue)
{
	PWIASANE_Context pContext;
	PWINSANE_Option option;
	PWINSANE_Params params;
	HANDLE hHeap;
	HRESULT hr;

#ifdef _DEBUG
	if (lCommand != CMD_STI_GETSTATUS)
		Trace(TEXT("Command Value (%d)"),lCommand);
#endif

	if (!pValue || !pValue->pScanInfo)
		return E_INVALIDARG;

	if (!pValue->pScanInfo->DeviceIOHandles[1])
		pValue->pScanInfo->DeviceIOHandles[1] = GetProcessHeap();

	hHeap = pValue->pScanInfo->DeviceIOHandles[1];
	if (!hHeap)
		return E_OUTOFMEMORY;

	if (!pValue->pScanInfo->pMicroDriverContext)
		pValue->pScanInfo->pMicroDriverContext = HeapAlloc(hHeap, HEAP_ZERO_MEMORY, sizeof(WIASANE_Context));

	hr = E_NOTIMPL;
	pContext = (PWIASANE_Context) pValue->pScanInfo->pMicroDriverContext;

    switch (lCommand) {
		case CMD_SETSTIDEVICEHKEY:
			if (pValue->pHandle)
				pValue->pScanInfo->DeviceIOHandles[2] = *pValue->pHandle;

			if (pContext)
				hr = ReadRegistryInformation(pValue->pScanInfo, pContext);
			else
				hr = E_FAIL;

			break;

		case CMD_INITIALIZE:
			if (pContext) {
				if (pContext->session)
					hr = UninitializeScanner(pValue->pScanInfo, pContext);
				else
					hr = S_OK;

				if (SUCCEEDED(hr))
					hr = InitializeScanner(pValue->pScanInfo, pContext);
			} else
				hr = E_FAIL;

			if (SUCCEEDED(hr))
				hr = InitScannerDefaults(pValue->pScanInfo, pContext);

			pValue->pScanInfo->DeviceIOHandles[2] = NULL;

			break;

		case CMD_UNINITIALIZE:
			if (pContext) {
				if (pContext->session)
					hr = UninitializeScanner(pValue->pScanInfo, pContext);
				else
					hr = S_OK;

				if (SUCCEEDED(hr))
					hr = FreeScanner(pValue->pScanInfo, pContext);

				if (SUCCEEDED(hr))
					pContext = NULL;
			} else
				hr = S_OK;

			pValue->pScanInfo->pMicroDriverContext = pContext;

			break;

		case CMD_RESETSCANNER:
		case CMD_STI_DEVICERESET:
			if (pContext && pContext->session && pContext->device) {
				if (!pContext->device->Cancel()) {
					hr = E_FAIL;
					break;
				}
			}

			hr = S_OK;
			break;

		case CMD_STI_DIAGNOSTIC:
			if (pContext && pContext->session && pContext->device) {
				if (!pContext->device->FetchOptions()) {
					hr = E_FAIL;
					break;
				}
			}

			hr = S_OK;
			break;

		case CMD_STI_GETSTATUS:
			pValue->lVal = MCRO_ERROR_OFFLINE;
			pValue->pGuid = (GUID*) &GUID_NULL;

			if (pContext && pContext->session && pContext->device) {
				params = pContext->device->GetParams();
				if (params) {
					pValue->lVal = MCRO_STATUS_OK;
				}
			}

			hr = S_OK;
			break;

		case CMD_SETXRESOLUTION:
		case CMD_SETYRESOLUTION:
			if (pContext && pContext->session && pContext->device) {
				option = pContext->device->GetOption("resolution");
				if (!option) {
					hr = E_NOTIMPL;
					break;
				}
				if (!option->IsValidValue(pValue->lVal)) {
					hr = E_INVALIDARG;
					break;
				}
			} else {
				hr = E_FAIL;
				break;
			}

			switch (lCommand) {
				case CMD_SETXRESOLUTION:
					pValue->pScanInfo->Xresolution = pValue->lVal;
					break;

				case CMD_SETYRESOLUTION:
					pValue->pScanInfo->Yresolution = pValue->lVal;
					break;
			}

			hr = S_OK;
			break;

		case CMD_SETCONTRAST:
			if (pContext && pContext->session && pContext->device) {
				option = pContext->device->GetOption("contrast");
				if (!option) {
					hr = E_NOTIMPL;
					break;
				}
				if (!option->IsValidValue(pValue->lVal)) {
					hr = E_INVALIDARG;
					break;
				}
			} else {
				hr = E_FAIL;
				break;
			}

			pValue->pScanInfo->Contrast = pValue->lVal;

			hr = S_OK;
			break;

		case CMD_SETINTENSITY:
			if (pContext && pContext->session && pContext->device) {
				option = pContext->device->GetOption("brightness");
				if (!option) {
					hr = E_NOTIMPL;
					break;
				}
				if (!option->IsValidValue(pValue->lVal)) {
					hr = E_INVALIDARG;
					break;
				}
			} else {
				hr = E_FAIL;
				break;
			}

			pValue->pScanInfo->Intensity = pValue->lVal;

			hr = S_OK;
			break;

		case CMD_SETDATATYPE:
			pValue->pScanInfo->DataType = pValue->lVal;
			hr = S_OK;
			break;

		case CMD_SETNEGATIVE:
			pValue->pScanInfo->Negative = pValue->lVal;
			hr = S_OK;
			break;

		case CMD_GETADFSTATUS:
		case CMD_GETADFHASPAPER:
			// pValue->lVal = MCRO_ERROR_PAPER_EMPTY;
			// hr = S_OK;
			break;

		case CMD_GET_INTERRUPT_EVENT:
			break;

		case CMD_GETCAPABILITIES:
			pValue->lVal = 0;
			pValue->pGuid = NULL;
			pValue->ppButtonNames = NULL;

			hr = S_OK;
			break;

		case CMD_SETSCANMODE:
			hr = SetScanMode(pValue->pScanInfo, pValue->lVal);
			break;

		default:
			Trace(TEXT("Unknown Command (%d)"),lCommand);
			break;
    }

    return hr;
}

WIAMICRO_API HRESULT Scan(_Inout_ PSCANINFO pScanInfo, LONG lPhase, _Out_writes_bytes_(lLength) PBYTE pBuffer, LONG lLength, _Out_ LONG *plReceived)
{
	PWIASANE_Context pContext;
	LONG idx, aquired;
	DWORD aquire;
	HANDLE hHeap;
	HRESULT hr;

	if (plReceived)
		*plReceived = 0;

    Trace(TEXT("------ Scan Requesting %d ------"), lLength);

	if (pScanInfo == NULL)
		return E_INVALIDARG;

	hHeap = pScanInfo->DeviceIOHandles[1];
	if (!hHeap)
		return E_OUTOFMEMORY;

	pContext = (PWIASANE_Context) pScanInfo->pMicroDriverContext;
	if (!pContext)
		return E_OUTOFMEMORY;

    switch (lPhase) {
		case SCAN_FIRST:
			Trace(TEXT("SCAN_FIRST"));

			//
			// first phase
			//

			if (pContext->session && pContext->device) {
				hr = SetScannerSettings(pScanInfo, pContext);
				if (FAILED(hr))
					return hr;

				pContext->task = (PWIASANE_Task) HeapAlloc(hHeap, HEAP_ZERO_MEMORY, sizeof(WIASANE_Task));
				if (!pContext->task)
					return E_OUTOFMEMORY;

				pContext->task->scan = pContext->device->Start();
				if (pContext->task->scan->Connect() != CONTINUE)
					return E_FAIL;

				hr = FetchScannerParams(pScanInfo, pContext);
				if (FAILED(hr))
					return hr;

				if (pScanInfo->PixelBits > 1)
					pContext->task->xbytegap = (pScanInfo->Window.xExtent * pScanInfo->PixelBits / 8) - pScanInfo->WidthBytes;
				else
					pContext->task->xbytegap = ((LONG) floor(pScanInfo->Window.xExtent + 7.0) / 8) - pScanInfo->WidthBytes;
				pContext->task->ybytegap = (pScanInfo->Window.yExtent - pScanInfo->Lines) * pScanInfo->WidthBytes;

				pContext->task->total = pScanInfo->WidthBytes * pScanInfo->Lines;
				pContext->task->received = 0;
				Trace(TEXT("Data: %d/%d"), pContext->task->received, pContext->task->total);
			}

		case SCAN_NEXT: // SCAN_FIRST will fall through to SCAN_NEXT (because it is expecting data)
			if (lPhase == SCAN_NEXT)
				Trace(TEXT("SCAN_NEXT"));

			//
			// next phase, get data from the scanner and set plReceived value
			//

			if (pContext->session && pContext->device && pContext->task && pContext->task->scan) {
				memset(pBuffer, 0, lLength);

				aquire = pContext->task->xbytegap ? min(lLength, pScanInfo->WidthBytes) : lLength;
				aquired = 0;

				while (pContext->task->scan->AquireImage((pBuffer + *plReceived + aquired), &aquire) == CONTINUE) {
					if (aquire > 0) {
						if (pContext->task->xbytegap) {
							aquired += aquire;
							if (aquired == pScanInfo->WidthBytes) {
								*plReceived += aquired;
								if (lLength - *plReceived >= pContext->task->xbytegap)
									*plReceived += pContext->task->xbytegap;
								aquired = 0;
							}
							if (lLength - *plReceived < pScanInfo->WidthBytes - aquired)
								break;
							aquire = pScanInfo->WidthBytes - aquired;
						} else {
							*plReceived += aquire;
							aquire = lLength - *plReceived;
						}
					}
					if (aquire <= 0)
						break;
				}

				if (pContext->task->ybytegap > 0 && *plReceived < lLength) {
					aquired = min(lLength - *plReceived, pContext->task->ybytegap);
					memset(pBuffer + *plReceived, -1, aquired);
					*plReceived += aquired;
				}

				if (pScanInfo->DataType == WIA_DATA_THRESHOLD) {
					for (idx = 0; idx < *plReceived; idx++) {
						pBuffer[idx] = ~pBuffer[idx];
					}
				}

				pContext->task->total = pScanInfo->WidthBytes * pScanInfo->Lines;
				pContext->task->received += *plReceived;
				Trace(TEXT("Data: %d/%d -> %d/%d"), pContext->task->received, pContext->task->total, pContext->task->total - pContext->task->received, lLength);
			}

			break;

		case SCAN_FINISHED:
		default:
			Trace(TEXT("SCAN_FINISHED"));

			//
			// stop scanner, do not set lRecieved, or write any data to pBuffer.  Those values
			// will be NULL.  This lPhase is only to allow you to stop scanning, and return the
			// scan head to the HOME position. SCAN_FINISHED will be called always for regular scans, and
			// for cancelled scans.
			//

			if (pContext->session && pContext->device) {
				if (pContext->task->scan) {
					delete pContext->task->scan;
					pContext->task->scan = NULL;
				}
				if (pContext->task) {
					ZeroMemory(pContext->task, sizeof(WIASANE_Task));
					HeapFree(hHeap, 0, pContext->task);
					pContext->task = NULL;
				}

				pContext->device->Cancel();
			}

			break;
    }

    return S_OK;
}

WIAMICRO_API HRESULT SetPixelWindow(_Inout_ PSCANINFO pScanInfo, LONG x, LONG y, LONG xExtent, LONG yExtent)
{
	PWIASANE_Context pContext;
	PWINSANE_Option opt_tl_x, opt_tl_y, opt_br_x, opt_br_y;
	HRESULT hr;
	double tl_x, tl_y, br_x, br_y;

    if (pScanInfo == NULL)
        return E_INVALIDARG;

	pContext = (PWIASANE_Context) pScanInfo->pMicroDriverContext;
	if (!pContext)
		return E_OUTOFMEMORY;

	if (!pContext->session || !pContext->device)
		return E_FAIL;

	opt_tl_x = pContext->device->GetOption("tl-x");
	opt_tl_y = pContext->device->GetOption("tl-y");
	opt_br_x = pContext->device->GetOption("br-x");
	opt_br_y = pContext->device->GetOption("br-y");

	if (!opt_tl_x || !opt_tl_y || !opt_br_x || !opt_br_y)
		return E_INVALIDARG;

	tl_x = ((double) x) / ((double) pScanInfo->Xresolution);
	tl_y = ((double) y) / ((double) pScanInfo->Yresolution);
	br_x = ((double) (x + xExtent)) / ((double) pScanInfo->Xresolution);
	br_y = ((double) (y + yExtent)) / ((double) pScanInfo->Yresolution);

	hr = IsValidOptionValueInch(opt_tl_x, tl_x);
	if (FAILED(hr))
		return hr;

	hr = IsValidOptionValueInch(opt_tl_y, tl_y);
	if (FAILED(hr))
		return hr;

	hr = IsValidOptionValueInch(opt_br_x, br_x);
	if (FAILED(hr))
		return hr;

	hr = IsValidOptionValueInch(opt_br_y, br_y);
	if (FAILED(hr))
		return hr;

	hr = SetOptionValueInch(opt_tl_x, tl_x);
	if (FAILED(hr))
		return hr;

	hr = SetOptionValueInch(opt_tl_y, tl_y);
	if (FAILED(hr))
		return hr;

	hr = SetOptionValueInch(opt_br_x, br_x);
	if (FAILED(hr))
		return hr;

	hr = SetOptionValueInch(opt_br_y, br_y);
	if (FAILED(hr))
		return hr;

    pScanInfo->Window.xPos = x;
    pScanInfo->Window.yPos = y;
    pScanInfo->Window.xExtent = xExtent;
    pScanInfo->Window.yExtent = yExtent;

#ifdef _DEBUG
    Trace(TEXT("Scanner window"));
    Trace(TEXT("xpos  = %d"), pScanInfo->Window.xPos);
    Trace(TEXT("ypos  = %d"), pScanInfo->Window.yPos);
    Trace(TEXT("xext  = %d"), pScanInfo->Window.xExtent);
    Trace(TEXT("yext  = %d"), pScanInfo->Window.yExtent);
#endif

    return S_OK;
}


HRESULT ReadRegistryInformation(_Inout_ PSCANINFO pScanInfo, _Inout_ PWIASANE_Context pContext)
{
	HKEY hKey, hOpenKey;
	DWORD dwWritten, dwType, dwPort;
	PTSTR ptHost, ptName;
	LSTATUS st;
	HRESULT hr;

	hr = S_OK;
	hKey = (HKEY) pScanInfo->DeviceIOHandles[2];
	hOpenKey = NULL;

	if (pContext->host)
		free(pContext->host);

	if (pContext->name)
		free(pContext->name);

	pContext->port = WINSANE_DEFAULT_PORT;
	pContext->host = _tcsdup(TEXT("localhost"));
	pContext->name = NULL;

	//
	// Open DeviceData section to read driver specific information
	//

	st = RegOpenKeyEx(hKey, TEXT("DeviceData"), 0, KEY_QUERY_VALUE|KEY_READ, &hOpenKey);
	if (st == ERROR_SUCCESS) {
		dwWritten = sizeof(DWORD);
		dwType = REG_DWORD;
		dwPort = WINSANE_DEFAULT_PORT;

		st = RegQueryValueEx(hOpenKey, TEXT("Port"), NULL, &dwType, (LPBYTE)&dwPort, &dwWritten);
		if (st == ERROR_SUCCESS) {
			pContext->port = (USHORT) dwPort;
		} else
			hr = E_FAIL;

		dwWritten = 0;
		dwType = REG_SZ;
		ptHost = NULL;

		st = RegQueryValueEx(hOpenKey, TEXT("Host"), NULL, &dwType, (LPBYTE)ptHost, &dwWritten);
		if (st == ERROR_SUCCESS && dwWritten > 0) {
			ptHost = (PTSTR) HeapAlloc(pScanInfo->DeviceIOHandles[1], HEAP_ZERO_MEMORY, dwWritten);
			if (ptHost) {
				st = RegQueryValueEx(hOpenKey, TEXT("Host"), NULL, &dwType, (LPBYTE)ptHost, &dwWritten);
				if (st == ERROR_SUCCESS) {
					if (pContext->host)
						free(pContext->host);

					pContext->host = _tcsdup(ptHost);
				} else
					hr = E_FAIL;

				HeapFree(pScanInfo->DeviceIOHandles[1], 0, ptHost);
			} else
				hr = E_OUTOFMEMORY;
		} else
			hr = E_FAIL;

		dwWritten = 0;
		dwType = REG_SZ;
		ptName = NULL;

		st = RegQueryValueEx(hOpenKey, TEXT("Name"), NULL, &dwType, (LPBYTE)ptName, &dwWritten);
		if (st == ERROR_SUCCESS && dwWritten > 0) {
			ptName = (PTSTR) HeapAlloc(pScanInfo->DeviceIOHandles[1], HEAP_ZERO_MEMORY, dwWritten);
			if (ptName) {
				st = RegQueryValueEx(hOpenKey, TEXT("Name"), NULL, &dwType, (LPBYTE)ptName, &dwWritten);
				if (st == ERROR_SUCCESS) {
					if (pContext->name)
						free(pContext->name);

					pContext->name = _tcsdup(ptName);
				} else
					hr = E_FAIL;

				HeapFree(pScanInfo->DeviceIOHandles[1], 0, ptName);
			} else
				hr = E_OUTOFMEMORY;
		} else
			hr = E_FAIL;
	} else
		hr = E_ACCESSDENIED;

	return hr;
}

HRESULT InitializeScanner(_Inout_ PSCANINFO pScanInfo, _Inout_ PWIASANE_Context pContext)
{
	UNREFERENCED_PARAMETER(pScanInfo);

	pContext->task = NULL;
	pContext->session = WINSANE_Session::Remote(pContext->host, pContext->port);
	if (pContext->session && pContext->session->Init(NULL, NULL) && pContext->session->GetDevices() > 0) {
		pContext->device = pContext->session->GetDevice(pContext->name);
		if (pContext->device && pContext->device->Open()) {
			pContext->device->FetchOptions();
		} else {
			pContext->device = NULL;
			return E_FAIL;
		}
	} else {
		pContext->device = NULL;
		return E_FAIL;
	}

    return S_OK;
}

HRESULT UninitializeScanner(_Inout_ PSCANINFO pScanInfo, _Inout_ PWIASANE_Context pContext)
{
	if (pContext->session) {
		if (pContext->device) {
			if (pContext->task) {
				if (pContext->task->scan) {
					pContext->device->Cancel();
					delete pContext->task->scan;
					pContext->task->scan = NULL;
				}
				ZeroMemory(pContext->task, sizeof(WIASANE_Task));
				HeapFree(pScanInfo->DeviceIOHandles[1], 0, pContext->task);
				pContext->task = NULL;
			}
			pContext->device->Close();
			pContext->device = NULL;
		}
		pContext->session->Exit();
		delete pContext->session;
		pContext->session = NULL;
	}

	return S_OK;
}

HRESULT FreeScanner(_Inout_ PSCANINFO pScanInfo, _Inout_ PWIASANE_Context pContext)
{
	if (pContext->host)
		free(pContext->host);

	if (pContext->name)
		free(pContext->name);

	ZeroMemory(pContext, sizeof(WIASANE_Context));
	HeapFree(pScanInfo->DeviceIOHandles[1], 0, pContext);

	return S_OK;
}

HRESULT InitScannerDefaults(_Inout_ PSCANINFO pScanInfo, _Inout_ PWIASANE_Context pContext)
{
	PWINSANE_Option option;
	PSANE_String_Const string_list;
	PSANE_Range range;
	HRESULT hr;
	double dbl;
	int index;

	if (pContext && pContext->session && pContext->device) {
		pScanInfo->ADF                = 0; // set to no ADF in Test device
		pScanInfo->bNeedDataAlignment = TRUE;

		pScanInfo->SupportedCompressionType = 0;
		pScanInfo->SupportedDataTypes       = 0;

		option = pContext->device->GetOption("mode");
		if (option && option->GetType() == SANE_TYPE_STRING && option->GetConstraintType() == SANE_CONSTRAINT_STRING_LIST) {
			string_list = option->GetConstraintStringList();
			for (index = 0; string_list[index] != NULL; index++) {
				if (strcmp(string_list[index], "Lineart") == 0) {
					pScanInfo->SupportedDataTypes |= SUPPORT_BW;
				} else if (strcmp(string_list[index], "Gray") == 0) {
					pScanInfo->SupportedDataTypes |= SUPPORT_GRAYSCALE;
				} else if (strcmp(string_list[index], "Color") == 0) {
					pScanInfo->SupportedDataTypes |= SUPPORT_COLOR;
				}
			}
		}

		pScanInfo->BedWidth  = 8500;  // 1000's of an inch (WIA compatible unit)
		pScanInfo->BedHeight = 11000; // 1000's of an inch (WIA compatible unit)

		option = pContext->device->GetOption("br-x");
		if (option) {
			hr = GetOptionMaxValueInch(option, &dbl);
			if (SUCCEEDED(hr)) {
				pScanInfo->BedWidth = (LONG) (dbl * 1000.0);
			}
		}
		option = pContext->device->GetOption("br-y");
		if (option) {
			hr = GetOptionMaxValueInch(option, &dbl);
			if (SUCCEEDED(hr)) {
				pScanInfo->BedHeight = (LONG) (dbl * 1000.0);
			}
		}

		pScanInfo->OpticalXResolution = 300;
		pScanInfo->Xresolution = pScanInfo->OpticalXResolution;

		option = pContext->device->GetOption("resolution");
		if (option) {
			hr = GetOptionMaxValue(option, &dbl);
			if (SUCCEEDED(hr)) {
				pScanInfo->OpticalXResolution = (LONG) dbl;
			}
			hr = option->GetValue(&dbl);
			if (SUCCEEDED(hr) == S_OK) {
				pScanInfo->Xresolution = (LONG) dbl;
			}
		}

		pScanInfo->OpticalYResolution = pScanInfo->OpticalXResolution;
		pScanInfo->Yresolution = pScanInfo->Xresolution;

		pScanInfo->Contrast            = 0;
		pScanInfo->ContrastRange.lMin  = 0;
		pScanInfo->ContrastRange.lMax  = 100;
		pScanInfo->ContrastRange.lStep = 1;

		option = pContext->device->GetOption("contrast");
		if (option) {
			if (option->GetConstraintType() == SANE_CONSTRAINT_RANGE) {
				range = option->GetConstraintRange();
				pScanInfo->Contrast            = (range->min + range->max) / 2;
				pScanInfo->ContrastRange.lMin  = range->min;
				pScanInfo->ContrastRange.lMax  = range->max;
				pScanInfo->ContrastRange.lStep = range->quant ? range->quant : 1;
			}
			hr = option->GetValue(&dbl);
			if (SUCCEEDED(hr)) {
				pScanInfo->Contrast = (LONG) dbl;
			}
		}
		
		pScanInfo->Intensity            = 0;
		pScanInfo->IntensityRange.lMin  = 0;
		pScanInfo->IntensityRange.lMax  = 100;
		pScanInfo->IntensityRange.lStep = 1;

		option = pContext->device->GetOption("brightness");
		if (option) {
			if (option->GetConstraintType() == SANE_CONSTRAINT_RANGE) {
				range = option->GetConstraintRange();
				pScanInfo->IntensityRange.lMin  = (range->min + range->max) / 2;
				pScanInfo->IntensityRange.lMax  = range->max;
				pScanInfo->IntensityRange.lStep = range->quant ? range->quant : 1;
			}
			hr = option->GetValue(&dbl);
			if (SUCCEEDED(hr)) {
				pScanInfo->Intensity = (LONG) dbl;
			}
		}

		pScanInfo->WidthPixels            = (LONG) (((double) (pScanInfo->BedWidth  * pScanInfo->Xresolution)) / 1000.0);
		pScanInfo->Lines                  = (LONG) (((double) (pScanInfo->BedHeight * pScanInfo->Yresolution)) / 1000.0);

		pScanInfo->Window.xPos            = 0;
		pScanInfo->Window.yPos            = 0;
		pScanInfo->Window.xExtent         = pScanInfo->WidthPixels;
		pScanInfo->Window.yExtent         = pScanInfo->Lines;

		// Scanner options
		pScanInfo->Negative               = 0;
		pScanInfo->Mirror                 = 0;
		pScanInfo->AutoBack               = 0;
		pScanInfo->DitherPattern          = 0;
		pScanInfo->ColorDitherPattern     = 0;
		pScanInfo->ToneMap                = 0;
		pScanInfo->Compression            = 0;

		hr = FetchScannerParams(pScanInfo, pContext);
		if (FAILED(hr))
			return hr;
	}

#ifdef _DEBUG
    Trace(TEXT("bw    = %d"), pScanInfo->BedWidth);
    Trace(TEXT("bh    = %d"), pScanInfo->BedHeight);
#endif

	return S_OK;
}

HRESULT SetScannerSettings(_Inout_ PSCANINFO pScanInfo, _Inout_ PWIASANE_Context pContext)
{
	PWINSANE_Option option;
	HRESULT hr;

	if (pContext && pContext->session && pContext->device) {
		option = pContext->device->GetOption("mode");
		if (option && option->GetType() == SANE_TYPE_STRING) {
			switch (pScanInfo->DataType) {
				case WIA_DATA_THRESHOLD:
					hr = option->SetValueString("Lineart");
					break;
				case WIA_DATA_GRAYSCALE:
					hr = option->SetValueString("Gray");
					break;
				case WIA_DATA_COLOR:
					hr = option->SetValueString("Color");
					break;
				default:
					hr = E_INVALIDARG;
					break;
			}
			if (FAILED(hr))
				return hr;
		} else
			return E_NOTIMPL;

		option = pContext->device->GetOption("resolution");
		if (option) {
			hr = option->SetValue(pScanInfo->Xresolution);
			if (FAILED(hr))
				return hr;
		} else
			return E_NOTIMPL;

		option = pContext->device->GetOption("contrast");
		if (option) {
			hr = option->SetValue(pScanInfo->Contrast);
			if (FAILED(hr) && hr != E_NOTIMPL)
				return hr;
		}

		option = pContext->device->GetOption("brightness");
		if (option) {
			hr = option->SetValue(pScanInfo->Intensity);
			if (FAILED(hr) && hr != E_NOTIMPL)
				return hr;
		}
	}

    return S_OK;
}

HRESULT FetchScannerParams(_Inout_ PSCANINFO pScanInfo, _Inout_ PWIASANE_Context pContext)
{
	PWINSANE_Params params;
	SANE_Frame frame;
	SANE_Int depth;
	HRESULT hr;

	params = pContext->device->GetParams();
	if (!params)
		return E_FAIL;

	frame = params->GetFormat();
	depth = params->GetDepth();

	switch (frame) {
		case SANE_FRAME_GRAY:
			pScanInfo->RawDataFormat = WIA_PACKED_PIXEL;
			pScanInfo->RawPixelOrder = 0;
			pScanInfo->DataType = depth == 1 ? WIA_DATA_THRESHOLD : WIA_DATA_GRAYSCALE;
			pScanInfo->PixelBits = depth;
			hr = S_OK;
			break;

		case SANE_FRAME_RGB:
			pScanInfo->RawDataFormat = WIA_PACKED_PIXEL;
			pScanInfo->RawPixelOrder = WIA_ORDER_BGR;
			pScanInfo->DataType = WIA_DATA_COLOR;
			pScanInfo->PixelBits = depth * 3;
			hr = S_OK;
			break;

		case SANE_FRAME_RED:
		case SANE_FRAME_GREEN:
		case SANE_FRAME_BLUE:
			pScanInfo->RawDataFormat = WIA_PLANAR;
			pScanInfo->RawPixelOrder = WIA_ORDER_BGR;
			pScanInfo->DataType = WIA_DATA_COLOR;
			pScanInfo->PixelBits = depth * 3;
			hr = S_OK;
			break;

		default:
			hr = E_NOTIMPL;
			break;
	}

	if (SUCCEEDED(hr)) {
		pScanInfo->WidthBytes = params->GetBytesPerLine();
		pScanInfo->WidthPixels = params->GetPixelsPerLine();
		pScanInfo->Lines = params->GetLines();
	}

	delete params;

#ifdef _DEBUG
    Trace(TEXT("Scanner parameters"));
    Trace(TEXT("x res = %d"), pScanInfo->Xresolution);
    Trace(TEXT("y res = %d"), pScanInfo->Yresolution);
    Trace(TEXT("bpp   = %d"), pScanInfo->PixelBits);
    Trace(TEXT("xpos  = %d"), pScanInfo->Window.xPos);
    Trace(TEXT("ypos  = %d"), pScanInfo->Window.yPos);
    Trace(TEXT("xext  = %d"), pScanInfo->Window.xExtent);
    Trace(TEXT("yext  = %d"), pScanInfo->Window.yExtent);
	Trace(TEXT("wbyte = %d"), pScanInfo->WidthBytes);
	Trace(TEXT("wpixl = %d"), pScanInfo->WidthPixels);
	Trace(TEXT("lines = %d"), pScanInfo->Lines);
#endif

	return hr;
}

HRESULT SetScanMode(_Inout_ PSCANINFO pScanInfo, _Inout_ LONG lScanMode)
{
	PWIASANE_Context pContext;
	PWINSANE_Option option;
	PSANE_String_Const string_list;
	HRESULT hr;

	pContext = (PWIASANE_Context) pScanInfo->pMicroDriverContext;
	if (!pContext)
		return E_OUTOFMEMORY;

	hr = E_NOTIMPL;

	switch (lScanMode) {
		case SCANMODE_FINALSCAN:
			Trace(TEXT("Final Scan"));

			if (pContext->session && pContext->device) {
				option = pContext->device->GetOption("preview");
				if (option && option->GetType() == SANE_TYPE_BOOL) {
					option->SetValueBool(SANE_FALSE);
				}

				option = pContext->device->GetOption("compression");
				if (option && option->GetType() == SANE_TYPE_STRING
				 && option->GetConstraintType() == SANE_CONSTRAINT_STRING_LIST) {
					string_list = option->GetConstraintStringList();
					if (string_list[0] != NULL) {
						option->SetValueString(string_list[0]);
					}
				}
			}

			hr = S_OK;
			break;

		case SCANMODE_PREVIEWSCAN:
			Trace(TEXT("Preview Scan"));

			if (pContext->session && pContext->device) {
				option = pContext->device->GetOption("preview");
				if (option && option->GetType() == SANE_TYPE_BOOL) {
					option->SetValueBool(SANE_TRUE);
				}

				option = pContext->device->GetOption("compression");
				if (option && option->GetType() == SANE_TYPE_STRING
				 && option->GetConstraintType() == SANE_CONSTRAINT_STRING_LIST) {
					string_list = option->GetConstraintStringList();
					if (string_list[0] != NULL && string_list[1] != NULL) {
						option->SetValueString(string_list[1]);
					}
				}
			}

			hr = S_OK;
			break;

		default:
			Trace(TEXT("Unknown Scan Mode (%d)"), lScanMode);

			hr = E_FAIL;
			break;
	}

	return hr;
}
