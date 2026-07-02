#include "StdAfx.h"
#include "ssSoundDriverManager.h"
#include "ssUnZip.h"
#include "ssIfFolder.h"
#include "ssIfDriverConfig.h"
#include "ssDriverConfig.h"
#include "ssBindConfig.h"
#include "ssConfigLoader.h"

#import "MSXML.DLL" named_guids


static bool ParseName(MSXML::IXMLDOMNodePtr _pNode, ssDriverConfig *_config)
{
	USES_CONVERSION;
	HRESULT hr;
	BSTR bstr;

	hr = _pNode->get_text(&bstr);
	_config->name = string(W2CA(bstr));

	return true;
}

static bool ParseDriver(MSXML::IXMLDOMNodePtr _pNode, ssIfDriverConfig *_config)
{
	USES_CONVERSION;
	HRESULT hr;
	BSTR bstr;

	hr = _pNode->get_text(&bstr);
	//_config->driver_majortype = string(W2CA(bstr));
	_config->SetDriverType(string(W2CA(bstr)));

	MSXML::IXMLDOMNamedNodeMapPtr pAtt = NULL;

	if (SUCCEEDED(_pNode->get_attributes(&pAtt)) && pAtt != NULL) {
		hr = pAtt->reset();
		for (int i = 0; i < pAtt->Getlength(); i++) {
			MSXML::IXMLDOMNodePtr pID = NULL;

			hr = pAtt->get_item(i, &pID);
			hr = pID->get_nodeName(&bstr);
			if (wcscmp(bstr, L"type") == 0) {
				hr = pID->get_text(&bstr);
				//_config->driver_subtype = string(W2CA(bstr));
				_config->SetDriverSubType(string(W2CA(bstr)));
			}

		}
	}

	return true;
}

static bool ParseRom(MSXML::IXMLDOMNodePtr _pNode, ssDriverConfig *_config)
{
	USES_CONVERSION;
	HRESULT hr;
	BSTR bstr;

	ssDriverConfig::ssRom rom;
	rom.offset = 0;

	hr = _pNode->get_text(&bstr);
	rom.filename = string(W2CA(bstr));

	MSXML::IXMLDOMNamedNodeMapPtr pAtt = NULL;

	if (SUCCEEDED(_pNode->get_attributes(&pAtt)) && pAtt != NULL) {
		hr = pAtt->reset();
		for (int i = 0; i < pAtt->Getlength(); i++) {
			MSXML::IXMLDOMNodePtr pID = NULL;

			hr = pAtt->get_item(i, &pID);
			hr = pID->get_nodeName(&bstr);
			if (wcscmp(bstr, L"type") == 0) {
				hr = pID->get_text(&bstr);
				rom.type = string(W2CA(bstr));
			} else if (wcscmp(bstr, L"offset") == 0) {
				hr = pID->get_text(&bstr);
				rom.offset = strtoul(W2CA(bstr), NULL, 0);
			}

		}
	}

	_config->romlist.push_back(rom);

	return true;
}

static bool ParseRoms(MSXML::IXMLDOMNodePtr _pNode, ssDriverConfig *_config)
{
	USES_CONVERSION;
	HRESULT hr;
	BSTR bstr;

	if (_pNode->hasChildNodes()) {
		MSXML::IXMLDOMNodeListPtr pNodeList = NULL;

		hr = _pNode->get_childNodes(&pNodeList);
		for (int i = 0; i < pNodeList->Getlength(); i++) {
			MSXML::IXMLDOMNodePtr pNode = NULL;

			hr = pNodeList->get_item(i, &pNode);
			hr = pNode->get_nodeTypeString(&bstr);
			if (wcscmp(bstr, L"element") == 0) {
				hr = pNode->get_nodeName(&bstr);
				if (wcscmp(bstr, L"rom") == 0) {
					ParseRom(pNode, _config);
				}
			}

		}

	}
	return true;
}

