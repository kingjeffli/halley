#include <sstream>
#include "codegen_cpp.h"
#include "cpp_class_gen.h"
#include <set>
#include <halley/support/exception.h>
#include <algorithm>
#include "halley/text/string_converter.h"
#include "halley/core/game/game_platform.h"
#include "halley/tools/ecs/system_message_schema.h"
#include "halley/utils/algorithm.h"

using namespace Halley;

static String upperFirst(String name)
{
	if (name[0] >= 'a' && name[0] <= 'z') {
		name[0] -= 32;
	}
	return name;
}

static String lowerFirst(String name)
{
	if (name[0] >= 'A' && name[0] <= 'Z') {
		name[0] += 32;
	}
	return name;
}

CodeGenResult CodegenCPP::generateComponent(ComponentSchema component)
{
	const String className = component.name + "Component" + (component.customImplementation ? "Base" : "");

	CodeGenResult result;
	result.emplace_back(CodeGenFile(makePath("components", className, "h"), generateComponentHeader(component)));
	return result;
}

CodeGenResult CodegenCPP::generateSystem(SystemSchema system, const HashMap<String, ComponentSchema>& components, const HashMap<String, SystemMessageSchema>& systemMessages)
{
	const String className = system.name + "System";

	CodeGenResult result;
	result.emplace_back(CodeGenFile(makePath("systems", className, "h"), generateSystemHeader(system, components, systemMessages)));
	//result.emplace_back(CodeGenFile(makePath("../../src/systems", className, "cpp"), generateSystemStub(system), true));
	return result;
}

CodeGenResult CodegenCPP::generateMessage(MessageSchema message)
{
	const String className = message.name + "Message";

	CodeGenResult result;
	result.emplace_back(CodeGenFile(makePath("messages", className, "h"), generateMessageHeader(message, "Message")));
	return result;
}

CodeGenResult CodegenCPP::generateSystemMessage(SystemMessageSchema message)
{
	const String className = message.name + "SystemMessage";

	CodeGenResult result;
	result.emplace_back(CodeGenFile(makePath("system_messages", className, "h"), generateMessageHeader(message, "SystemMessage")));
	return result;
}

CodeGenResult CodegenCPP::generateRegistry(const Vector<ComponentSchema>& componentsRaw, const Vector<SystemSchema>& systemsRaw)
{
	auto components = componentsRaw;
	std::sort(components.begin(), components.end());
	auto systems = systemsRaw;
	std::sort(systems.begin(), systems.end());

	Vector<String> registryCpp {
		"#include <halley.hpp>",
		"using namespace Halley;",
		""
	};

	for (auto& comp: components) {
		registryCpp.emplace_back("#include \"" + getComponentFileName(comp) + "\"");
	}
	
	registryCpp.insert(registryCpp.end(), {
		"",
		"// System factory functions"
	});

	for (auto& sys: systems) {
		registryCpp.push_back("System* halleyCreate" + sys.name + "System();");
	}

	// System factory
	registryCpp.insert(registryCpp.end(), {
		"",
		"",
		"using SystemFactoryPtr = System* (*)();",
		"using SystemFactoryMap = HashMap<String, SystemFactoryPtr>;",
		"",
		"static SystemFactoryMap makeSystemFactories() {",
		"	SystemFactoryMap result;"
	});

	for (auto& sys : systems) {
		registryCpp.push_back("	result[\"" + sys.name + "System\"] = &halleyCreate" + sys.name + "System;");
	}

	registryCpp.insert(registryCpp.end(), {
		"	return result;",
		"}"
	});

	// Component factory
	registryCpp.insert(registryCpp.end(), {
		"",
		"",
		"using ComponentFactoryPtr = std::function<CreateComponentFunctionResult(EntityFactory&, EntityRef&, const ConfigNode&)>;",
		"using ComponentFactoryMap = HashMap<String, ComponentFactoryPtr>;",
		"",
		"static ComponentFactoryMap makeComponentFactories() {",
		"	ComponentFactoryMap result;"
	});

	for (auto& comp : components) {
		registryCpp.push_back("	result[\"" + comp.name + "\"] = [] (EntityFactory& factory, EntityRef& e, const ConfigNode& node) -> CreateComponentFunctionResult { return factory.createComponent<" + comp.name + "Component>(e, node); };");
	}

	registryCpp.insert(registryCpp.end(), {
		"	return result;",
		"}"
	});

	// Create system and component methods
	registryCpp.insert(registryCpp.end(), {
		"",
		"namespace Halley {",
		"	std::unique_ptr<System> createSystem(String name) {",
		"		static SystemFactoryMap factories = makeSystemFactories();",
		"		auto result = factories.find(name);",
		"		if (result == factories.end()) {",
		"			throw Exception(\"System not found: \" + name, HalleyExceptions::Entity);",
		"		}",
		"		return std::unique_ptr<System>(result->second());",
		"	}",
		"",
		"   CreateComponentFunctionResult createComponent(EntityFactory& factory, const String& name, EntityRef& entity, const ConfigNode& componentData) {",
		"		static ComponentFactoryMap factories = makeComponentFactories();",
		"		auto result = factories.find(name);",
		"		if (result == factories.end()) {",
		"			throw Exception(\"Component not found: \" + name, HalleyExceptions::Entity);",
		"		}",
		"		return result->second(factory, entity, componentData);",
		"   }",
		"}"
	});

	Vector<String> registryH{
		"#pragma once",
		"",
		"#include <halley/entity/registry.h>"
	};

	CodeGenResult result;
	result.emplace_back(CodeGenFile("registry.cpp", registryCpp));
	result.emplace_back(CodeGenFile("registry.h", registryH));
	return result;
}

