#include "DaeValidator.h"
#include "PathUtil.h"
#include "Strings.h"
#include "StringUtil.h"
#include <iostream>
#include <set>
#include <sstream>

using namespace std;

namespace std
{
	template<>
	struct less<tuple<size_t, string>>
	{
		bool operator () (const tuple<size_t, string>& a, const tuple<size_t, string>& b) const
		{
			return get<1>(a) < get<1>(b);
		}
	};
}

namespace opencollada
{
	extern const char* colladaNamespace141;
	extern const char* colladaSchemaFileName141;
	extern XmlSchema colladaSchema141;

	extern const char* colladaNamespace15;
	extern const char* colladaSchemaFileName15;
	extern XmlSchema colladaSchema15;

	DaeValidator::DaeValidator(const list<string> & daePaths)
	{
		mDaePaths.reserve(daePaths.size());
		mDaePaths.insert(mDaePaths.end(), daePaths.begin(), daePaths.end());
	}

	int DaeValidator::for_each_dae(const function<int(const Dae &)> & task) const
	{
		int result = 0;
		size_t count = 1;
		for (const auto & daePath : mDaePaths)
		{
			if (mDaePaths.size() > 1)
			{
				cout << "[" << count << "/" << mDaePaths.size() << " " << static_cast<size_t>(static_cast<float>(count) / static_cast<float>(mDaePaths.size()) * 100.0f) << "%]" << endl;
				++count;

				cout << "Processing " << daePath << endl;
			}

			Dae dae;
			dae.readFile(daePath);
			if (dae)
			{
				result |= task(dae);
			}
			else
			{
				result |= 1;
				cerr << "Error loading " << daePath << endl;
			}
		}
		return result;
	}

	int DaeValidator::checkAll()
	{
		return for_each_dae([&](const Dae & dae) {
			return checkAll(dae);
		});
	}

	int DaeValidator::checkAll(const Dae & dae)
	{
		return
			checkSchema(dae) |
			checkUniqueIds(dae) |
			checkLinks(dae);
	}

	int DaeValidator::checkSchema(const string & schema_uri)
	{
		if (schema_uri.empty())
		{
			return for_each_dae([&](const Dae & dae) {
				return checkSchema(dae);
			});
		}

		XmlSchema schema;
		schema.readFile(schema_uri);
		if (schema)
		{
			return for_each_dae([&](const Dae & dae) {
				return ValidateAgainstSchema(dae, schema);
			});
		}

		cerr << "Error loading " << schema_uri << endl;
		return 1;
	}

	int DaeValidator::checkSchema(const Dae & dae)
	{
		cout << "Checking schema..." << endl;

		int result = 0;

		// Get root <COLLADA> element
		auto collada = dae.root();
		if (!collada)
		{
			cerr << "Can't find document root" << endl;
			return 1;
		}

		if (collada.name() != Strings::COLLADA)
		{
			cerr << "Root element is not <" << Strings::COLLADA << ">" << endl;
			return 1;
		}

		// Get COLLADA namespace
		auto xmlns = collada.ns();
		if (!xmlns)
		{
			cerr << "COLLADA element has no namespace" << endl;
			return 1;
		}

		// Determine COLLADA version used by input dae file
		auto href = xmlns.href();
		if (href == colladaNamespace141)
		{
			result |= ValidateAgainstSchema(dae, colladaSchema141);
		}
		else if (href == colladaNamespace15)
		{
			result |= ValidateAgainstSchema(dae, colladaSchema15);
		}
		else
		{
			cerr << "Can't determine COLLADA version used by input file" << endl;
			return 1;
		}

		vector<XmlNode> subDocs;

		// Find xsi:schemaLocation attributes in dae and try to validate against specified xsd documents
		const auto & elements = dae.root().selectNodes("//*[@xsi:schemaLocation]");
		for (const auto & element : elements)
		{
			if (auto schemaLocation = element.attribute("schemaLocation"))
			{
				vector<string> parts = String::Split(schemaLocation.value());
				// Parse pairs of namespace/xsd
				for (size_t i = 1; i < parts.size(); i += 2)
				{
					const string & ns = parts[i - 1];
					const string & xsdUri = parts[i];

					if (ns != colladaNamespace141 && ns != colladaNamespace15)
					{
						// "insert" does nothing if element already exists.
						mSchemas.insert(pair<string, XmlSchema>(ns, XmlSchema()));
						mSchemaLocations.insert(pair<string, string>(ns, xsdUri));
					}
				}
			}
		}

		// Preload uninitialized .xsd files
		auto itSchema = mSchemas.begin();
		auto itSchemaLocation = mSchemaLocations.begin();
		for (; itSchema != mSchemas.end(); ++itSchema, ++itSchemaLocation)
		{
			const auto & schemaUri = itSchemaLocation->second;
			auto & schema = itSchema->second;

			// Don't try to load schemas that already failed in a previous run
			if (schema.failedToLoad())
				continue;

			string uri = schemaUri;

			if (!schema)
			{
				schema.readFile(uri);
			}

			if (!schema)
			{
				// Try to find schema document in executable directory
				Uri xsdUri(schemaUri);
				if (xsdUri.isValid())
				{
					uri = Path::Join(Path::GetExecutableDirectory(), xsdUri.pathFile());
					schema.readFile(uri);
				}
			}

			if (!schema)
			{
				// Try to find schema document in COLLADA document directory
				Uri xsdUri(schemaUri);
				string xsdFile = xsdUri.pathFile();
				xsdUri = dae.getURI();
				xsdUri.setPathFile(xsdFile);
				uri = xsdUri.str();
				schema.readFile(uri);
			}

			if (schema && uri != schemaUri)
			{
				cout << "Using " << uri << endl;
			}
			else if (!schema)
			{
				cerr << "Error loading " << schemaUri << endl;
				result |= 1;
			}
		}

		// Validate "sub documents"
		for (const auto & schema : mSchemas)
		{
			// Ignore schemas that failed to load
			if (!schema.second)
				continue;

			const auto & ns = schema.first;
			stringstream xpath;
			xpath << "//*[namespace-uri()='" << ns << "' and not(namespace-uri(./..)='" << ns << "')]";
			const auto & nodes = dae.root().selectNodes(xpath.str());
			for (auto node : nodes)
			{
				auto old = dae.setRoot(node);
				result |= ValidateAgainstSchema(dae, schema.second);
				dae.setRoot(old);
			}
		}

		return result;
	}