static bool ParseRomList(MSXML::IXMLDOMNodePtr _pNode, ssDriverConfig *_config)
{
	USES_CONVERSION;
	HRESULT hr;
	BSTR bstr;

	MSXML::IXMLDOMNamedNodeMapPtr pAtt = NULL;

	ParseRoms(_pNode, _config);

	if (SUCCEEDED(_pNode->get_attributes(&pAtt)) && pAtt != NULL) {
		hr = pAtt->reset();
		for (int i = 0; i < pAtt->Getlength(); i++) {
			MSXML::IXMLDOMNodePtr pID = NULL;

			hr = pAtt->get_item(i, &pID);
			hr = pID->get_nodeName(&bstr);
			if (wcscmp(bstr, L"archive") == 0) {
				hr = pID->get_text(&bstr);
				_config->archive = string(W2CA(bstr));
			}

		}
	}

	return true;
}


static bool ParseOption(MSXML::IXMLDOMNodePtr _pNode, ssIfDriverConfig *_config)
{
	USES_CONVERSION;
	HRESULT hr;
	BSTR bstr;

	MSXML::IXMLDOMNamedNodeMapPtr pAtt = NULL;

	if (SUCCEEDED(_pNode->get_attributes(&pAtt)) && pAtt != NULL) {
		string name;
		int value;
		hr = pAtt->reset();
		for (int i = 0; i < pAtt->Getlength(); i++) {
			MSXML::IXMLDOMNodePtr pID = NULL;

			hr = pAtt->get_item(i, &pID);
			hr = pID->get_nodeName(&bstr);
			if (wcscmp(bstr, L"name") == 0) {
				hr = pID->get_text(&bstr);
				name = string(W2CA(bstr));
			} else if (wcscmp(bstr, L"value") == 0) {
				hr = pID->get_text(&bstr);
				value = strtoul(W2CA(bstr), NULL, 0);
			}

		}

		//_config->option[name] = value;
		_config->AddOption(name, value);
	}

	return true;
}

static bool ParseOptions(MSXML::IXMLDOMNodePtr _pNode, ssIfDriverConfig *_config)
{
	USES_CONVERSION;
	HRESULT hr;
	BSTR bstr;

	if (_pNode->hasChildNodes()) {
		MSXML::IXMLDOMNodeListPtr pNodeList = NULL;

		hr = _pNode->get_childNodes(&pNodeList);
		for (int i = 0; i < pNodeList->Getlength(); i++) {
			MSXML::IXMLDOMNodePtr pNode = NULL;

			hr = pNodeList->get_item(i, &pNode);
			hr = pNode->get_nodeTypeString(&bstr);
			if (wcscmp(bstr, L"element") == 0) {
				hr = pNode->get_nodeName(&bstr);
				if (wcscmp(bstr, L"option") == 0) {
					ParseOption(pNode, _config);
				}
			}

		}

	}

	return true;
}

static bool ParseTitle(MSXML::IXMLDOMNodePtr _pNode, ssDriverConfig *_config)
{
	USES_CONVERSION;
	HRESULT hr;
	BSTR bstr;

	ssDriverConfig::ssTitle title;
	title.code = 0;

	hr = _pNode->get_text(&bstr);
	title.name = string(W2CA(bstr));

	MSXML::IXMLDOMNamedNodeMapPtr pAtt = NULL;

	if (SUCCEEDED(_pNode->get_attributes(&pAtt)) && pAtt != NULL) {
		hr = pAtt->reset();
		for (int i = 0; i < pAtt->Getlength(); i++) {
			MSXML::IXMLDOMNodePtr pID = NULL;

			hr = pAtt->get_item(i, &pID);
			hr = pID->get_nodeName(&bstr);
			if (wcscmp(bstr, L"code") == 0) {
				hr = pID->get_text(&bstr);
				title.code = strtoul(W2CA(bstr), NULL, 0);
			}

		}
	}

	_config->titlelist.push_back(title);

	return true;
}