Vector<String> CodegenCPP::generateComponentHeader(ComponentSchema component)
{
	Vector<String> contents = {
		"#pragma once",
		"",
		"#ifndef DONT_INCLUDE_HALLEY_HPP",
		"#include <halley.hpp>",
		"#endif"
		""
	};

	for (auto& includeFile: component.includeFiles) {
		contents.push_back("#include \"" + includeFile + "\"");
	}
	contents.push_back("");

	String deserializeBody;
	bool first = true;
	for (auto& member: component.members) {
		if (!member.serializable) {
			continue;
		}
		
		if (first) {
			first = false;
		} else {
			if constexpr (getPlatform() == GamePlatform::Windows) {
				deserializeBody += "\r\n\t\t";
			} else {
				deserializeBody += "\n\t\t";
			}
		}
		deserializeBody += "Halley::ConfigNodeHelper<decltype(" + member.name + ")>::deserialize(" + member.name + ", context, node[\"" + member.name + "\"]);";
	}

	String className = component.name + "Component" + (component.customImplementation ? "Base" : "");
	
	auto gen = CPPClassGenerator(className, "Halley::Component", MemberAccess::Public, !component.customImplementation)
		.setAccessLevel(MemberAccess::Public)
		.addMember(MemberSchema(TypeSchema("int", false, true, true), "componentIndex", toString(component.id)))
		.addBlankLine()
		.addMembers(component.members)
		.addBlankLine()
		.setAccessLevel(MemberAccess::Public)
		.addDefaultConstructor();

	// Additional constructors
	if (!component.members.empty()) {
		const auto serializableMembers = filter(component.members.begin(), component.members.end(), [] (const MemberSchema& m) { return m.serializable; });
		if (!serializableMembers.empty()) {
			gen.addBlankLine()
				.addConstructor(MemberSchema::toVariableSchema(serializableMembers), true);
		}
	}
	
	// Deserialize method
	gen.addBlankLine()
		.addMethodDefinition(MethodSchema(TypeSchema("void"), { VariableSchema(TypeSchema("Halley::ConfigNodeSerializationContext&"), "context"), VariableSchema(TypeSchema("Halley::ConfigNode&", true), "node") }, "deserialize"), deserializeBody);

	gen.finish()
		.writeTo(contents);

	return contents;
}

