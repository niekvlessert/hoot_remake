#include "StdAfx.h"
#include "ssSoundDriver.h"
#include "ssDriverDescription.h"
#include "ssDriverBinder.h"

// ドライバを管理、生成するクラス

#pragma warning (disable:4503)

#import "MSXML.DLL" named_guids

#define SAFERELEASE(p) {if (p) {(p)->Release(); p = NULL;}}

ssDriverBinder *ssDriverBinder::m_Instance = NULL;

ssDriverBinder *ssDriverBinder::Instance(void)
{
	if (m_Instance == NULL) {
		m_Instance = new ssDriverBinder();
	}
	return m_Instance;
}

void ssDriverBinder::CleanUp()
{
	if (m_Instance != NULL) {
		delete m_Instance;
		m_Instance = NULL;
	}
}

ssDriverBinder::ssDriverBinder()
{
	atexit(CleanUp);
}

bool ssDriverBinder::Register(const string &_major, ssDriverFactoryFunction _func)
{
	TRACE("Register: %s\n", _major.c_str());

	m_Map.insert(make_pair(_major, _func));

	return true;
}

bool ssDriverBinder::RegisterDescription(const ssDriverDescription *_desc)
{
	m_DescMap[string(_desc->name)][string(_desc->type)] = _desc;

	return true;
}

static void DumpDescOptions(MSXML::IXMLDOMNodePtr subtype,
							const ssDriverDescription::Options *doptions)
{
	if (doptions[0].name != 0) {
		// options
		MSXML::IXMLDOMDocumentPtr doc = subtype->ownerDocument;
		MSXML::IXMLDOMTextPtr text;

		MSXML::IXMLDOMNodePtr options = doc->createElement("options");
		subtype->appendChild(options);
		for (int i = 0; i < ssDriverDescription::MAX_OPTIONS; i++) {
			const ssDriverDescription::Options *doption = &doptions[i];
			if (doption->name == 0) break;

			MSXML::IXMLDOMNodePtr option = doc->createElement("option");
			options->appendChild(option);

			MSXML::IXMLDOMNodePtr name = doc->createElement("name");
			option->appendChild(name);

			text = doc->createTextNode(doption->name);
			name->appendChild(text);

			MSXML::IXMLDOMNodePtr value = doc->createElement("value");
			option->appendChild(value);

			char str[32];
			sprintf(str, "0x%x(%d)", doption->value, doption->value);
			text = doc->createTextNode(str);
			value->appendChild(text);

			if (doption->descriptions[0] != 0) {
				// descriptions
				MSXML::IXMLDOMNodePtr descriptions = doc->createElement("descriptions");
				option->appendChild(descriptions);
				for (int j = 0; j < ssDriverDescription::MAX_OPTION_DESCRIPTION; j++) {
					if (doption->descriptions[j] == 0) break;

					MSXML::IXMLDOMNodePtr description = doc->createElement("description");
					descriptions->appendChild(description);
					text = doc->createTextNode(doption->descriptions[j]);
					description->appendChild(text);
				}
			}
		}
	}
}

static void DumpDescFiles(MSXML::IXMLDOMNodePtr subtype,
						  const ssDriverDescription::Files *dfiles)
{
	if (dfiles[0].name != 0) {
		// files
		MSXML::IXMLDOMDocumentPtr doc = subtype->ownerDocument;
		MSXML::IXMLDOMTextPtr text;

		MSXML::IXMLDOMNodePtr files = doc->createElement("files");
		subtype->appendChild(files);
		for (int i = 0; i < ssDriverDescription::MAX_FILES; i++) {
			const ssDriverDescription::Files *dfile = &dfiles[i];
			if (dfile->name == 0) break;

			MSXML::IXMLDOMNodePtr file = doc->createElement("file");
			files->appendChild(file);

			MSXML::IXMLDOMNodePtr name = doc->createElement("name");
			file->appendChild(name);

			text = doc->createTextNode(dfile->name);
			name->appendChild(text);

			if (dfile->descriptions[0] != 0) {
				// descriptions
				MSXML::IXMLDOMNodePtr descriptions = doc->createElement("descriptions");
				file->appendChild(descriptions);
				for (int j = 0; j < ssDriverDescription::MAX_FILE_DESCRIPTION; j++) {
					if (dfile->descriptions[j] == 0) break;


					MSXML::IXMLDOMNodePtr description = doc->createElement("description");
					descriptions->appendChild(description);
					text = doc->createTextNode(dfile->descriptions[j]);
					description->appendChild(text);
				}
			}
		}
	}
}