static bool ParseTitleList(MSXML::IXMLDOMNodePtr _pNode, ssDriverConfig *_config)
{
	USES_CONVERSION;
	HRESULT hr;
	BSTR bstr;

	if (_pNode->hasChildNodes()) {
		MSXML::IXMLDOMNodeListPtr pNodeList = NULL;

		hr = _pNode->get_childNodes(&pNodeList);
		for (int i = 0; i < pNodeList->Getlength(); i++) {
			MSXML::IXMLDOMNodePtr pNode = NULL;

			hr = pNodeList->get_item(i, &pNode);
			hr = pNode->get_nodeTypeString(&bstr);
			if (wcscmp(bstr, L"element") == 0) {
				hr = pNode->get_nodeName(&bstr);
				if (wcscmp(bstr, L"title") == 0) {
					ParseTitle(pNode, _config);
				}
			}

		}

	}

	return true;
}

static bool ParseGame(MSXML::IXMLDOMNodePtr _pGame, ssDriverConfig *_config)
{
	HRESULT hr;
	BSTR bstr;

	if (_pGame->hasChildNodes()) {
		MSXML::IXMLDOMNodeListPtr pNodeList = NULL;

		hr = _pGame->get_childNodes(&pNodeList);
		for (int i = 0; i < pNodeList->Getlength(); i++) {
			MSXML::IXMLDOMNodePtr pNode = NULL;

			hr = pNodeList->get_item(i, &pNode);
			hr = pNode->get_nodeTypeString(&bstr);
			if (wcscmp(bstr, L"element") == 0) {
				hr = pNode->get_nodeName(&bstr);
				if (wcscmp(bstr, L"name") == 0) {
					ParseName(pNode, _config);
				} else if (wcscmp(bstr, L"driver") == 0) {
					ParseDriver(pNode, _config);
				} else if (wcscmp(bstr, L"options") == 0) {
					ParseOptions(pNode, _config);
				} else if (wcscmp(bstr, L"romlist") == 0) {
					ParseRomList(pNode, _config);
				} else if (wcscmp(bstr, L"titlelist") == 0) {
					ParseTitleList(pNode, _config);
				}
			}

		}

	}

	return true;
}

static bool ParseExt(MSXML::IXMLDOMNodePtr _pNode, ssBindConfig *_config)
{
	USES_CONVERSION;
	HRESULT hr;
	BSTR bstr;

	hr = _pNode->get_text(&bstr);
	_config->AddExt(string(W2CA(bstr)));

	return true;
}

static bool ParseExts(MSXML::IXMLDOMNodePtr _pNode, ssBindConfig *_config)
{
	USES_CONVERSION;
	HRESULT hr;
	BSTR bstr;

	if (_pNode->hasChildNodes()) {
		MSXML::IXMLDOMNodeListPtr pNodeList = NULL;

		hr = _pNode->get_childNodes(&pNodeList);
		for (int i = 0; i < pNodeList->Getlength(); i++) {
			MSXML::IXMLDOMNodePtr pNode = NULL;

			hr = pNodeList->get_item(i, &pNode);
			hr = pNode->get_nodeTypeString(&bstr);
			if (wcscmp(bstr, L"element") == 0) {
				hr = pNode->get_nodeName(&bstr);
				if (wcscmp(bstr, L"ext") == 0) {
					ParseExt(pNode, _config);
				}
			}

		}

	}

	return true;
}

static bool ParseBind(MSXML::IXMLDOMNodePtr _pBind, ssBindConfig *_config)
{
	HRESULT hr;
	BSTR bstr;

	if (_pBind->hasChildNodes()) {
		MSXML::IXMLDOMNodeListPtr pNodeList = NULL;

		hr = _pBind->get_childNodes(&pNodeList);
		for (int i = 0; i < pNodeList->Getlength(); i++) {
			MSXML::IXMLDOMNodePtr pNode = NULL;

			hr = pNodeList->get_item(i, &pNode);
			hr = pNode->get_nodeTypeString(&bstr);
			if (wcscmp(bstr, L"element") == 0) {
				hr = pNode->get_nodeName(&bstr);
				if (wcscmp(bstr, L"exts") == 0) {
					ParseExts(pNode, _config);
				} else if (wcscmp(bstr, L"driver") == 0) {
					ParseDriver(pNode, _config);
				} else if (wcscmp(bstr, L"options") == 0) {
					ParseOptions(pNode, _config);
				}
			}

		}

	}

	return true;
}