template <typename T, typename U>
Vector<U> convert(Vector<T> in, U(*f)(const T&))
{
	size_t sz = in.size();
	Vector<U> result;
	result.reserve(sz);
	for (size_t i = 0; i < sz; i++) {
		result.emplace_back(f(in[i]));
	}
	return result;
}

class SystemInfo
{
public:
	SystemInfo(SystemSchema& system)
	{
		if (system.method == SystemMethod::Update) {
			methodName = "update";
			methodArgType = "Halley::Time";
			methodArgName = "time";
			methodConst = false;
		}
		else if (system.method == SystemMethod::Render) {
			methodName = "render";
			methodArgType = "Halley::RenderContext&";
			methodArgName = "rc";
			methodConst = true;
		}
		else {
			throw Exception("Unsupported method in " + system.name + "System", HalleyExceptions::Tools);
		}

		familyArgs = { VariableSchema(TypeSchema(methodArgType), methodArgName) };
		if (system.strategy == SystemStrategy::Global) {
			stratImpl = "static_cast<T*>(this)->" + methodName + "(" + methodArgName + ");";
		} else if (system.strategy == SystemStrategy::Individual) {
			familyArgs.push_back(VariableSchema(TypeSchema("MainFamily&"), "e"));
			stratImpl = "invokeIndividual([this, &" + methodArgName + "] (auto& e) { static_cast<T*>(this)->" + methodName + "(" + methodArgName + ", e); }, mainFamily);";
		} else if (system.strategy == SystemStrategy::Parallel) {
			familyArgs.push_back(VariableSchema(TypeSchema("MainFamily&"), "e"));
			stratImpl = "invokeParallel([this, &" + methodArgName + "] (auto& e) { static_cast<T*>(this)->" + methodName + "(" + methodArgName + ", e); }, mainFamily);";
		} else {
			throw Exception("Unsupported strategy in " + system.name + "System", HalleyExceptions::Tools);
		}
	}

	String methodName;
	String methodArgType;
	String methodArgName;
	String stratImpl;
	Vector<VariableSchema> familyArgs;
	bool methodConst;
};