	int DaeValidator::checkUniqueIds()
	{
		return for_each_dae([&](const Dae & dae) {
			return checkUniqueIds(dae);
		});
	}

	int DaeValidator::checkUniqueIds(const Dae & dae)
	{
		cout << "Checking unique ids..." << endl;

		int result = 0;
		map<string, size_t> ids;
		const auto & nodes = dae.root().selectNodes("//*[@id]");
		for (const auto & node : nodes)
		{
			string id = node.attribute("id").value();
			size_t line = node.line();

			int checkEscapeCharResult = CheckEscapeChar(id);
			if (checkEscapeCharResult != 0)
			{
				cerr << dae.getURI() << ":" << line << ": \"" << id << "\" contains non-escaped characters." << endl;
				result |= checkEscapeCharResult;
			}

			auto it = ids.find(id);
			if (it != ids.end())
			{
				cerr << dae.getURI() << ":" << line << ": Duplicated id \"" << id << "\". See first declaration at line " << it->second << "." << endl;
				result |= 1;
			}
			else
			{
				ids[id] = line;
			}
		}
		return result;
	}

	int DaeValidator::checkLinks()
	{
		return for_each_dae([&](const Dae & dae) {
			return checkLinks(dae);
		});
	}

	int DaeValidator::checkLinks(const Dae & dae)
	{
		cout << "Checking links..." << endl;

		const auto & ids = dae.getIds();

		int result = 0;
		for (const auto & t : dae.getAnyURIs())
		{
			const auto & line = get<0>(t);
			const auto & uri = get<1>(t);
			if (!Path::Exists(uri.nativePath()))
			{
				cerr << dae.getURI() << ":" << line << ": Can't resolve " << uri << endl;
				result |= 1;
			}
			else if (!uri.fragment().empty())
			{
				Uri no_fragment_uri(uri);
				no_fragment_uri.setFragment("");
				if (no_fragment_uri == dae.getURI())
				{
					auto id = ids.find(uri.fragment());
					if (id == ids.end())
					{
						cerr << dae.getURI() << ":" << line << ": Can't resolve #" << uri.fragment() << endl;
						result |= 1;
					}
				}
				else
				{
					auto it = dae.getExternalDAEs().find(no_fragment_uri);
					if (it != dae.getExternalDAEs().end())
					{
						auto ext_ids = it->second.getIds();
						auto id = ext_ids.find(uri.fragment());
						if (id == ext_ids.end())
						{
							cerr << dae.getURI() << ":" << line << ": Can't resolve " << uri << endl;
							result |= 1;
						}
					}
					else
					{
						cerr << dae.getURI() << ":" << line << ": " << uri << ": referenced file exists but has not been successfully loaded." << endl;
						result |= 1;
					}
				}
			}
		}

		// IDREF
		for (const auto & IDREF : dae.getIDREFs())
		{
			const auto & line = get<0>(IDREF);
			const auto & idref = get<1>(IDREF);

			auto id = ids.find(idref);
			if (id == ids.end())
			{
				cerr << dae.getURI() << ":" << line << ": Can't resolve #" << idref << endl;
				result |= 1;
			}
		}

		return result;
	}

	int DaeValidator::ValidateAgainstSchema(const Dae & dae, const XmlSchema & schema)
	{
		return schema.validate(dae) ? 0 : 1;
	}

	int DaeValidator::CheckEscapeChar(const std::string & s)
	{
		if (s.find_first_of(" #$%&/:;<=>?@[\\:]^`{|}~") != string::npos)
			return 1;
		return 0;
	}
}