bool ssConfigLoader::Load(const string &_fname,
						  vector<ssIfFolder *> &_configlist)
{
	HRESULT hr;
	BSTR bstr;

	MSXML::IXMLDOMDocumentPtr pDoc = NULL;
	MSXML::IXMLDOMElementPtr pRoot = NULL;

	CoInitialize(NULL);

	hr = CoCreateInstance (
		MSXML::CLSID_DOMDocument, NULL, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER,
		MSXML::IID_IXMLDOMDocument, (LPVOID *)&pDoc);

	hr = pDoc->put_async(VARIANT_FALSE);

	bstr = T2BSTR(_fname.c_str());
	hr = pDoc->load(bstr);
	hr = pDoc->get_documentElement(&pRoot);

	hr = pRoot->get_nodeTypeString(&bstr);
	if (wcscmp(bstr, L"element") == 0) {
		pRoot->get_nodeName(&bstr);
		if (wcscmp(bstr, L"gamelist") == 0 && pRoot->hasChildNodes()) {
			MSXML::IXMLDOMNodeListPtr pGameList = NULL;

			pRoot->get_childNodes(&pGameList);
			for (int i = 0; i < pGameList->Getlength(); i++) {
				MSXML::IXMLDOMNodePtr pNode = NULL;

				pGameList->get_item(i, &pNode);
				hr = pNode->get_nodeTypeString(&bstr);

				if (wcscmp(bstr, L"element") == 0) {
					hr = pNode->get_nodeName(&bstr);

					if (wcscmp(bstr, L"game") == 0) {
						ssDriverConfig *config = new ssDriverConfig;

						ParseGame(pNode, config);

						if (ssUnZip::IsExist(config->archive)) {
							_configlist.push_back(config);
						} else {
							delete config;
						}
					} else if (wcscmp(bstr, L"bind") == 0) {
						ssBindConfig *config = new ssBindConfig;

						ParseBind(pNode, config);

						_configlist.push_back(config);
					}
				}

			}

		}
	}

	return true;
}

static void DeleteEntry(ssIfFolder *_config)
{
	delete _config;
}

bool ssConfigLoader::Free(vector<ssIfFolder *> &_configlist)
{
	for_each(_configlist.begin(), _configlist.end(), DeleteEntry);
	_configlist.clear();
	return true;
}

#ifdef _DEBUG
static void DumpEntry(ssIfFolder *_config)
{
	ssDriverConfig *config = dynamic_cast<ssDriverConfig *>(_config);
	if (config != NULL) {
		TRACE("--------------------------------\n");
		TRACE("name    : %s\n", config->name.c_str());
		TRACE("driver  : %s (%s)\n", config->driver_majortype.c_str(), config->driver_subtype.c_str());
		TRACE("archive : %s\n", config->archive.c_str());
		ssDriverConfig::ssOption::const_iterator o;
		for (o = config->option.begin(); o != config->option.end(); o++) {
			TRACE("option  : %s = %d\n", o->first.c_str(), o->second);
		}
		ssDriverConfig::ssRomList::const_iterator r;
		for (r = config->romlist.begin(); r != config->romlist.end(); r++) {
			TRACE("rom     : %s (%s : %08x)\n", r->filename.c_str(), r->type.c_str(), r->offset);
		}
		ssDriverConfig::ssTitleList::const_iterator t;
		for (t = config->titlelist.begin(); t != config->titlelist.end(); t++) {
			TRACE("title   : [%02x] %s\n", t->code, t->name.c_str());
		}
		TRACE("\n");
	}
}
#endif

bool ssConfigLoader::Dump(vector<ssIfFolder *> &_configlist)
{
#ifdef __DEBUG
	for_each(_configlist.begin(), _configlist.end(), DumpEntry);
#endif
	return true;
}