Vector<String> CodegenCPP::generateSystemHeader(SystemSchema& system, const HashMap<String, ComponentSchema>& components, const HashMap<String, SystemMessageSchema>& systemMessages) const
{
	auto info = SystemInfo(system);

	Vector<String> contents = {
		"#pragma once",
		"",
		"#include <halley.hpp>",
		""
	};

	// General headers
	for (auto& includeFile: system.includeFiles) {
		contents.push_back("#include \"" + includeFile + "\"");
	}
	contents.push_back("");

	// Family headers
	std::set<String> included;
	for (auto& fam : system.families) {
		for (auto& comp : fam.components) {
			if (included.find(comp.name) == included.end()) {
				auto iter = components.find(comp.name);
				if (iter != components.end()) {
					contents.emplace_back("#include \"" + getComponentFileName(iter->second) + "\"");
					included.emplace(comp.name);
				}
			}
		}
	}
	for (auto& msg : system.messages) {
		contents.emplace_back("#include \"../messages/" + toFileName(msg.name + "Message") + ".h\"");
	}
	for (auto& msg : system.systemMessages) {
		contents.emplace_back("#include \"../system_messages/" + toFileName(msg.name + "SystemMessage") + ".h\"");
	}

	contents.insert(contents.end(), {
		"",
		"// Generated file; do not modify.",
		"template <typename T>"
	});

	auto sysClassGen = CPPClassGenerator(system.name + "SystemBase", "Halley::System", MemberAccess::Private)
		.addMethodDeclaration(MethodSchema(TypeSchema("Halley::System*"), {}, "halleyCreate" + system.name + "System", false, false, false, false, true))
		.addBlankLine()
		.setAccessLevel(MemberAccess::Public);

	for (auto& fam : system.families) {
		auto members = convert<ComponentReferenceSchema, MemberSchema>(fam.components, [](auto& comp)
		{
			String type = comp.optional ? "Halley::MaybeRef<" + comp.name + "Component>" : comp.name + "Component&";
			return MemberSchema(TypeSchema(type, !comp.write), lowerFirst(comp.name));
		});

		sysClassGen
			.addClass(CPPClassGenerator(upperFirst(fam.name) + "Family", "Halley::FamilyBaseOf<" + upperFirst(fam.name) + "Family>")
				.setAccessLevel(MemberAccess::Public)
				.addMembers(members)
				.addBlankLine()
				.addTypeDefinition("Type", "Halley::FamilyType<" + String::concatList(convert<ComponentReferenceSchema, String>(fam.components, [](auto& comp)
				{
					return comp.optional ? "Halley::MaybeRef<" + comp.name + "Component>" : comp.name + "Component";
				}), ", ") + ">")
				.addBlankLine()
				.setAccessLevel(MemberAccess::Protected)
				.addConstructor(MemberSchema::toVariableSchema(members), false)
				.finish())
			.addBlankLine();
	}

	sysClassGen.setAccessLevel(MemberAccess::Protected);

	if ((int(system.access) & int(SystemAccess::API)) != 0) {
		sysClassGen.addMethodDefinition(MethodSchema(TypeSchema("const Halley::HalleyAPI&"), {}, "getAPI", true), "return doGetAPI();");
	}
	if ((int(system.access) & int(SystemAccess::World)) != 0) {
		sysClassGen.addMethodDefinition(MethodSchema(TypeSchema("Halley::World&"), {}, "getWorld", true), "return doGetWorld();");
	}
	if ((int(system.access) & int(SystemAccess::Resources)) != 0) {
		sysClassGen.addMethodDefinition(MethodSchema(TypeSchema("Halley::Resources&"), {}, "getResources", true), "return doGetResources();");
	}

	// Entity messages
	bool hasReceiveEntityMessage = false;
	Vector<String> entityMsgsReceived;
	for (auto& msg : system.messages) {
		if (msg.send) {
			sysClassGen.addMethodDefinition(MethodSchema(TypeSchema("void"), { VariableSchema(TypeSchema("Halley::EntityId"), "entityId"), VariableSchema(TypeSchema(msg.name + "Message"), "msg") }, "sendMessage"), "sendMessageGeneric(entityId, std::move(msg));");
		}
		if (msg.receive) {
			hasReceiveEntityMessage = true;
			entityMsgsReceived.push_back(msg.name + "Message::messageIndex");
		}
	}

	// System messages
	bool hasReceiveSystemMessage = false;
	Vector<String> systemMsgsReceived;
	for (auto& msg : system.systemMessages) {
		if (msg.send) {
			auto iter = systemMessages.find(msg.name);
			if (iter != systemMessages.end()) {
				const auto& sysMsg = iter->second;

				String functionName;
				Vector<VariableSchema> parameters = { VariableSchema(TypeSchema(msg.name + "SystemMessage"), "msg") };

				if (sysMsg.returnType == "void") {
					functionName = "sendSystemMessageGenericVoid<decltype(msg), decltype(callback)>";
					parameters.push_back(VariableSchema(TypeSchema("std::function<void()>"), "callback", "{}"));
				} else {
					functionName = "sendSystemMessageGeneric<decltype(msg), " + sysMsg.returnType + ", decltype(callback)>";
					parameters.push_back(VariableSchema(TypeSchema("std::function<void(" + sysMsg.returnType + ")>"), "callback"));
				}

				Vector<String> body;
				body.emplace_back("String targetSystem = \"\";");
				body.emplace_back(String(sysMsg.multicast ? "return " : "size_t n = ") + functionName + "(std::move(msg), std::move(callback), targetSystem);");
				if (!sysMsg.multicast) {
					body.emplace_back("if (n != 1) {");
					body.emplace_back("    throw Halley::Exception(\"Sending non-multicast " + sysMsg.name + "SystemMessage, but there are \" + toString(n) + \" systems receiving it (expecting exactly one).\", HalleyExceptions::Entity);");
					body.emplace_back("}");
				}

				sysClassGen
					.addMethodDefinition(MethodSchema(TypeSchema(sysMsg.multicast ? "size_t" : "void"), parameters, "sendMessage"), body)
					.addBlankLine();

				if (!sysMsg.multicast) {
					// Add system-specific overload
					parameters.insert(parameters.begin(), VariableSchema(TypeSchema("Halley::String&", true), "targetSystem"));
					body.erase(body.begin());

					sysClassGen
						.addMethodDefinition(MethodSchema(TypeSchema("void"), parameters, "sendMessage"), body)
						.addBlankLine();
				}
			} else {
				throw Exception("System message with name " + msg.name + " not found during codegen of system " + system.name, HalleyExceptions::Tools);
			}
		}
		if (msg.receive) {
			hasReceiveSystemMessage = true;
			systemMsgsReceived.push_back(msg.name + "SystemMessage::messageIndex");
		}
	}

	// Service declarations
	for (auto& service: system.services) {
		sysClassGen
			.addBlankLine()
			.setAccessLevel(MemberAccess::Private)
			.addMember(MemberSchema(service.name + "*", lowerFirst(service.name), "nullptr"))
			.setAccessLevel(MemberAccess::Protected)
			.addMethodDefinition(MethodSchema(TypeSchema(service.name + "&"), {}, "get" + service.name, true), "return *" + lowerFirst(service.name) + ";");
	}

	// Construct initBase();
	std::vector<String> initBaseMethodBody;
	for (auto& service: system.services) {
		initBaseMethodBody.push_back(lowerFirst(service.name) + " = &doGetWorld().template getService<" + service.name + ">(getName());");
	}
	initBaseMethodBody.push_back("invokeInit<T>(static_cast<T*>(this));");
	for (auto& family: system.families) {
		initBaseMethodBody.push_back("initialiseFamilyBinding<T, " + upperFirst(family.name) + "Family>(" + family.name + "Family, static_cast<T*>(this));");
	}
	
	sysClassGen
		.setAccessLevel(MemberAccess::Private)
		.addMethodDefinition(MethodSchema(TypeSchema("void"), {}, "initBase", false, false, true, true), initBaseMethodBody);

	auto fams = convert<FamilySchema, MemberSchema>(system.families, [](auto& fam) { return MemberSchema(TypeSchema("Halley::FamilyBinding<" + upperFirst(fam.name) + "Family>"), fam.name + "Family"); });
	auto mid = fams.begin() + std::min(fams.size(), size_t(1));
	std::vector<MemberSchema> mainFams(fams.begin(), mid);
	std::vector<MemberSchema> otherFams(mid, fams.end());
	sysClassGen
		.addBlankLine()
		.setAccessLevel(system.strategy == SystemStrategy::Global ? MemberAccess::Protected : MemberAccess::Private)
		.addMembers(mainFams)
		.setAccessLevel(MemberAccess::Protected)
		.addMembers(otherFams)
		.addBlankLine()
		.setAccessLevel(MemberAccess::Private)
		.addMethodDefinition(MethodSchema(TypeSchema("void"), { VariableSchema(TypeSchema(info.methodArgType), info.methodArgName) }, info.methodName + "Base", false, false, true, true), info.stratImpl)
		.addBlankLine();

	if (hasReceiveEntityMessage) {
		sysClassGen.setAccessLevel(MemberAccess::Public);
		
		Vector<String> body = { "switch (msgIndex) {" };
		for (auto& msg : system.messages) {
			if (msg.receive) {
				body.emplace_back("case " + msg.name + "Message::messageIndex: onMessagesReceived(reinterpret_cast<" + msg.name + "Message**>(msgs), idx, n); break;");

				sysClassGen
					.addMethodDeclaration(MethodSchema(TypeSchema("void"), {
						VariableSchema(TypeSchema(msg.name + "Message&", true), "msg"),
						VariableSchema(TypeSchema("MainFamily&"), "e")
					}, "onMessageReceived", false, true, false, false, false, true))
					.addBlankLine();
			}
		}
		body.emplace_back("}");

		sysClassGen
			.setAccessLevel(MemberAccess::Private)
			.addMethodDefinition(MethodSchema(TypeSchema("void"), {
				VariableSchema(TypeSchema("int"), "msgIndex"),
				VariableSchema(TypeSchema("Halley::Message**"), "msgs"),
				VariableSchema(TypeSchema("size_t*"), "idx"),
				VariableSchema(TypeSchema("size_t"), "n")
			}, "onMessagesReceived", false, false, true, true), body)
			.addBlankLine()
			.addLine("template <typename M>")
			.addMethodDefinition(MethodSchema(TypeSchema("void"), {
				VariableSchema(TypeSchema("M**"), "msgs"),
				VariableSchema(TypeSchema("size_t*"), "idx"),
				VariableSchema(TypeSchema("size_t"), "n")
			}, "onMessagesReceived"), "for (size_t i = 0; i < n; i++) static_cast<T*>(this)->onMessageReceived(*msgs[i], mainFamily[idx[i]]);")
			.addBlankLine();
	}

	if (hasReceiveSystemMessage) {
		Vector<String> canReceiveBody;
		canReceiveBody.emplace_back("if (!targetSystem.isEmpty() && targetSystem != getName()) return false;");
		canReceiveBody.emplace_back("switch (msgIndex) {");

		Vector<String> onReceivedBody = { "switch (msgIndex) {" };

		sysClassGen.setAccessLevel(MemberAccess::Public);
		for (auto& msg : system.systemMessages) {
			if (msg.receive) {
				const auto& sysMsg = systemMessages.find(msg.name)->second;
				String resultVar = sysMsg.returnType == "void" ? "" : "auto result = ";
				
				onReceivedBody.emplace_back("case " + msg.name + "SystemMessage::messageIndex: {");
				onReceivedBody.emplace_back("    auto& realMsg = reinterpret_cast<" + msg.name + "SystemMessage&>(msg);");
				if (sysMsg.multicast) {
					onReceivedBody.emplace_back("    " + resultVar + "static_cast<T*>(this)->onMessageReceived(realMsg);");
				} else {
					onReceivedBody.emplace_back("    " + resultVar + "static_cast<T*>(this)->onMessageReceived(std::move(realMsg));");
				}
				if (sysMsg.returnType == "void") {
					onReceivedBody.emplace_back("    callback(nullptr);");
				} else {
					onReceivedBody.emplace_back("    callback(reinterpret_cast<std::byte*>(&result));");
				}
				onReceivedBody.emplace_back("    break;");
				onReceivedBody.emplace_back("}");

				canReceiveBody.emplace_back("case " + msg.name + "SystemMessage::messageIndex: return true;");

				sysClassGen
					.addMethodDeclaration(MethodSchema(TypeSchema(sysMsg.returnType), {
						VariableSchema(TypeSchema(msg.name + "SystemMessage" + (sysMsg.multicast ? "&" : ""), sysMsg.multicast), "msg")
					}, "onMessageReceived", false, true, false, false, false, true))
					.addBlankLine();
			}
		}
		onReceivedBody.emplace_back("}");
		
		canReceiveBody.emplace_back("}");
		canReceiveBody.emplace_back("return false;");

		sysClassGen
			.setAccessLevel(MemberAccess::Private)
			.addMethodDefinition(MethodSchema(TypeSchema("void"), {
				VariableSchema(TypeSchema("int"), "msgIndex"),
				VariableSchema(TypeSchema("Halley::SystemMessage&"), "msg"),
				VariableSchema(TypeSchema("std::function<void(std::byte*)>&", true), "callback")
			}, "onSystemMessageReceived", false, false, true, true), onReceivedBody)
			.addMethodDefinition(MethodSchema(TypeSchema("bool"), {
				VariableSchema(TypeSchema("int"), "msgIndex"),
				VariableSchema(TypeSchema("Halley::String&", true), "targetSystem")
			}, "canHandleSystemMessage", true, false, true, true), canReceiveBody);
	}

	sysClassGen
		.setAccessLevel(MemberAccess::Public)
		.addCustomConstructor({}, {
			VariableSchema(TypeSchema(""), "System", "{" + String::concatList(convert<FamilySchema, String>(system.families, [](auto& fam) { return "&" + fam.name + "Family"; }), ", ") + "}, {" + String::concatList(entityMsgsReceived, ", ") + "}")
		}, { "static_assert(std::is_final_v<T>, \"System must be final.\");" })
		.finish()
		.writeTo(contents);

	contents.push_back("");

	return contents;
}