static void DumpDesc(MSXML::IXMLDOMNodePtr subtypes,
					 const ssDriverDescription *desc)
{
	MSXML::IXMLDOMDocumentPtr doc = subtypes->ownerDocument;
	MSXML::IXMLDOMTextPtr text;

	MSXML::IXMLDOMNodePtr subtype = doc->createElement("subtype");
	subtypes->appendChild(subtype);

	MSXML::IXMLDOMNodePtr type_name = doc->createElement("name");
	text = doc->createTextNode(desc->type);
	type_name->appendChild(text);
	subtype->appendChild(type_name);

	if (desc->descriptions[0] != 0) {
		// descriptions
		MSXML::IXMLDOMNodePtr descriptions = doc->createElement("descriptions");
		subtype->appendChild(descriptions);
		for (int i = 0; i < ssDriverDescription::MAX_DRIVER_DESCRIPTION; i++) {
			if (desc->descriptions[i] == 0) break;

			MSXML::IXMLDOMNodePtr description = doc->createElement("description");
			descriptions->appendChild(description);
			text = doc->createTextNode(desc->descriptions[i]);
			description->appendChild(text);
		}
	}

	::DumpDescFiles(subtype, desc->files);
	::DumpDescOptions(subtype, desc->options);
}

bool ssDriverBinder::DumpDescriptionSub(const string &_fname)
{
	bool ret = false;

	TRACE("DumpDescription start\n");

	CoInitialize(NULL);

	MSXML::IXMLDOMDocumentPtr doc("MSXML.DOMDocument");
	doc->appendChild(
		doc->createProcessingInstruction(
			"xml", "version='1.0' encoding='Shift_JIS'"));
	doc->appendChild(
		doc->createProcessingInstruction(
			"xml-stylesheet", "type='text/xsl' href='drivers.xsl'"));
	MSXML::IXMLDOMNodePtr drivers = doc->createElement("drivers");
	doc->appendChild(drivers);

	MSXML::IXMLDOMTextPtr text;

	ssDescMapName::const_iterator ni;
	for (ni = m_DescMap.begin(); ni != m_DescMap.end(); ni++) {
		string name = ni->first;

		MSXML::IXMLDOMNodePtr driver = doc->createElement("driver");
		drivers->appendChild(driver);

		MSXML::IXMLDOMNodePtr driver_name = doc->createElement("name");
		driver->appendChild(driver_name);

		text = doc->createTextNode(name.c_str());
		driver_name->appendChild(text);

		MSXML::IXMLDOMNodePtr subtypes = doc->createElement("subtypes");
		driver->appendChild(subtypes);

		ssDescMapType typemap = ni->second;
		ssDescMapType::const_iterator ti;
		for (ti = typemap.begin(); ti != typemap.end(); ti++) {
			string type = ti->first;
			const ssDriverDescription *desc = ti->second;
			TRACE("%s/%s\n", desc->name, desc->type);

			::DumpDesc(subtypes, desc);
		}
	}

	doc->save(_fname.c_str());

	TRACE("DumpDescription end\n");

	return ret;
}

bool ssDriverBinder::DumpDescription(const string &_fname)
{
	ssDriverBinder *binder = ssDriverBinder::Instance();

	return binder->DumpDescriptionSub(_fname);
}

ssSoundDriver *ssDriverBinder::CreateDriver(const ssDriverConfig *_config) const
{
	typedef ssDriverMap::const_iterator I;
	ssSoundDriver *driver = NULL;

	string major = _config->GetDriverType();

	pair<I, I> b = m_Map.equal_range(major);
	for (I i = b.first; i != b.second; i++) {
		driver = (i->second)(_config);
		if (driver != NULL) {
			break;
		}
	}

	return driver;
}
