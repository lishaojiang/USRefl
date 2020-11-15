#include "TypeInfoGenerator.h"

#include <sstream>

using namespace Ubpa::USRefl;
using namespace std;

string TypeInfoGenerator::Generate(const vector<TypeMeta>& typeMetas) {
	stringstream ss;

	constexpr auto indent = "    ";

	ss
		<< "// This file is generated by Ubpa::USRefl::AutoRefl" << endl
		<< endl
		<< "#pragma once" << endl
		<< endl
		<< "#include <USRefl/USRefl.h>" << endl
		<< endl;

	for (const auto& typeMeta : typeMetas) {
		const auto fullname = typeMeta.GenerateFullName();
		const std::string nsname = typeMeta.GenerateNsName();
		const std::string tname = typeMeta.IsTemplateType() ? fullname : "Type";
		ss
			<< "template<" << typeMeta.GenerateTemplateList() << ">" << endl
			<< "struct Ubpa::USRefl::TypeInfo<" << fullname << "> :" << endl;
		
		ss << indent << "TypeInfoBase<" << fullname;
		auto publicBaseIndice = typeMeta.GetPublicBaseIndices();
		if(!publicBaseIndice.empty()) {
			if (publicBaseIndice.size() > 1) {
				ss << "," << endl;
				for (size_t i = 0; i < publicBaseIndice.size(); i++) {
					const auto& base = typeMeta.bases[publicBaseIndice[i]];
					ss << indent << indent << base.GenerateText();
					if (i != publicBaseIndice.size() - 1)
						ss << ",";
					ss << endl;
				}
				ss << indent;
			}
			else
				ss << ", " << typeMeta.bases.front().GenerateText();
		}
		
		ss
			<< ">" << endl
			<< "{" << endl;

		// name
		ss << "#ifdef UBPA_USREFL_NOT_USE_NAMEOF" << endl;

		if (typeMeta.IsTemplateType())
			ss << indent << "// [!] all instance types have the same name" << endl;

		
		ss
			<< indent << "static constexpr char name[" << (nsname.size() + 1) << "] = \"" << nsname << "\";" << endl
			<< "#endif" << endl;
		
		// attributes
		switch (config.attrListConstMode) {
		case ConstMode::Constepxr:
			ss << indent << "static constexpr AttrList attrs = {";
			break;
		case ConstMode::Const:
			ss << indent << "inline static const AttrList attrs = {";
			break;
		case ConstMode::NonConst:
			ss << indent << "inline static AttrList attrs = {";
			break;
		}
		if (!typeMeta.attrs.empty()) {
			ss << endl;
			for (const auto& attr : typeMeta.attrs) {
				auto name = attr.GenerateName(
					attr.ns.empty() ?
					config.nonNamespaceNameWithoutQuotation
					: !config.namespaceNameWithQuotation
				);
				ss << indent << indent << "Attr {" << name;
				if (!attr.value.empty())
					ss << ", " << attr.GenerateValue(config.isAttrValueToFunction);
				ss << "}," << endl;
			}
			ss << indent;
		}
		ss << "};" << endl;
		
		// fields
		switch (config.attrListConstMode) {
		case ConstMode::Constepxr:
			ss << indent << "static constexpr FieldList fields = {";
			break;
		case ConstMode::Const:
			ss << indent << "inline static const FieldList fields = {";
			break;
		case ConstMode::NonConst:
			ss << indent << "inline static FieldList fields = {";
			break;
		}
		if(typeMeta.HaveAnyOutputField()) {
			ss << endl;
			for (const auto& field : typeMeta.fields) {
				if (field.isTemplate
					|| field.accessSpecifier != AccessSpecifier::PUBLIC
					|| field.IsFriendFunction()
					|| field.IsDeletedFunction()
				)
					continue;
				
				ss << indent << indent << "Field {";
				
				// [name]
				Attr attr;
				if (field.name == typeMeta.name) {
					// constructor
					attr.ns = config.nameof_namespace;
					attr.name = config.nameof_constructor;
				}
				else if (field.name == ("~" + typeMeta.name)) {
					// destructor
					attr.ns = config.nameof_namespace;
					attr.name = config.nameof_destructor;
				}
				else
					attr.name = field.name;
				ss
					<< attr.GenerateName(
						attr.ns.empty() ?
						config.nonNamespaceNameWithoutQuotation
						: !config.namespaceNameWithQuotation
					) << ", ";
				
				// [value]
				switch (field.mode) {
				case Field::Mode::Variable: {
					ss << "&" << tname << "::" << field.name;
					break;
				}
				case Field::Mode::Function: {
					if (field.name == typeMeta.name) {
						// constructor
						ss << "WrapConstructor<" << tname << "(" << field.GenerateParamTypeList() << ")>()";
					}
					else if(field.name == ("~" + typeMeta.name)) {
						// destructor
						ss << "WrapDestructor<" << tname << ">()";
					}
					else if (typeMeta.IsOverloaded(field.name))
						ss << "static_cast<" << field.GenerateFunctionType(tname) << ">(&" << tname << "::" << field.name << ")";
					else
						ss << "&" << tname << "::" << field.name;
					break;
				}
				case Field::Mode::Value: {
					ss << tname << "::" << field.name;
					break;
				}
				}
				// attributes
				const bool hasInitializerAttr = config.isInitializerAsAttr
					&& field.mode != Field::Mode::Value && !field.initializer.empty();
				const bool hasDefaultFunctionsAttr = config.generateDefaultFunctions
					&& field.mode == Field::Mode::Function && field.GetDefaultParameterNum() > 0;
				if (!field.attrs.empty() || hasInitializerAttr || hasDefaultFunctionsAttr) {
					ss
						<< ", AttrList {" << endl;
					// [initializer]
					if (hasInitializerAttr) {
						Attr attr;
						attr.ns = config.nameof_namespace;
						attr.name = config.nameof_initializer;
						attr.value = field.initializer;
						auto name = attr.GenerateName(
							attr.ns.empty() ?
							config.nonNamespaceNameWithoutQuotation
							: !config.namespaceNameWithQuotation
						);
						ss
							<< indent << indent << indent
							<< "Attr {" << name << ", "
							<< attr.GenerateValue(field.GenerateSimpleFieldType()) << "}," << endl;
					}
					// [default functions]
					if (hasDefaultFunctionsAttr) {
						Attr attr;
						attr.ns = config.nameof_namespace;
						attr.name = config.nameof_default_functions;
						auto name = attr.GenerateName(
							attr.ns.empty() ?
							config.nonNamespaceNameWithoutQuotation
							: !config.namespaceNameWithQuotation
						);
						ss
							<< indent << indent << indent
							<< "Attr {" << name << ", std::tuple {" << endl;
						const size_t defaultParameterNum = field.GetDefaultParameterNum();
						const bool isConstructor = field.name == typeMeta.name;
						for (size_t i = 1; i <= defaultParameterNum; i++) {
							size_t num = field.parameters.size() - i;
							if(isConstructor) {
								ss
									<< indent << indent << indent << indent
									<< "WrapConstructor<" << tname << "(" << field.GenerateParamTypeList(num) << ")>()";
							}
							else if (field.IsMemberFunction()) {
								auto qualifiers = field.GenerateQualifiers();
								bool isPointer = qualifiers.find('&') == std::string::npos;
								if (isPointer)
									qualifiers += "*";

								auto ftname = tname + " " + qualifiers;
								ss
									<< indent << indent << indent << indent
									<< "[](" << ftname << " __this" << (num > 0 ? ", " : "")
									<< field.GenerateNamedParameterList(num) << "){ return "
									<< (isPointer? "__this->" : ("std::forward<" + ftname + ">(__this)."))
									<< field.name << "(" << field.GenerateForwardArgumentList(num) << "); }";
							}
							else { // static
								auto qualifiers = field.GenerateQualifiers();
								bool isPointer = qualifiers.find('&') == std::string::npos;
								if (isPointer)
									qualifiers += "*";

								auto ftname = tname + " " + qualifiers;
								ss
									<< indent << indent << indent << indent
									<< "[](" << field.GenerateNamedParameterList(num) << "){ return "
									<< tname << "::" << field.name << "(" << field.GenerateForwardArgumentList(num) << "); }";
							}
							if (i != defaultParameterNum)
								ss << ",";
							ss << endl;
						}
						ss << indent << indent << indent << "}}," << endl; // attr
					}
					// [normal attrs]
					for (const auto& attr : field.attrs) {
						auto name = attr.GenerateName(
							attr.ns.empty() ?
							config.nonNamespaceNameWithoutQuotation
							: !config.namespaceNameWithQuotation
						);
						ss << indent << indent << indent
							<< "Attr {" << name;
						if (!attr.value.empty())
							ss << ", " << attr.GenerateValue(config.isAttrValueToFunction);
						ss << "}," << endl;
					}
					ss
						<< indent << indent << "}";
				}
				ss << "}," << endl;
			}
			ss << indent;
		}
		ss << "};" << endl; // end of fields
		
		ss << "};" << endl << endl; // end of TypeInfo
	}

	return ss.str();
}