Vector<String> CodegenCPP::generateSystemStub(SystemSchema& system) const
{
	auto info = SystemInfo(system);
	String systemName = system.name + "System";

	Vector<String> contents = {
		"#include <systems/" + toFileName(systemName) + ".h>",
		""
	};

	auto actualSys = CPPClassGenerator(systemName, systemName + "Base<" + systemName + ">", MemberAccess::Public, true)
		.setAccessLevel(MemberAccess::Public)
		.addMethodDefinition(MethodSchema(TypeSchema("void"), info.familyArgs, info.methodName, info.methodConst), "// TODO");

	for (auto& msg : system.messages) {
		if (msg.receive) {
			actualSys
				.addBlankLine()
				.addMethodDefinition(MethodSchema(TypeSchema("void"), { VariableSchema(TypeSchema(msg.name + "Message&", true), "msg"), VariableSchema(TypeSchema("MainFamily&"), "entity") }, "onMessageReceived"), "// TODO");
		}
	}

	actualSys
		.finish()
		.writeTo(contents);

	contents.insert(contents.end(), {
		"",
		"REGISTER_SYSTEM(" + systemName + ")",
		""
	});

	return contents;
}

Vector<String> CodegenCPP::generateMessageHeader(const MessageSchema& message, const String& suffix)
{
	Vector<String> contents = {
		"#pragma once",
		"",
		"#include <halley.hpp>",
		""
	};

	for (auto& includeFile: message.includeFiles) {
		contents.push_back("#include \"" + includeFile + "\"");
	}
	contents.push_back("");

	auto gen = CPPClassGenerator(message.name + suffix, "Halley::" + suffix, MemberAccess::Public, true)
		.setAccessLevel(MemberAccess::Public)
		.addMember(MemberSchema(TypeSchema("int", false, true, true), "messageIndex", toString(message.id)))
		.addBlankLine()
		.addMembers(message.members)
		.addBlankLine()
		.addDefaultConstructor()
		.addBlankLine();

	if (!message.members.empty()) {
		gen.addConstructor(MemberSchema::toVariableSchema(message.members), true)
			.addBlankLine();
	}

	gen.addMethodDefinition(MethodSchema(TypeSchema("size_t"), {}, "getSize", true, false, true, true), "return sizeof(" + message.name + suffix + ");")
		.finish()
		.writeTo(contents);

	return contents;
}

Path CodegenCPP::makePath(Path dir, String className, String extension) const
{
	return dir / (toFileName(className) + "." + extension).cppStr();
}

String CodegenCPP::toFileName(String className) const
{
	std::stringstream ss;

	auto isUpper = [](char c) {
		return c >= 'A' && c <= 'Z';
	};

	auto lower = [](char c) {
		return static_cast<char>(c + 32);
	};

	auto tryGet = [](const String& s, size_t i) {
		if (s.size() > i) {
			return s[i];
		} else {
			return char(0);
		}
	};

	for (size_t i = 0; i < className.size(); i++) {
		if (isUpper(className[i])) {
			if (i > 0 && !isUpper(tryGet(className, i+1))) {
				ss << '_';
			}
			ss << lower(className[i]);
		} else {
			ss << className[i];
		}
	}
	return ss.str();
}

String CodegenCPP::getComponentFileName(const ComponentSchema& component) const
{
	if (component.customImplementation) {
		return component.customImplementation.value();
	} else {
		return "components/" + toFileName(component.name + "Component") + ".h";
	}
}
